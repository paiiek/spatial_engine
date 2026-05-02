// core/src/core/SpatialEngine.cpp

#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "util/RtAssertNoAlloc.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace spe::core {

SpatialEngine::SpatialEngine(int listen_port)
    : osc_backend_(
        [this](const ipc::Command& cmd) {
            // OSC thread → push to RT-safe FIFO
            util::QueuedCmd qc;
            qc.tag = cmd.tag;
            switch (cmd.tag) {
            case ipc::CommandTag::ObjMove:
                if (auto* p = std::get_if<ipc::PayloadObjMove>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.az_rad = p->az_rad;
                    qc.el_rad = p->el_rad;
                    qc.dist_m = p->dist_m;
                    qc.active = true;
                }
                break;
            case ipc::CommandTag::ObjGain:
                if (auto* p = std::get_if<ipc::PayloadObjGain>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.gain   = p->gain;
                }
                break;
            case ipc::CommandTag::ObjActive:
                if (auto* p = std::get_if<ipc::PayloadObjActive>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.active = p->active;
                }
                break;
            case ipc::CommandTag::SysReset:
                break;
            default:
                return; // not queued
            }
            cmd_fifo_.push(qc);
        },
        listen_port)
{}

SpatialEngine::~SpatialEngine() {
    osc_backend_.stop();
}

void SpatialEngine::setLayout(spe::geometry::SpeakerLayout layout) {
    layout_     = std::move(layout);
    has_layout_ = true;
}

void SpatialEngine::prepareToPlay(double sample_rate, int max_block_size) {
    sample_rate_    = sample_rate;
    max_block_size_ = max_block_size;

    // Load default layout if not set
    if (!has_layout_) {
        auto result = spe::geometry::load_layout("../configs/lab_8ch.yaml");
        if (spe::geometry::is_ok(result)) {
            layout_     = std::get<spe::geometry::SpeakerLayout>(result);
            has_layout_ = true;
        }
    }

    if (has_layout_) {
        vbap_.prepareToPlay(layout_, sample_rate);
        int n_spk = vbap_.numSpeakers();
        mix_buf_.assign(static_cast<size_t>(n_spk) * max_block_size, 0.f);
        render_ready_.store(true);
    }

    // Wire dry_ptrs_ to scratch buffers
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        dry_ptrs_[static_cast<size_t>(i)] = dry_scratch_[static_cast<size_t>(i)].data();
    }

    osc_backend_.start();
    prepared_.store(true);
}

void SpatialEngine::releaseResources() {
    osc_backend_.stop();
    render_ready_.store(false);
    prepared_.store(false);
}

void SpatialEngine::audioBlock(const spe::audio_io::AudioBlock& block) {
    SPE_RT_NO_ALLOC_SCOPE();

    if (block.num_frames > spe::MAX_BLOCK) {
        internal_xruns_.record_overrun();
        return;
    }

    // Drain OSC command FIFO directly into obj_cache_ (RT-safe, no seq issues)
    {
        util::QueuedCmd qc;
        while (cmd_fifo_.pop(qc)) {
            if (qc.obj_id >= static_cast<uint32_t>(MAX_OBJECTS)) continue;
            auto& c = obj_cache_[qc.obj_id];
            switch (qc.tag) {
            case ipc::CommandTag::ObjMove:
                c.az = qc.az_rad; c.el = qc.el_rad;
                c.dist = qc.dist_m; c.active = true;
                break;
            case ipc::CommandTag::ObjGain:
                // gain stored in StateModel for future use
                break;
            case ipc::CommandTag::ObjActive:
                c.active = qc.active;
                break;
            case ipc::CommandTag::SysReset:
                obj_cache_.fill(ObjCache{});
                break;
            default: break;
            }
        }
    }

    // Build ObjectState array from cache
    std::array<render::ObjectState, MAX_OBJECTS> obj_states{};
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        obj_states[static_cast<size_t>(i)] = {c.az, c.el, c.dist, c.active};
    }

    // Generate per-object sine tones (RT-safe: no alloc, uses std::sin)
    const float dt = 1.0f / static_cast<float>(sample_rate_);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        auto& scratch = dry_scratch_[static_cast<size_t>(i)];
        if (!obj_states[static_cast<size_t>(i)].active) {
            std::fill(scratch.begin(), scratch.begin() + block.num_frames, 0.0f);
            continue;
        }
        // Unique frequency per object: 110, 165, 220, 275, 330 ... Hz
        float freq  = 110.0f * (1 + static_cast<float>(i) * 0.5f);
        float omega = 2.0f * static_cast<float>(M_PI) * freq * dt;
        float phase = osc_phases_[static_cast<size_t>(i)];
        for (int n = 0; n < block.num_frames; ++n) {
            scratch[static_cast<size_t>(n)] = 0.25f * std::sin(phase);
            phase += omega;
        }
        // Wrap phase to avoid float drift
        while (phase > 2.0f * static_cast<float>(M_PI)) phase -= 2.0f * static_cast<float>(M_PI);
        osc_phases_[static_cast<size_t>(i)] = phase;
    }

    // VBAP spatial render
    if (render_ready_.load(std::memory_order_relaxed) && block.output_channel_count > 0) {
        int n_spk = vbap_.numSpeakers();
        std::fill(mix_buf_.begin(), mix_buf_.begin() + n_spk * block.num_frames, 0.0f);

        vbap_.processBlock(
            std::span<const render::ObjectState>(obj_states.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            mix_buf_.data(),
            block.num_frames
        );

        // Deinterleave VBAP output → planar output_channels
        int out_ch = std::min(block.output_channel_count, n_spk);
        for (int spk = 0; spk < out_ch; ++spk) {
            if (block.output_channels && block.output_channels[spk]) {
                for (int n = 0; n < block.num_frames; ++n) {
                    block.output_channels[spk][n] =
                        mix_buf_[static_cast<size_t>(n * n_spk + spk)];
                }
            }
        }
    }

    blocks_processed_.fetch_add(1, std::memory_order_relaxed);

    util::TraceEvent ev;
    ev.timestamp_ns = block.hw_timestamp_ns;
    ev.kind         = 1;
    ev.payload_a    = static_cast<std::uint32_t>(block.num_frames);
    ev.payload_b    = static_cast<std::uint32_t>(block.output_channel_count);
    trace_.push(ev);
}

}  // namespace spe::core

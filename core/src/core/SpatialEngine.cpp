// core/src/core/SpatialEngine.cpp

#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "reverb/ReverbEngine.h"
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
            case ipc::CommandTag::ObjAlgo:
                if (auto* p = std::get_if<ipc::PayloadObjAlgo>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.algo   = p->algo;
                }
                break;
            case ipc::CommandTag::NoiseType:
                if (auto* p = std::get_if<ipc::PayloadNoiseType>(&cmd.payload)) {
                    qc.noise_ch   = p->channel;
                    qc.noise_pink = p->pink;
                }
                break;
            case ipc::CommandTag::NoiseGain:
                if (auto* p = std::get_if<ipc::PayloadNoiseGain>(&cmd.payload)) {
                    qc.noise_ch     = p->channel;
                    qc.noise_gain_db = p->gain_db;
                }
                break;
            case ipc::CommandTag::TransportPlay:
            case ipc::CommandTag::TransportStop:
                // No payload; tag alone decides action
                break;
            case ipc::CommandTag::ObjDsp:
                if (auto* p = std::get_if<ipc::PayloadObjDsp>(&cmd.payload)) {
                    qc.obj_id    = p->obj_id;
                    qc.dsp_param = static_cast<uint8_t>(p->param);
                    qc.dsp_value = p->value;
                }
                break;
            case ipc::CommandTag::SysReset:
                break;
            case ipc::CommandTag::SysAmbiOrder:
                if (auto* p = std::get_if<ipc::PayloadSysAmbiOrder>(&cmd.payload)) {
                    qc.ambi_order = p->order;
                }
                break;
            case ipc::CommandTag::SysLtcChase:
                if (auto* p = std::get_if<ipc::PayloadSysLtcChase>(&cmd.payload)) {
                    qc.ltc_chase_enable = p->enable ? 1 : 0;
                }
                break;
            case ipc::CommandTag::ReverbSelect:
                if (auto* p = std::get_if<ipc::PayloadReverbSelect>(&cmd.payload)) {
                    qc.reverb_which = p->which;
                }
                break;
            case ipc::CommandTag::OutputGain:
                if (auto* p = std::get_if<ipc::PayloadOutputGain>(&cmd.payload)) {
                    qc.output_ch       = p->channel;
                    qc.output_value_db = p->gain_db;
                }
                break;
            case ipc::CommandTag::OutputLimit:
                if (auto* p = std::get_if<ipc::PayloadOutputLimit>(&cmd.payload)) {
                    qc.output_ch       = p->channel;
                    qc.output_value_db = p->threshold_db;
                }
                break;
            // Phase C3 ADM-OSC v1.0 extended tags — obj_cache_ path only (ADR 0006)
            case ipc::CommandTag::ObjMute:
                if (auto* p = std::get_if<ipc::PayloadObjMute>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.active = !p->muted; // mute=true → active=false
                }
                break;
            case ipc::CommandTag::ObjXYZ:
                if (auto* p = std::get_if<ipc::PayloadObjXYZ>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.xyz_x  = p->x;
                    qc.xyz_y  = p->y;
                    qc.xyz_z  = p->z;
                }
                break;
            case ipc::CommandTag::ObjActiveAdm:
                if (auto* p = std::get_if<ipc::PayloadObjActiveAdm>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.active = p->active;
                }
                break;
            case ipc::CommandTag::ObjWidth:
                if (auto* p = std::get_if<ipc::PayloadObjWidth>(&cmd.payload)) {
                    qc.obj_id    = p->obj_id;
                    qc.width_rad = p->width_rad;
                }
                break;
            case ipc::CommandTag::ObjName:
                if (auto* p = std::get_if<ipc::PayloadObjName>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    std::memcpy(qc.obj_name, p->name, 32);
                }
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
        dbap_.prepareToPlay(layout_, sample_rate);
        wfs_.prepareToPlay(layout_, sample_rate);
        ambisonic_.prepareToPlay(layout_, sample_rate);
        const int n_spk = vbap_.numSpeakers();
        const size_t total = static_cast<size_t>(n_spk) * max_block_size;
        mix_buf_.assign(total, 0.f);
        vbap_scratch_.assign(total, 0.f);
        dbap_scratch_.assign(total, 0.f);
        wfs_scratch_.assign(total, 0.f);
        ambisonic_scratch_.assign(total, 0.f);
        // Per-channel noise generator state (one entry per speaker output).
        // Size > 1 avoids OOB if a /noise/{ch}/* command arrives before layout.
        noise_chans_.assign(static_cast<size_t>(std::max(n_spk, 1)), NoiseChan{});

        // Per-speaker time-alignment: delay lines + gain scalars
        spk_delays_.resize(static_cast<size_t>(n_spk));
        spk_gain_lin_.resize(static_cast<size_t>(n_spk));
        spk_delay_samples_.resize(static_cast<size_t>(n_spk));
        for (int i = 0; i < n_spk; ++i) {
            spk_delays_[static_cast<size_t>(i)].prepareToPlay(sample_rate);
            const float gain_db = (i < static_cast<int>(layout_.speakers.size()))
                ? layout_.speakers[static_cast<size_t>(i)].gain_db : 0.f;
            spk_gain_lin_[static_cast<size_t>(i)] = std::pow(10.f, gain_db / 20.f);
            const float delay_ms = (i < static_cast<int>(layout_.speakers.size()))
                ? layout_.speakers[static_cast<size_t>(i)].delay_ms : 0.f;
            spk_delay_samples_[static_cast<size_t>(i)] =
                delay_ms * 0.001f * static_cast<float>(sample_rate);
        }

        // Per-channel limiters
        spk_limiters_.assign(static_cast<size_t>(n_spk), spe::dsp::ChannelLimiter{});
        for (auto& lim : spk_limiters_) lim.prepare(sample_rate);

        render_ready_.store(true);
    } else {
        // Provide minimum noise capacity (1 channel) so OSC commands don't OOB.
        if (noise_chans_.empty()) noise_chans_.resize(1);
    }

    // C1.d — initialise LtcChase ring + decoder for 25 fps default. The
    // chase is gated by ltc_chase_enable_ at audioBlock entry; until /sys/
    // ltc_chase ,i 1 fires, no input samples land in the ring.
    ltc_chase_.prepare(sample_rate, /*fps_hint=*/25);

    // FDN reverb (mono send → mono wet)
    fdn_.prepareToPlay(sample_rate, max_block_size);
    // IR convolution reverb
    reverb::ReverbConfig ir_cfg;
    ir_cfg.type       = reverb::ReverbType::IRConvolution;
    ir_cfg.sampleRate = sample_rate;
    ir_cfg.blockSize  = max_block_size;
    ir_reverb_ = reverb::createReverbEngine(ir_cfg);
    reverb_send_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    reverb_wet_buf_.assign(static_cast<size_t>(max_block_size), 0.f);

    // Per-object DSP chain (EQ → user delay → distance gain → HF rolloff → propagation delay → reverb send)
    // Heap-allocated to avoid stack overflow (each chain owns ~384 KB of delay buffers).
    if (static_cast<int>(chains_.size()) != MAX_OBJECTS) {
        chains_.clear();
        chains_.resize(MAX_OBJECTS);
    }
    for (auto& c : chains_) c.prepareToPlay(sample_rate);

    // BinauralMonitor side-output (pass-through if no SOFA file present)
    output::BinauralMonitor::Config bcfg;
    bcfg.sofaPath   = "";  // pass-through; full HRTF requires .speh path injection
    bcfg.sampleRate = static_cast<float>(sample_rate);
    bcfg.blockSize  = max_block_size;
    binaural_ok_ = (binaural_.initialize(bcfg) == output::BinauralMonitor::InitResult::Ok);
    binaural_l_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    binaural_r_buf_.assign(static_cast<size_t>(max_block_size), 0.f);

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

    // C1.d — LTC chase tap. When chase is enabled and the backend supplies
    // an input ch 0, push that block's samples into the LtcChase ring.
    // Allocation-free (SpscRing<float>::push). Decoder runs on the control
    // thread via SpatialEngine::updateLtcChase().
    if (ltc_chase_enable_.load(std::memory_order_relaxed)
        && block.input_channels != nullptr
        && block.input_channel_count > 0
        && block.input_channels[0] != nullptr) {
        ltc_chase_.pushSamples(block.input_channels[0], block.num_frames);
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
                c.gain_lin = qc.gain;
                break;
            case ipc::CommandTag::ObjActive:
                c.active = qc.active;
                break;
            case ipc::CommandTag::ObjAlgo:
                c.algo = qc.algo;
                break;
            case ipc::CommandTag::SysReset:
                obj_cache_.fill(ObjCache{});
                break;
            case ipc::CommandTag::NoiseType:
                if (qc.noise_ch < noise_chans_.size()) {
                    noise_chans_[qc.noise_ch].pink = qc.noise_pink;
                }
                break;
            case ipc::CommandTag::NoiseGain:
                if (qc.noise_ch < noise_chans_.size()) {
                    // -60 dB ≈ silent floor; convert dB → linear
                    const float g = (qc.noise_gain_db <= -60.f)
                        ? 0.f
                        : std::pow(10.f, qc.noise_gain_db / 20.f);
                    noise_chans_[qc.noise_ch].gain_lin = g;
                }
                break;
            case ipc::CommandTag::TransportPlay:
                transport_play_.store(true, std::memory_order_relaxed);
                break;
            case ipc::CommandTag::TransportStop:
                transport_play_.store(false, std::memory_order_relaxed);
                break;
            case ipc::CommandTag::ReverbSelect:
                active_reverb_.store(static_cast<int>(qc.reverb_which),
                                     std::memory_order_relaxed);
                break;
            case ipc::CommandTag::SysAmbiOrder:
                ambisonic_.setOrder(static_cast<int>(qc.ambi_order));
                break;
            case ipc::CommandTag::SysLtcChase:
                ltc_chase_enable_.store(qc.ltc_chase_enable != 0,
                                        std::memory_order_relaxed);
                break;
            case ipc::CommandTag::OutputGain:
                if (qc.output_ch < spk_gain_lin_.size())
                    spk_gain_lin_[qc.output_ch] = std::pow(10.f, qc.output_value_db / 20.f);
                break;
            case ipc::CommandTag::OutputLimit:
                if (qc.output_ch < spk_limiters_.size())
                    spk_limiters_[qc.output_ch].setThreshold(
                        std::pow(10.f, qc.output_value_db / 20.f));
                break;
            case ipc::CommandTag::ObjDsp:
                switch (qc.dsp_param) {
                case 0: c.eq_gain_db[0] = qc.dsp_value; break;
                case 1: c.eq_gain_db[1] = qc.dsp_value; break;
                case 2: c.eq_gain_db[2] = qc.dsp_value; break;
                case 3: c.eq_gain_db[3] = qc.dsp_value; break;
                case 4: c.user_delay_ms = qc.dsp_value; break;
                case 5: c.k_hf          = qc.dsp_value; break;
                case 6: c.reverb_send   = qc.dsp_value; break;
                case 7: c.width_rad     = qc.dsp_value; break;
                default: break;
                }
                break;
            // Phase C3 ADM-OSC v1.0 extended — all via obj_cache_ (ADR 0006)
            case ipc::CommandTag::ObjMute:
                c.active = qc.active; // muted → active=false
                break;
            case ipc::CommandTag::ObjActiveAdm:
                c.active = qc.active;
                break;
            case ipc::CommandTag::ObjXYZ:
                // Store Cartesian; renderers use spherical (az/el/dist).
                // Convert: x=right y=up z=forward → az/el via atan2.
                {
                    float r = std::sqrt(qc.xyz_x * qc.xyz_x +
                                        qc.xyz_y * qc.xyz_y +
                                        qc.xyz_z * qc.xyz_z);
                    if (r > 1e-6f) {
                        c.az   = std::atan2(qc.xyz_x, qc.xyz_z);
                        c.el   = std::asin(qc.xyz_y / r);
                        c.dist = r;
                    }
                    c.active = true;
                }
                break;
            case ipc::CommandTag::ObjWidth:
                c.width_rad = qc.width_rad;
                break;
            case ipc::CommandTag::ObjName:
                // Name stored in ObjCache if the field exists; otherwise no-op.
                // (ObjCache does not currently have a name field — this is a
                //  Phase C3 stub; the name is decoded and forwarded but not
                //  yet rendered. Phase C4 can surface it in /sys/state.)
                break;
            default: break;
            }
        }
    }

    // Generate per-object sine tones (RT-safe: no alloc, uses std::sin)
    const float dt = 1.0f / static_cast<float>(sample_rate_);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        auto& scratch = dry_scratch_[static_cast<size_t>(i)];
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        if (!c.active) {
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

    // Per-object DSP chain (in-place on dry_scratch_) + accumulate reverb send
    std::fill(reverb_send_buf_.begin(),
              reverb_send_buf_.begin() + block.num_frames, 0.0f);

    const float transport_gain = transport_play_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        if (!c.active) continue;

        spe::dsp::PerObjectChainParams p;
        p.dist_m         = c.dist;
        p.k_hf           = c.k_hf;
        p.reverb_send    = c.reverb_send;
        p.gain_lin       = c.gain_lin * transport_gain;
        p.user_delay_ms  = c.user_delay_ms;
        p.eq.gain_db[0]  = c.eq_gain_db[0];
        p.eq.gain_db[1]  = c.eq_gain_db[1];
        p.eq.gain_db[2]  = c.eq_gain_db[2];
        p.eq.gain_db[3]  = c.eq_gain_db[3];
        chains_[static_cast<size_t>(i)].setParams(p);

        auto& scratch = dry_scratch_[static_cast<size_t>(i)];
        for (int n = 0; n < block.num_frames; ++n) {
            float reverb_tap = 0.f;
            scratch[static_cast<size_t>(n)] =
                chains_[static_cast<size_t>(i)].processSample(
                    scratch[static_cast<size_t>(n)], reverb_tap);
            reverb_send_buf_[static_cast<size_t>(n)] += reverb_tap;
        }
    }

    // Reverb: mono in-place. Copy send → wet, then process in place.
    std::copy(reverb_send_buf_.begin(),
              reverb_send_buf_.begin() + block.num_frames,
              reverb_wet_buf_.begin());
    {
        const int rev = active_reverb_.load(std::memory_order_relaxed);
        if (rev == 1 && ir_reverb_) {
            ir_reverb_->process(reverb_wet_buf_.data(), block.num_frames);
        } else {
            fdn_.process(reverb_wet_buf_.data(), block.num_frames);
        }
    }

    // Per-algorithm spatial render (each renderer reads dry_ptrs_,
    // sees only its own algorithm's objects via the masked spans below).
    if (render_ready_.load(std::memory_order_relaxed) && block.output_channel_count > 0) {
        const int n_spk = vbap_.numSpeakers();

        std::array<render::ObjectState, MAX_OBJECTS> vbap_objs{};
        std::array<render::ObjectState, MAX_OBJECTS> dbap_objs{};
        std::array<render::ObjectState, MAX_OBJECTS> wfs_objs{};
        std::array<render::ObjectState, MAX_OBJECTS> ambisonic_objs{};
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            const auto& c = obj_cache_[static_cast<size_t>(i)];
            const render::ObjectState s = {c.az, c.el, c.dist, c.active, c.width_rad};
            switch (c.algo) {
            case ipc::Algorithm::WFS:       wfs_objs[i]       = s; break;
            case ipc::Algorithm::DBAP:      dbap_objs[i]      = s; break;
            case ipc::Algorithm::Ambisonic: ambisonic_objs[i] = s; break;
            case ipc::Algorithm::VBAP:
            default:                        vbap_objs[i]      = s; break;
            }
        }

        vbap_.processBlock(
            std::span<const render::ObjectState>(vbap_objs.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            vbap_scratch_.data(), block.num_frames);
        dbap_.processBlock(
            std::span<const render::ObjectState>(dbap_objs.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            dbap_scratch_.data(), block.num_frames);
        wfs_.processBlock(
            std::span<const render::ObjectState>(wfs_objs.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            wfs_scratch_.data(), block.num_frames);
        ambisonic_.processBlock(
            std::span<const render::ObjectState>(ambisonic_objs.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            ambisonic_scratch_.data(), block.num_frames);

        // Sum per-algorithm scratches into mix_buf_
        const int total = n_spk * block.num_frames;
        for (int idx = 0; idx < total; ++idx) {
            mix_buf_[static_cast<size_t>(idx)] =
                vbap_scratch_[static_cast<size_t>(idx)] +
                dbap_scratch_[static_cast<size_t>(idx)] +
                wfs_scratch_ [static_cast<size_t>(idx)] +
                ambisonic_scratch_[static_cast<size_t>(idx)];
        }

        // Distribute reverb wet uniformly across all speakers (energy-preserving)
        const float reverb_per_spk = 1.0f / std::sqrt(static_cast<float>(n_spk));
        for (int n = 0; n < block.num_frames; ++n) {
            const float wet = reverb_wet_buf_[static_cast<size_t>(n)] * reverb_per_spk;
            for (int s = 0; s < n_spk; ++s) {
                mix_buf_[static_cast<size_t>(n * n_spk + s)] += wet;
            }
        }

        // Deinterleave mix_buf_ → planar output_channels (speaker bus)
        // Apply per-speaker delay (DelayLine) and gain during deinterleave.
        const int out_ch = std::min(block.output_channel_count, n_spk);
        for (int spk = 0; spk < out_ch; ++spk) {
            if (block.output_channels && block.output_channels[spk]) {
                const float g = (spk < static_cast<int>(spk_gain_lin_.size()))
                    ? spk_gain_lin_[static_cast<size_t>(spk)] : 1.f;
                const float d = (spk < static_cast<int>(spk_delay_samples_.size()))
                    ? spk_delay_samples_[static_cast<size_t>(spk)] : 0.f;
                for (int n = 0; n < block.num_frames; ++n) {
                    float s = mix_buf_[static_cast<size_t>(n * n_spk + spk)] * g;
                    if (spk < static_cast<int>(spk_delays_.size())) {
                        s = spk_delays_[static_cast<size_t>(spk)].processSample(s, d);
                    }
                    block.output_channels[spk][n] = s;
                }
            }
        }

        // Per-channel noise generator (array verification): adds white/pink
        // noise scaled by gain_lin to the speaker bus. Bypasses VBAP/reverb;
        // intended for engineer to confirm physical wiring of each speaker.
        for (int spk = 0; spk < out_ch; ++spk) {
            if (spk >= static_cast<int>(noise_chans_.size())) break;
            auto& nc = noise_chans_[static_cast<size_t>(spk)];
            if (nc.gain_lin <= 0.f) continue;
            if (!block.output_channels || !block.output_channels[spk]) continue;

            constexpr float kNoiseScale = 1.0f / 2147483648.f;  // int32 → ±1 float
            constexpr float kPinkAlpha  = 0.05f;                // gentle LP for "pink"
            for (int n = 0; n < block.num_frames; ++n) {
                // xorshift32 RNG (RT-safe, deterministic)
                nc.rng ^= nc.rng << 13;
                nc.rng ^= nc.rng >> 17;
                nc.rng ^= nc.rng << 5;
                const float white = static_cast<float>(static_cast<int32_t>(nc.rng)) * kNoiseScale;
                float sample;
                if (nc.pink) {
                    nc.pink_state = nc.pink_state * (1.f - kPinkAlpha) + white * kPinkAlpha;
                    sample = nc.pink_state * 4.f;  // pink output is quieter; compensate
                } else {
                    sample = white;
                }
                block.output_channels[spk][n] += sample * nc.gain_lin * transport_gain;
            }
        }

        // Per-channel limiter (applied after all summing: spatial + reverb + noise)
        for (int spk = 0; spk < out_ch; ++spk) {
            if (!block.output_channels || !block.output_channels[spk]) continue;
            if (spk >= static_cast<int>(spk_limiters_.size())) break;
            auto& lim = spk_limiters_[static_cast<size_t>(spk)];
            for (int n = 0; n < block.num_frames; ++n) {
                block.output_channels[spk][n] = lim.processSample(block.output_channels[spk][n]);
            }
        }

        // BinauralMonitor side-output on channels [n_spk, n_spk+1]
        // Source: first active object as the primary monitor target (v0 simple policy).
        if (binaural_ok_ &&
            block.output_channel_count >= n_spk + 2 &&
            block.output_channels &&
            block.output_channels[n_spk] &&
            block.output_channels[n_spk + 1])
        {
            int first_active = -1;
            for (int i = 0; i < MAX_OBJECTS; ++i) {
                if (obj_cache_[static_cast<size_t>(i)].active) { first_active = i; break; }
            }
            if (first_active >= 0) {
                const auto& c = obj_cache_[static_cast<size_t>(first_active)];
                binaural_.setDirection(c.az, c.el);
                binaural_.processBlock(
                    dry_scratch_[static_cast<size_t>(first_active)].data(),
                    block.num_frames,
                    binaural_l_buf_.data(),
                    binaural_r_buf_.data());
            } else {
                std::fill(binaural_l_buf_.begin(),
                          binaural_l_buf_.begin() + block.num_frames, 0.f);
                std::fill(binaural_r_buf_.begin(),
                          binaural_r_buf_.begin() + block.num_frames, 0.f);
            }
            std::copy(binaural_l_buf_.begin(),
                      binaural_l_buf_.begin() + block.num_frames,
                      block.output_channels[n_spk]);
            std::copy(binaural_r_buf_.begin(),
                      binaural_r_buf_.begin() + block.num_frames,
                      block.output_channels[n_spk + 1]);
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

// core/src/audio_io/SharedRingBackend.cpp
//
// ADR 0019 Phase C PCM IPC — PR2 (backend). See SharedRingBackend.h for the
// thread model, attach()/start() contract, and the counter→OSC mapping.

#include "audio_io/SharedRingBackend.h"

#if defined(__linux__) || defined(__APPLE__)

#include "core/Constants.h"
#include "util/RtAssertNoAlloc.h"

#include <algorithm>
#include <cstring>

namespace spe::audio_io {

using shm::RingHeader;
using shm::channel_byte_offset;
using shm::kSpeRingMagic;
using shm::kRingHeaderVersion;
using shm::ProducerState;

namespace {

bool is_power_of_two(std::uint32_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

}  // namespace

// ── attach() ────────────────────────────────────────────────────────────
// OS-level mmap ONLY; no header validation. nullptr on OS failure.

std::unique_ptr<SharedRingBackend> SharedRingBackend::attach(const std::string& path,
                                                             shm::AttachMode mode) {
    // We do not yet know the region size; the producer sized it via
    // total_region_bytes(channels, capacity). We must map at least the header
    // to read channels/capacity, then re-map the full region. Map the header
    // page first to learn the geometry.
    auto backend = std::unique_ptr<SharedRingBackend>(new SharedRingBackend());

    shm::SharedMemoryRegion probe;
    if (probe.attach(path.c_str(), mode, shm::kRingHeaderSize) != shm::RegionError::Ok) {
        return nullptr;
    }
    const RingHeader* h = probe.header();
    const std::uint32_t channels  = h->channels;
    const std::uint32_t capacity  = h->capacity_frames;
    probe.detach();

    const std::size_t full_bytes = shm::total_region_bytes(channels, capacity);
    if (backend->region_.attach(path.c_str(), shm::AttachMode::OpenExisting, full_bytes)
            != shm::RegionError::Ok) {
        return nullptr;
    }
    return backend;
}

SharedRingBackend::~SharedRingBackend() {
    stop();
}

std::string SharedRingBackend::description() const {
    return "SharedRingBackend(rate=" + std::to_string(static_cast<int>(sample_rate_))
         + ",in_ch=" + std::to_string(in_channels_)
         + ",block=" + std::to_string(block_size_)
         + ",cap=" + std::to_string(masking_capacity_) + ")";
}

// ── prepare(): all gates IN ORDER + all allocation ────────────────────────

BackendError SharedRingBackend::prepare(AudioCallback* callback, int engine_block_size) {
    if (running_)   return BackendError::AlreadyStarted;
    if (!callback)  return BackendError::DeviceOpenFailed;
    if (!region_.is_attached()) return BackendError::DeviceOpenFailed;

    const RingHeader* h = region_.header();

    // Gate 1: magic + version.
    if (h->magic != kSpeRingMagic || h->version != kRingHeaderVersion) {
        return BackendError::DeviceOpenFailed;
    }

    const std::uint32_t hdr_block = h->block_size;
    const std::uint32_t hdr_cap   = h->capacity_frames;

    // Gate 2: block_size > MAX_BLOCK strictly (the ONLY gate returning this code).
    if (hdr_block > static_cast<std::uint32_t>(spe::MAX_BLOCK)) {
        return BackendError::BlockSizeExceedsMax;
    }

    // Gate 3: block_size must divide the engine block, and must not exceed
    // the ring capacity (a block larger than the ring can never be satisfied).
    if (hdr_block == 0
        || engine_block_size <= 0
        || (static_cast<std::uint32_t>(engine_block_size) % hdr_block) != 0
        || hdr_block > hdr_cap) {
        return BackendError::BlockConfigMismatch;
    }

    // Gate 4: capacity must be a power of two (masking idx & (cap-1) is only
    // correct for pow2; the consumer NEVER rounds — masking capacity is always
    // header.capacity_frames verbatim).
    if (!is_power_of_two(hdr_cap)) {
        return BackendError::DeviceOpenFailed;
    }

    // All gates passed — commit geometry + allocate.
    sample_rate_      = static_cast<double>(h->sample_rate);
    in_channels_      = static_cast<int>(h->channels);
    block_size_       = static_cast<int>(hdr_block);
    engine_block_     = engine_block_size;
    masking_capacity_ = hdr_cap;

    // Resync the consumer cursor to the producer's read_idx (PR3 will revisit
    // the resync-on-attach policy as case 9; PR2 mirrors the header read_idx).
    read_idx_local_ = h->read_idx.load(std::memory_order_relaxed);

    allocate_staging();
    callback_ = callback;
    return BackendError::Ok;
}

void SharedRingBackend::allocate_staging() {
    staging_flat_.assign(static_cast<std::size_t>(in_channels_) * block_size_, 0.0f);
    staging_channels_.resize(static_cast<std::size_t>(in_channels_));
    for (int ch = 0; ch < in_channels_; ++ch) {
        staging_channels_[static_cast<std::size_t>(ch)] =
            staging_flat_.data() + static_cast<std::size_t>(ch) * block_size_;
    }
}

// ── start() ───────────────────────────────────────────────────────────────

BackendError SharedRingBackend::start(AudioCallback* callback) {
    // AudioBackend seam: default engine block to the header block_size (which
    // trivially divides itself). PR3 wires the real engine block via the
    // companion overload.
    if (!region_.is_attached()) return BackendError::DeviceOpenFailed;
    return start(callback, static_cast<int>(region_.header()->block_size));
}

BackendError SharedRingBackend::start(AudioCallback* callback, int engine_block_size) {
    const BackendError err = prepare(callback, engine_block_size);
    if (err != BackendError::Ok) return err;

    callback_->prepareToPlay(sample_rate_, block_size_);
    running_ = true;
    // NOTE: production cadence (the worker std::thread with sleep_until, like
    // NullBackend::thread_loop) is wired in PR3; PR2 ships the RT body
    // (pump_block) + the deterministic pump_synchronous test hook. The worker
    // thread will read steady_clock BEFORE pump_block and do the sleep_until
    // cadence OUTSIDE pump_block.
    return BackendError::Ok;
}

BackendError SharedRingBackend::stop() noexcept {
    if (!running_) return BackendError::NotStarted;
    running_ = false;
    if (callback_) {
        callback_->releaseResources();
        callback_ = nullptr;
    }
    return BackendError::Ok;
}

// ── Diagnostics accessors ──────────────────────────────────────────────────

std::uint64_t SharedRingBackend::producer_heartbeat_ms() const noexcept {
    if (!region_.is_attached()) return 0;
    return region_.header()->producer_heartbeat_ms.load(std::memory_order_acquire);
}

std::uint32_t SharedRingBackend::producer_state() const noexcept {
    if (!region_.is_attached()) return 0;
    return region_.header()->producer_state.load(std::memory_order_acquire);
}

// ── pump_block(): the RT body ───────────────────────────────────────────────
// SPSC contract — see header. Only loads/stores/memcpy/memset; no clock read
// (hw_ts_ns is a param), no syscall, no allocation.

void SharedRingBackend::pump_block(AudioCallback* callback, std::uint64_t hw_ts_ns) noexcept {
    SPE_RT_NO_ALLOC_SCOPE();

    const RingHeader* h = region_.header();
    const std::uint32_t cap   = masking_capacity_;
    const std::uint32_t block = static_cast<std::uint32_t>(block_size_);

    // (1) Single acquire load of write_idx at the very top — the
    //     synchronizes-with edge with the producer's release store. No sample
    //     read may be hoisted above this.
    const std::uint64_t write_idx = h->write_idx.load(std::memory_order_acquire);

    // (2) available = write_idx - read_idx_local (read_idx is consumer-owned).
    const std::uint64_t available = write_idx - read_idx_local_;

    const char* base = static_cast<const char*>(region_.base());

    if (available >= block) {
        // (3) Copy each channel's block frames, handling the capacity-wrap
        //     split as up to two memcpys (PM1). cap is always
        //     header.capacity_frames (pow2), so idx & (cap-1) masks correctly.
        const std::uint32_t idx   = static_cast<std::uint32_t>(read_idx_local_ & (cap - 1));
        const std::uint32_t first = std::min(block, cap - idx);
        const std::uint32_t rest  = block - first;
        for (int ch = 0; ch < in_channels_; ++ch) {
            const char* ch_base = base + channel_byte_offset(static_cast<std::uint32_t>(ch), cap);
            float* dst = staging_flat_.data() + static_cast<std::size_t>(ch) * block_size_;
            std::memcpy(dst,
                        ch_base + static_cast<std::size_t>(idx) * sizeof(float),
                        static_cast<std::size_t>(first) * sizeof(float));
            if (rest > 0) {
                std::memcpy(dst + first,
                            ch_base,  // wrap to channel base (frame 0)
                            static_cast<std::size_t>(rest) * sizeof(float));
            }
        }
        read_idx_local_ += block;
    } else {
        // Underrun: silence + backend-private xrun record. DO NOT advance
        // read_idx past write_idx.
        std::memset(staging_flat_.data(), 0,
                    staging_flat_.size() * sizeof(float));
        xruns_.record_underrun();
        ++underrun_warnings_;  // maps to shm_underrun (PR4)
    }

    // (4) Publish the consumed range with a release store — only after the
    //     copy completed.
    region_.header()->read_idx.store(read_idx_local_, std::memory_order_release);

    // (5) Build the AudioBlock (input-only) and call the engine.
    AudioBlock blk;
    blk.output_channels      = nullptr;
    blk.input_channels       = staging_channels_.data();
    blk.output_channel_count = 0;
    blk.input_channel_count  = in_channels_;
    blk.num_frames           = block_size_;
    blk.sample_rate          = sample_rate_;
    blk.hw_timestamp_ns      = hw_ts_ns;
    callback->audioBlock(blk);
}

BackendError SharedRingBackend::pump_synchronous(AudioCallback* callback, int blocks,
                                                 std::uint64_t hw_ts_base,
                                                 int engine_block_size) {
    const BackendError err = prepare(callback, engine_block_size);
    if (err != BackendError::Ok) return err;

    callback_->prepareToPlay(sample_rate_, block_size_);
    for (int i = 0; i < blocks; ++i) {
        const std::uint64_t hw_ts =
            hw_ts_base + static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(block_size_);
        pump_block(callback, hw_ts);
    }
    callback_->releaseResources();
    callback_ = nullptr;
    return BackendError::Ok;
}

// ── poll_diagnostics(): control thread only ─────────────────────────────────
// READS header atomics + private state; WRITES only the warning counters,
// previous-pts cache, and per-code last-warning timestamps. NEVER advances
// read_idx, touches staging, or writes a header atomic.

void SharedRingBackend::poll_diagnostics(std::uint64_t now_ms, std::uint64_t now_ns) noexcept {
    (void)now_ns;
    const RingHeader* h = region_.header();

    // attached-no-data: once-on-attach latch while write_idx == read_idx == 0.
    if (!attached_no_data_latched_) {
        const std::uint64_t wi = h->write_idx.load(std::memory_order_acquire);
        const std::uint64_t ri = h->read_idx.load(std::memory_order_acquire);
        if (wi == 0 && ri == 0) {
            ++attached_no_data_warnings_;
            attached_no_data_latched_ = true;
        }
    }

    // Heartbeat staleness: stale := (now_ms - producer_heartbeat_ms) > 100,
    // rate-limited to once per 30 000 ms.
    const std::uint64_t hb = h->producer_heartbeat_ms.load(std::memory_order_acquire);
    if (now_ms >= hb && (now_ms - hb) > kStaleThresholdMs) {
        const bool window_open =
            !have_stale_warn_ts_ || (now_ms - last_stale_warn_ms_) >= kStaleRateLimitMs;
        if (window_open) {
            ++stale_warnings_;
            last_stale_warn_ms_ = now_ms;
            have_stale_warn_ts_ = true;
        }
    }

    // Pacing drift: block_period_ns = 1e9 * block_size / sample_rate;
    // drift := pts_delta_ns > block_period_ns, rate-limited to once per 5 000 ms.
    const std::uint64_t pts = h->producer_meta_block_pts_ns.load(std::memory_order_acquire);
    if (!have_prev_pts_) {
        prev_pts_ns_   = pts;
        have_prev_pts_ = true;
    } else if (pts != prev_pts_ns_) {
        const std::uint64_t delta = (pts > prev_pts_ns_) ? (pts - prev_pts_ns_)
                                                         : (prev_pts_ns_ - pts);
        prev_pts_ns_ = pts;
        const std::uint64_t block_period_ns = static_cast<std::uint64_t>(
            1e9 * static_cast<double>(block_size_) / sample_rate_);
        if (delta > block_period_ns) {
            const bool window_open =
                !have_pacing_warn_ts_ || (now_ms - last_pacing_warn_ms_) >= kPacingRateLimitMs;
            if (window_open) {
                ++pacing_warnings_;
                last_pacing_warn_ms_ = now_ms;
                have_pacing_warn_ts_ = true;
            }
        }
    }
}

}  // namespace spe::audio_io

#endif  // defined(__linux__) || defined(__APPLE__)

// core/src/audio_io/SharedRingBackend.cpp
//
// ADR 0019 Phase C PCM IPC — PR2 (backend). See SharedRingBackend.h for the
// thread model, attach()/start() contract, and the counter→OSC mapping.

#include "audio_io/SharedRingBackend.h"

#if defined(__linux__) || defined(__APPLE__)

#include "core/Constants.h"
#include "util/RtAssertNoAlloc.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>

#include <unistd.h>   // getpid

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

    // FIX-2: enforce geometry bounds before computing total_region_bytes so we
    // never pass an overflow-inducing channels/capacity to mmap. A buggy or
    // stale producer that wrote huge values is rejected here with nullptr.
    if (channels == 0u || channels > kMaxChannels) {
        return nullptr;
    }
    if (capacity == 0u || capacity > kMaxCapacityFrames) {
        return nullptr;
    }

    const std::size_t full_bytes = shm::total_region_bytes(channels, capacity);

    if (backend->region_.attach(path.c_str(), shm::AttachMode::OpenExisting, full_bytes)
            != shm::RegionError::Ok) {
        return nullptr;
    }

    // FIX-3: verify the actual shm backing object is at least as large as the
    // geometry claims. mmap() on Linux succeeds even when the requested size
    // exceeds the object size — accesses beyond the backing store then SIGBUS
    // on the audio thread. backing_size() returns the fstat st_size captured
    // at attach time. (Additive PR1 accessor — see SharedMemoryRegion.h.)
    // NOTE: this is a point-in-time snapshot; a producer that ftruncate-shrinks
    // the object AFTER attach can still induce SIGBUS. That is an accepted
    // same-host, file-permission-controlled risk per ADR 0019 §10 (rings are
    // not authenticated; a producer corrupting its own live ring is out of
    // scope) — the RT availability clamp + geometry ceiling bound the rest.
    const std::size_t backing = backend->region_.backing_size();
    if (backing > 0 && backing < full_bytes) {
        backend->region_.detach();
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

BackendError SharedRingBackend::prepare(AudioCallback* callback, int engine_block_size,
                                        int out_channels, bool acquire_consumer_lock) {
    if (running_.load())   return BackendError::AlreadyStarted;
    if (!callback)  return BackendError::DeviceOpenFailed;
    if (!region_.is_attached()) return BackendError::DeviceOpenFailed;

    const RingHeader* h = region_.header();

    // Gate 1: magic + version + header_size (FIX-6: fold header_size into the
    // identity check — a wrong header_size signals a different wire format).
    if (h->magic != kSpeRingMagic
        || h->version != kRingHeaderVersion
        || h->header_size != shm::kRingHeaderSize) {
        return BackendError::DeviceOpenFailed;
    }

    const std::uint32_t hdr_block = h->block_size;
    const std::uint32_t hdr_cap   = h->capacity_frames;
    const std::uint32_t hdr_ch    = h->channels;

    // Gate 1b: geometry bounds (FIX-2). Checked before any arithmetic that
    // uses channels/capacity so total_region_bytes() cannot overflow and
    // staging alloc cannot request hundreds of GiB.
    if (hdr_ch == 0u || hdr_ch > kMaxChannels) {
        return BackendError::DeviceOpenFailed;
    }
    if (hdr_cap == 0u || hdr_cap > kMaxCapacityFrames) {
        return BackendError::DeviceOpenFailed;
    }

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

    // Gate 5: sample_rate must be in a sane range (prevents div-by-zero in
    // poll_diagnostics and any downstream period computation).
    const std::uint32_t hdr_sr = h->sample_rate;
    if (hdr_sr < 8000u || hdr_sr > 768000u) {
        return BackendError::SampleRateUnsupported;
    }

    // Gate 6: consumer can never be ahead of the producer; a stale/corrupted
    // region with read_idx > write_idx would cause uint64 underflow in
    // pump_block's availability computation → unbounded OOB copy on the audio
    // thread. Reject it here (control thread, before any worker starts).
    const std::uint64_t hdr_wi = h->write_idx.load(std::memory_order_acquire);
    const std::uint64_t hdr_ri = h->read_idx.load(std::memory_order_acquire);
    if (hdr_ri > hdr_wi) {
        return BackendError::DeviceOpenFailed;
    }

    // Decision 1 (case 9) — gap-gated resync, STRICT static-overwrite predicate.
    // Compute the seed for read_idx_local_ here (no side effects yet); the
    // store-back to header.read_idx is deferred until after Gate 7 succeeds so a
    // rejected second consumer never mutates the producer-owned header.
    //
    //   backlog = hdr_wi - hdr_ri  (hdr_wi >= hdr_ri — Gate 6 passed, no underflow)
    //   The resident window is [write_idx - capacity, write_idx). hdr_ri's frame
    //   is physically overwritten IFF hdr_ri < write_idx - capacity, i.e.
    //   backlog > capacity_frames STRICTLY. At backlog == capacity exactly, hdr_ri
    //   is the OLDEST STILL-RESIDENT frame — valid data that must be played, NOT
    //   resynced (the == C boundary stays on the verbatim seed). So the trigger
    //   is strict '>': it precisely matches case 9 (backlog > C) without touching
    //   cases 3/4 (backlog 0 / 128 « C) or the == C resident boundary.
    const std::uint64_t backlog = hdr_wi - hdr_ri;
    const std::uint32_t block   = static_cast<std::uint32_t>(hdr_block);
    bool resynced = false;
    if (backlog > hdr_cap) {
        // Static-overwrite: the oldest unread frame is already overwritten.
        // Resync to the newest block's start. Underflow clamp to 0 when
        // write_idx < block (defensively dead — Gate 3 rejects block > capacity,
        // so backlog > capacity implies write_idx >= block in practice).
        read_idx_local_ = (hdr_wi >= block) ? (hdr_wi - block) : 0u;
        resynced = true;
    } else {
        read_idx_local_ = hdr_ri;  // PR2 verbatim seed (cases 3/4 + the == C boundary)
    }

    // Gate 7 (Decision 2): consumer attach-lock — SPSC single-consumer
    // enforcement. CAS 0→getpid() on the lock word at kConsumerLockOffset.
    // Runs BEFORE the resync store-back, the geometry commit, and
    // allocate_staging() so a rejected second consumer mutates nothing and
    // allocates nothing (PR2 "on gate failure, allocate nothing" invariant).
    // ::kill / ::getpid are control-thread calls (start()), never on the RT path.
    // pump_synchronous passes acquire_consumer_lock=false so the test hook can
    // double-attach without tripping the reject.
    if (acquire_consumer_lock) {
        std::atomic<std::uint32_t>* lock = shm::consumer_lock_atomic(region_.header());
        const std::uint32_t me = static_cast<std::uint32_t>(::getpid());
        std::uint32_t expected = 0;
        if (!lock->compare_exchange_strong(expected, me, std::memory_order_acq_rel)) {
            // Lock held. Reclaim only if the holder is dead (kill(pid,0) reports
            // ESRCH). PID-recycle false-negative is an accepted, fail-safe
            // (refuse, never corrupt) same-host risk per ADR §10.
            if (::kill(static_cast<pid_t>(expected), 0) == -1 && errno == ESRCH) {
                // Stale holder dead → try to steal: CAS expected→me.
                if (!lock->compare_exchange_strong(expected, me, std::memory_order_acq_rel)) {
                    return BackendError::ConsumerAlreadyAttached;  // someone else won the race
                }
                // stole it
            } else {
                // Live holder (foreign live process, or re-entrant same pid that
                // already holds it without a prior stop()) → refuse.
                return BackendError::ConsumerAlreadyAttached;
            }
        }
        holds_consumer_lock_ = true;
    }

    // Store-release the resync'd value into header.read_idx (PR5 producer
    // handshake, Decision 1 step 3 / ADR §4.2). Only on the resync branch, and
    // only after the gates above (incl. Gate 7 once wired) succeed — so a
    // rejected consumer never writes the header. CONTROL-THREAD-ONLY write
    // (before any worker spawns); the non-resync branch leaves read_idx untouched
    // (PR2 behavior preserved).
    if (resynced) {
        region_.header()->read_idx.store(read_idx_local_, std::memory_order_release);
    }

    // All gates passed — commit geometry + allocate.
    sample_rate_      = static_cast<double>(hdr_sr);
    in_channels_      = static_cast<int>(h->channels);
    out_channels_     = out_channels;   // engine output channel count (Decision 3(ii))
    block_size_       = static_cast<int>(hdr_block);
    engine_block_     = engine_block_size;
    masking_capacity_ = hdr_cap;

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

    // Output staging the engine renders into (Decision 3(ii)). Sized from the
    // member set in prepare() before this call. When out_channels_ == 0 (PR2
    // test path) both vectors stay empty — no alloc, pump_block stays
    // byte-identical to PR2. Mirrors NullBackend's flat_buffer_ layout.
    if (out_channels_ > 0) {
        out_staging_flat_.assign(static_cast<std::size_t>(out_channels_) * block_size_, 0.0f);
        out_staging_channels_.resize(static_cast<std::size_t>(out_channels_));
        for (int ch = 0; ch < out_channels_; ++ch) {
            out_staging_channels_[static_cast<std::size_t>(ch)] =
                out_staging_flat_.data() + static_cast<std::size_t>(ch) * block_size_;
        }
    } else {
        out_staging_flat_.clear();
        out_staging_channels_.clear();
    }
}

// ── start() ───────────────────────────────────────────────────────────────

BackendError SharedRingBackend::start(AudioCallback* callback) {
    // AudioBackend seam: default engine block to the header block_size (which
    // trivially divides itself), out_channels = 0 (no output staging).
    if (!region_.is_attached()) return BackendError::DeviceOpenFailed;
    return start(callback, static_cast<int>(region_.header()->block_size), 0,
                 /*spawn_worker=*/false);
}

BackendError SharedRingBackend::start(AudioCallback* callback, int engine_block_size) {
    return start(callback, engine_block_size, 0, /*spawn_worker=*/false);
}

BackendError SharedRingBackend::start(AudioCallback* callback, int engine_block_size,
                                      int out_channels) {
    // The 3-arg form is the production / AC-5 path: it spawns the worker thread.
    return start(callback, engine_block_size, out_channels, /*spawn_worker=*/true);
}

// Private unified start: gates + alloc + prepareToPlay + (optionally) the worker.
// spawn_worker is true ONLY for the 3-arg production/CLI/AC-5 path; the 1-arg /
// 2-arg overloads (used by the frozen PR2 manual-pump_block ctest cases) set
// running_=true WITHOUT a worker so a test that calls pump_block() directly is
// not raced by a concurrent worker (preserves AC-7 byte-identical behavior).
// All start() paths still acquire the consumer lock (acquire_consumer_lock=true,
// Gap-b). See the §Deviations note: the plan's literal "spawn worker in every
// start()" is inconsistent with the frozen manual-pump cases, which it elsewhere
// (Decision 3(i)/AC-5) requires to stay thread-free.
BackendError SharedRingBackend::start(AudioCallback* callback, int engine_block_size,
                                      int out_channels, bool spawn_worker) {
    const BackendError err = prepare(callback, engine_block_size, out_channels,
                                     /*acquire_consumer_lock=*/true);
    if (err != BackendError::Ok) return err;

    callback_->prepareToPlay(sample_rate_, block_size_);
    running_.store(true);
    if (spawn_worker) {
        // Production cadence (PR3 Decision 3(i)): spawn the worker AFTER gates +
        // alloc + prepareToPlay. The worker reads steady_clock BEFORE pump_block
        // and does the sleep_until cadence OUTSIDE pump_block (NullBackend shape).
        worker_ = std::thread(&SharedRingBackend::thread_loop, this);
    }
    return BackendError::Ok;
}

BackendError SharedRingBackend::stop() noexcept {
    if (!running_.load()) return BackendError::NotStarted;
    running_.store(false);
    // Join the worker BEFORE releaseResources + lock release so no pump_block
    // runs after the callback's resources are freed (PR3 Decision 3(i)).
    if (worker_.joinable()) worker_.join();
    if (callback_) {
        callback_->releaseResources();
        callback_ = nullptr;
    }
    // Release the consumer attach-lock (Decision 2 step 4): only if WE hold it
    // (holds_consumer_lock_ guards against double-release / clobbering a holder
    // that reclaimed our slot). A bare release store is correct — nobody else
    // can hold it while we do. Reset the flag immediately so a ~dtor stop()
    // after an explicit stop() is idempotent (no re-store).
    if (holds_consumer_lock_ && region_.is_attached()) {
        shm::consumer_lock_atomic(region_.header())->store(0, std::memory_order_release);
        holds_consumer_lock_ = false;
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

std::uint32_t SharedRingBackend::consumerLockWord() const noexcept {
    if (!region_.is_attached()) return 0;
    // consumer_lock_atomic() takes a non-const RingHeader*; reading the shared
    // atomic word is logically const (no header WRITE), so const_cast away the
    // const from region_.header() (PR4 CR-1).
    return shm::consumer_lock_atomic(
               const_cast<shm::RingHeader*>(region_.header()))
        ->load(std::memory_order_acquire);
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

    // (2) Signed-safe availability: if write_idx ever regresses below
    //     read_idx_local_ (stale/corrupted region mid-run), clamp to 0 so we
    //     take the silence branch rather than computing a ~1.8e19 "available"
    //     that would drive an unbounded OOB memcpy on the audio thread.
    const std::uint64_t available = (write_idx >= read_idx_local_)
                                    ? (write_idx - read_idx_local_)
                                    : 0u;

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
        // (4a) Publish the consumed range with a release store — only after the
        //      copy completes, and ONLY on the consume branch. Skipping this on
        //      underrun avoids a spurious cross-process cache-line write each
        //      silent block (NIT-b).
        region_.header()->read_idx.store(read_idx_local_, std::memory_order_release);
    } else {
        // Underrun: silence + backend-private xrun record. DO NOT advance
        // read_idx past write_idx; do NOT write the header (NIT-b).
        std::memset(staging_flat_.data(), 0,
                    staging_flat_.size() * sizeof(float));
        xruns_.record_underrun();
        ++underrun_warnings_;  // maps to shm_underrun (PR4)
    }

    // (5) Build the AudioBlock and call the engine. The engine renders into the
    //     backend-owned output staging (Decision 3(ii)); when out_channels_ == 0
    //     (PR2 test path) this is byte-identical to PR2 (nullptr/0, no fill).
    //     std::fill over a pre-sized vector is alloc-free (RT-safe), mirroring
    //     NullBackend's per-block output zero (NullBackend.cpp:104).
    if (out_channels_ > 0) {
        std::fill(out_staging_flat_.begin(), out_staging_flat_.end(), 0.0f);
    }
    AudioBlock blk;
    blk.output_channels      = (out_channels_ > 0) ? out_staging_channels_.data() : nullptr;
    blk.input_channels       = staging_channels_.data();
    blk.output_channel_count = out_channels_;
    blk.input_channel_count  = in_channels_;
    blk.num_frames           = block_size_;
    blk.sample_rate          = sample_rate_;
    blk.hw_timestamp_ns      = hw_ts_ns;
    callback->audioBlock(blk);
}

BackendError SharedRingBackend::pump_synchronous(AudioCallback* callback, int blocks,
                                                 std::uint64_t hw_ts_base,
                                                 int engine_block_size,
                                                 int out_channels) {
    // acquire_consumer_lock=false: the synchronous test hook never takes the
    // SPSC consumer lock (so a test can double-attach via this path), and never
    // spawns the worker thread (running_ stays false).
    const BackendError err = prepare(callback, engine_block_size, out_channels,
                                     /*acquire_consumer_lock=*/false);
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

// ── thread_loop(): production cadence (PR3 Decision 3(i)) ────────────────────
// Mirrors NullBackend::thread_loop. Reads steady_clock::now() BEFORE pump_block
// (so the RT body has no clock read — hw_ts_ns is a parameter) and does the
// sleep_until cadence OUTSIDE pump_block.

void SharedRingBackend::thread_loop() {
    using clock = std::chrono::steady_clock;
    const auto block_period = std::chrono::nanoseconds(
        static_cast<std::int64_t>(1e9 * block_size_ / sample_rate_));

    auto next_deadline = clock::now() + block_period;

    while (running_.load(std::memory_order_acquire)) {
        // Clock read OUTSIDE pump_block (PR2 "hw_ts is a param" contract).
        const std::uint64_t now_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count());
        if (callback_) pump_block(callback_, now_ns);

        const auto now = clock::now();
        if (now > next_deadline) {
            xruns_.record_underrun();
            next_deadline = now + block_period;
        } else {
            std::this_thread::sleep_until(next_deadline);
            next_deadline += block_period;
        }
    }
}

// ── poll_diagnostics(): control thread only ─────────────────────────────────
// READS header atomics + private state; WRITES only the warning counters,
// previous-pts cache, and per-code last-warning timestamps. NEVER advances
// read_idx, touches staging, or writes a header atomic.

void SharedRingBackend::poll_diagnostics(std::uint64_t now_ms, std::uint64_t now_ns) noexcept {
    // now_ns is intentionally unused in PR2. PR4 will pass it through to the
    // /sys/warning OSC timestamp field when wiring the real emission on top of
    // these counter edges — keeping the param now preserves the PR4 call-site
    // interface without an API break. (NIT-a)
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

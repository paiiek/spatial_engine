// core/src/output_backend/BinauralMonitor.cpp

#include "output_backend/BinauralMonitor.h"
#include "ambi/AllRADTDesigns.hpp"
#include "hrtf/HrtfLookup.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace spe::output {

BinauralMonitor::InitResult BinauralMonitor::initialize(const Config& cfg)
{
    block_size_  = std::min(cfg.blockSize, MAX_BLOCK);
    sample_rate_ = cfg.sampleRate;

    // Q2 — pre-compute linear ramp envelopes once block_size_ is known.
    // Recomputed on every initialize() so block-size changes propagate.
    buildXfadeEnvelopes();
    // Reset audio-thread-local crossfade state to a known steady value so a
    // re-initialise (e.g. SOFA path change) does not strand an in-flight ramp.
    prev_effective_mode_         = effective_mode_.load(std::memory_order_acquire);
    xfade_blocks_remaining_      = 0;
    xfade_total_blocks_          = 0;
    outgoing_mode_               = prev_effective_mode_;
    incoming_mode_               = prev_effective_mode_;
    xfade_blocks_remaining_atomic_.store(0, std::memory_order_release);
    xfade_truncated_pending_.store(false, std::memory_order_release);

    // v0.6 D-M1 — Architect retroactive review caught a silent contract
    // violation: the v0.6 #5 sticky-demote state (strikes + demoted + warning
    // latch) was documented as "cleared by initialize()" in the plan, the
    // commit message, the CHANGELOG, and CH7_BINAURAL.md §7.5.4, but
    // initialize() did not actually clear it. The bug was invisible to ctest
    // because the unit test used clearRuntimeDemoteForTest() (the test-only
    // hook), never the production initialize() reset path.
    //
    // The 3 stores below restore the documented "sticky until next
    // prepareToPlay" contract. After this reset, a host that experienced a
    // demote in the previous prepareToPlay lifecycle starts fresh on the
    // next prepareToPlay and may re-attempt B2 — which may or may not
    // re-demote depending on the host's actual sustained budget. That
    // re-evaluation cycle is the design intent (CH7 §7.5.4 — to be added in
    // the same v0.6.1 doc-tightening commit).
    runtime_demote_strikes_.store(0, std::memory_order_release);
    runtime_demoted_.store(false, std::memory_order_release);
    runtime_demote_warning_pending_.store(false, std::memory_order_release);

    // v0.6 D-M2 — re-probe steady_clock::now() vDSO availability on every
    // initialize() so platform/kernel changes between prepareToPlay calls
    // are picked up. The probe writes both steady_clock_fast_ and (on slow
    // result) rt_timing_unavailable_pending_. Reset both atomics to fresh-
    // start values first so a previously slow probe doesn't leak into a
    // fast re-init.
    steady_clock_fast_.store(true, std::memory_order_release);
    rt_timing_unavailable_pending_.store(false, std::memory_order_release);
    probeSteadyClockSpeed();

    if (cfg.sofaPath.empty()) {
        // Pass-through mode: do NOT prime per-object slots. processBlockForObject
        // becomes a no-op writer for unprimed slots, processBlock falls back to
        // identity-copy.
        initialized_ = true;
        hrtf_loaded_ = false;
        return InitResult::Ok;
    }

    hrtf::SpehResult res = hrtf::loadSpeh(cfg.sofaPath, cfg.sampleRate, table_);
    switch (res) {
    case hrtf::SpehResult::Ok:               break;
    case hrtf::SpehResult::FileNotFound:     return InitResult::SofaNotFound;
    case hrtf::SpehResult::SampleRateMismatch: return InitResult::SofaSampleRateMismatch;
    case hrtf::SpehResult::IRLengthUnsupported: return InitResult::SofaIRLengthUnsupported;
    default:                                 return InitResult::SofaInvalidFormat;
    }

    hrtf_loaded_ = true;

    // Build the KD-tree on positions (control thread, allocates OK).
    tree_.build(table_);

    // Prime all OlaConvolvers to worst-case capacity so loadInto() never allocates.
    primeAllSlots();

    // v0.5 P4: build the B2 AmbiVS chain (24-pt t-design + 48 convolvers +
    // per-VS HRIR cache). Independent of physical speaker layout.
    initializeB2();

    // Backwards-compat: auto-prime object 0 at (az=0, el=0) so legacy
    // single-source callsites can call processBlock() immediately after
    // initialize() without an explicit setDirection().
    setDirection(0, 0.f, 0.f);

    initialized_ = true;
    return InitResult::Ok;
}

void BinauralMonitor::primeAllSlots()
{
    for (auto& obj : obj_slots_) {
        for (auto& c : obj.conv_L) c.prepareForReload(hrtf::kOlaMaxIRLength, block_size_);
        for (auto& c : obj.conv_R) c.prepareForReload(hrtf::kOlaMaxIRLength, block_size_);
        obj.front_idx.store(0, std::memory_order_release);
        obj.crossfade_active.store(false, std::memory_order_release);
        obj.primed.store(false, std::memory_order_release);
        obj.ramp_new.reset(1.f);
        obj.ramp_old.reset(0.f);
    }
}

void BinauralMonitor::setDirection(int obj_id, float az_rad, float el_rad)
{
    if (!hrtf_loaded_) return;
    if (obj_id < 0 || obj_id >= MAX_OBJECTS) return;

    auto& obj = obj_slots_[static_cast<std::size_t>(obj_id)];

    // Look up nearest HRIR pair via KD-tree (O(log N), alloc-free).
    const hrtf::HrtfPair p =
        hrtf::lookupHrtfFromTree(table_, tree_, az_rad, el_rad);

    // Determine which slot to load (the one NOT currently fronted).
    const int front = obj.front_idx.load(std::memory_order_acquire);
    const int idle  = 1 - front;

    // Load new IR into the idle slot. Alloc-free post-priming.
    obj.conv_L[static_cast<std::size_t>(idle)].loadInto(p.left,  p.ir_length);
    obj.conv_R[static_cast<std::size_t>(idle)].loadInto(p.right, p.ir_length);

    const bool was_primed = obj.primed.load(std::memory_order_acquire);
    if (!was_primed) {
        // First-ever direction set: snap to full gain, no fade-in from silence
        // (preserves steady-state behavior expected by v0.4 single-source
        //  callsites; subsequent setDirection calls still get the 2-block
        //  crossfade).
        obj.ramp_new.reset(1.f);
        obj.ramp_old.reset(0.f);
        obj.front_idx.store(idle, std::memory_order_release);
        obj.crossfade_active.store(false, std::memory_order_release);
        obj.primed.store(true, std::memory_order_release);
        return;
    }

    // Crossfade setup.
    const int ramp_samples = 2 * block_size_;
    const bool was_fading = obj.crossfade_active.load(std::memory_order_acquire);
    if (was_fading) {
        // Preempt-with-current-gain handoff (A5):
        //   - The previously-fading-in slot becomes "old"; its current ramp_new
        //     gain becomes the starting gain of the new ramp_old.
        //   - The new incoming slot ramps up from 0.
        const float cur_in = obj.ramp_new.currentValue();
        obj.ramp_old.reset(cur_in);
    } else {
        // Steady-state old slot was at gain 1.0 — that's our starting point
        // for ramping it down.
        obj.ramp_old.reset(1.f);
    }
    obj.ramp_old.setTarget(0.f, ramp_samples);

    obj.ramp_new.reset(0.f);
    obj.ramp_new.setTarget(1.f, ramp_samples);

    // Promote the idle slot to front (release): audio thread sees the new
    // slot on its next acquire-load.
    obj.front_idx.store(idle, std::memory_order_release);
    obj.crossfade_active.store(true, std::memory_order_release);
}

void BinauralMonitor::processBlockForObject(int obj_id,
                                            const float* monoIn,
                                            int          numSamples,
                                            float*       leftOut,
                                            float*       rightOut)
{
    if (!leftOut || !rightOut) return;

    // Out-of-range obj_id → silence to be safe.
    if (obj_id < 0 || obj_id >= MAX_OBJECTS || !hrtf_loaded_) {
        std::memset(leftOut,  0, static_cast<std::size_t>(numSamples) * sizeof(float));
        std::memset(rightOut, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }

    auto& obj = obj_slots_[static_cast<std::size_t>(obj_id)];

    // Until the first setDirection() has loaded an IR, the slot's IR is
    // all zeros (post-prepareForReload). Emit silence so callers can rely
    // on a known "off" state.
    if (!obj.primed.load(std::memory_order_acquire)) {
        std::memset(leftOut,  0, static_cast<std::size_t>(numSamples) * sizeof(float));
        std::memset(rightOut, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }

    const int front = obj.front_idx.load(std::memory_order_acquire);
    const int back  = 1 - front;

    // Always run the front slot.
    obj.conv_L[static_cast<std::size_t>(front)].process(monoIn, numSamples, obj.tmp_new_L.data());
    obj.conv_R[static_cast<std::size_t>(front)].process(monoIn, numSamples, obj.tmp_new_R.data());

    const bool fading = obj.crossfade_active.load(std::memory_order_acquire);
    if (fading) {
        // Also run the back slot. NB: the back slot may have been the front
        // slot one direction-update ago; it remains valid until reload.
        obj.conv_L[static_cast<std::size_t>(back)].process(monoIn, numSamples, obj.tmp_old_L.data());
        obj.conv_R[static_cast<std::size_t>(back)].process(monoIn, numSamples, obj.tmp_old_R.data());

        for (int n = 0; n < numSamples; ++n) {
            const float gn = obj.ramp_new.next();
            const float go = obj.ramp_old.next();
            leftOut[n]  = gn * obj.tmp_new_L[static_cast<std::size_t>(n)]
                       + go * obj.tmp_old_L[static_cast<std::size_t>(n)];
            rightOut[n] = gn * obj.tmp_new_R[static_cast<std::size_t>(n)]
                       + go * obj.tmp_old_R[static_cast<std::size_t>(n)];
        }

        // When ramp_new reaches its target (gain 1.0) and ramp_old reaches 0,
        // the crossfade is over. ramp_new.isRamping() returns false post-completion.
        if (!obj.ramp_new.isRamping() && !obj.ramp_old.isRamping()) {
            obj.crossfade_active.store(false, std::memory_order_release);
        }
    } else {
        // Steady state — emit the front slot's output directly.
        std::memcpy(leftOut,  obj.tmp_new_L.data(),
                    static_cast<std::size_t>(numSamples) * sizeof(float));
        std::memcpy(rightOut, obj.tmp_new_R.data(),
                    static_cast<std::size_t>(numSamples) * sizeof(float));
    }
}

void BinauralMonitor::processBlock(const float* monoIn, int numSamples,
                                   float* leftOut, float* rightOut)
{
    if (!initialized_ || !hrtf_loaded_) {
        // Pass-through (v0.4 backwards-compat fallback).
        if (monoIn && leftOut)
            std::memcpy(leftOut,  monoIn,
                        static_cast<std::size_t>(numSamples) * sizeof(float));
        if (monoIn && rightOut)
            std::memcpy(rightOut, monoIn,
                        static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }
    processBlockForObject(0, monoIn, numSamples, leftOut, rightOut);
}

void BinauralMonitor::reset()
{
    for (auto& obj : obj_slots_) {
        for (auto& c : obj.conv_L) c.reset();
        for (auto& c : obj.conv_R) c.reset();
        obj.crossfade_active.store(false, std::memory_order_release);
        obj.ramp_new.reset(1.f);
        obj.ramp_old.reset(0.f);
    }
}

std::uint64_t BinauralMonitor::loadIntoFailures() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& obj : obj_slots_) {
        for (const auto& c : obj.conv_L) total += c.loadIntoFailures();
        for (const auto& c : obj.conv_R) total += c.loadIntoFailures();
    }
    for (const auto& c : vs_conv_L_) total += c.loadIntoFailures();
    for (const auto& c : vs_conv_R_) total += c.loadIntoFailures();
    return total;
}

// ─────────────────────────────────────────────────────────────────────────
// v0.5 P4 — B2 AmbiVS chain
// ─────────────────────────────────────────────────────────────────────────

void BinauralMonitor::initializeB2()
{
    if (!hrtf_loaded_) {
        b2_initialized_ = false;
        return;
    }

    // 1. Build vs_layout_ from kTDesign24. Each t-design Cartesian point
    //    (x, y, z) maps to a synthetic speaker with channel = i+1 ∈ [1..24].
    //    SpeakerLayout::kMaxYamlChannel = 64 ≥ 24, so channel_to_idx_ fits.
    vs_layout_.name       = "binaural_b2_tdesign24";
    vs_layout_.version    = "v0.5";
    vs_layout_.regularity = spe::geometry::Regularity::IRREGULAR;
    vs_layout_.speakers.clear();
    vs_layout_.speakers.reserve(kNumVirtualSpeakers);
    vs_layout_.channel_to_idx_.fill(-1);
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        const auto& pt = spe::ambi::kTDesign24[i];
        spe::geometry::Speaker s;
        s.channel = i + 1;
        s.x = pt.x;
        s.y = pt.y;
        s.z = pt.z;
        vs_layout_.speakers.push_back(s);
        vs_layout_.channel_to_idx_[static_cast<std::size_t>(i + 1)] =
            static_cast<int16_t>(i);
    }

    // 2. Prepare the AmbiDecoder for orders 1..3 against vs_layout_.
    vs_decoder_.prepare(vs_layout_);

    // 3. Per-VS HRIR cache + convolver priming. KD-tree lookup at each VS
    //    direction yields the nearest HRIR pair; we copy into the cache and
    //    prime+loadInto the convolver pair.
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        const auto& pt    = spe::ambi::kTDesign24[i];
        const float horiz = std::sqrt(pt.x * pt.x + pt.z * pt.z);
        const float az    = std::atan2(pt.x, pt.z);
        const float el    = std::atan2(pt.y, horiz);

        const hrtf::HrtfPair p =
            hrtf::lookupHrtfFromTree(table_, tree_, az, el);
        const int ir_len = std::min(p.ir_length, hrtf::kOlaMaxIRLength);
        vs_hrir_len_[static_cast<std::size_t>(i)] = ir_len;
        std::fill(vs_hrir_L_[static_cast<std::size_t>(i)].begin(),
                  vs_hrir_L_[static_cast<std::size_t>(i)].end(), 0.f);
        std::fill(vs_hrir_R_[static_cast<std::size_t>(i)].begin(),
                  vs_hrir_R_[static_cast<std::size_t>(i)].end(), 0.f);
        std::copy(p.left,  p.left  + ir_len,
                  vs_hrir_L_[static_cast<std::size_t>(i)].begin());
        std::copy(p.right, p.right + ir_len,
                  vs_hrir_R_[static_cast<std::size_t>(i)].begin());

        vs_conv_L_[static_cast<std::size_t>(i)].prepareForReload(
            hrtf::kOlaMaxIRLength, block_size_);
        vs_conv_R_[static_cast<std::size_t>(i)].prepareForReload(
            hrtf::kOlaMaxIRLength, block_size_);
        vs_conv_L_[static_cast<std::size_t>(i)].loadInto(
            vs_hrir_L_[static_cast<std::size_t>(i)].data(), ir_len);
        vs_conv_R_[static_cast<std::size_t>(i)].loadInto(
            vs_hrir_R_[static_cast<std::size_t>(i)].data(), ir_len);
    }

    b2_initialized_ = true;
}

void BinauralMonitor::setRequestedMode(BinauralMode m) noexcept
{
    const int mv = static_cast<int>(m);
    requested_mode_.store(mv, std::memory_order_release);

    // If the requested mode is Direct, the audio path always honours it.
    // If AmbiVS, we honour only when the probe has not flagged fallback.
    if (m == BinauralMode::Direct) {
        effective_mode_.store(mv, std::memory_order_release);
        probe_warning_set_.store(false, std::memory_order_release);
    } else {
        // Default to honouring AmbiVS; a subsequent runThroughputProbe()
        // may clamp this back to Direct when CPU headroom is insufficient.
        const bool warn = probe_warning_set_.load(std::memory_order_acquire);
        if (!warn && b2_initialized_) {
            effective_mode_.store(mv, std::memory_order_release);
        } else {
            effective_mode_.store(static_cast<int>(BinauralMode::Direct),
                                  std::memory_order_release);
        }
    }
}

float BinauralMonitor::runThroughputProbe()
{
    if (!b2_initialized_ || block_size_ <= 0) {
        probe_throughput_.store(0.f, std::memory_order_release);
        return 0.f;
    }

    // M1 fix (per code-review): use 24 DISTINCT sacrificial convolvers,
    // each with a slightly different decay, so the probe loop exercises the
    // same cache-eviction pressure as the production B2 fan-out (24 unique
    // 1024-tap IRs in flight). A single re-used convolver gave optimistic
    // throughput because the IR / overlap stayed L1-resident.
    constexpr int kProbeIRLen = 512;
    std::array<std::array<float, kProbeIRLen>, kNumVirtualSpeakers> probe_irs{};
    std::array<hrtf::OlaConvolver, kNumVirtualSpeakers>             probe_convs{};
    for (int vs = 0; vs < kNumVirtualSpeakers; ++vs) {
        const float tau = 30.f + static_cast<float>(vs) * 2.5f;  // 30..87.5
        probe_irs[static_cast<std::size_t>(vs)][0] = 1.f;
        for (int i = 1; i < kProbeIRLen; ++i)
            probe_irs[static_cast<std::size_t>(vs)][static_cast<std::size_t>(i)] =
                std::exp(-static_cast<float>(i) / tau);
        probe_convs[static_cast<std::size_t>(vs)].prepareForReload(
            hrtf::kOlaMaxIRLength, block_size_);
        probe_convs[static_cast<std::size_t>(vs)].loadInto(
            probe_irs[static_cast<std::size_t>(vs)].data(), kProbeIRLen);
    }

    std::array<float, MAX_BLOCK> input{};
    for (int i = 0; i < block_size_; ++i)
        input[static_cast<std::size_t>(i)] =
            static_cast<float>((i * 17) % 31) * 0.01f;
    std::array<float, MAX_BLOCK> output{};

    constexpr int kProbeBlocks = 256;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < kProbeBlocks; ++b) {
        for (int vs = 0; vs < kNumVirtualSpeakers; ++vs) {
            probe_convs[static_cast<std::size_t>(vs)].process(
                input.data(), block_size_, output.data());
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double simulated_sec =
        static_cast<double>(kProbeBlocks * block_size_) /
        static_cast<double>(sample_rate_);
    const float throughput = (elapsed > 1e-9)
        ? static_cast<float>(simulated_sec / elapsed)
        : 0.f;

    probe_throughput_.store(throughput, std::memory_order_release);

    if (throughput < kMinB2Throughput) {
        probe_warning_set_.store(true, std::memory_order_release);
        effective_mode_.store(static_cast<int>(BinauralMode::Direct),
                              std::memory_order_release);
    } else {
        probe_warning_set_.store(false, std::memory_order_release);
        // Promote to requested mode if user wanted AmbiVS.
        if (requested_mode_.load(std::memory_order_acquire)
            == static_cast<int>(BinauralMode::AmbiVS)) {
            effective_mode_.store(static_cast<int>(BinauralMode::AmbiVS),
                                  std::memory_order_release);
        }
    }
    return throughput;
}

void BinauralMonitor::injectProbeThroughputForTest(float throughput_rt) noexcept
{
    probe_throughput_.store(throughput_rt, std::memory_order_release);
    if (throughput_rt < kMinB2Throughput) {
        probe_warning_set_.store(true, std::memory_order_release);
        effective_mode_.store(static_cast<int>(BinauralMode::Direct),
                              std::memory_order_release);
    } else {
        probe_warning_set_.store(false, std::memory_order_release);
        if (requested_mode_.load(std::memory_order_acquire)
            == static_cast<int>(BinauralMode::AmbiVS)
            && b2_initialized_) {
            effective_mode_.store(static_cast<int>(BinauralMode::AmbiVS),
                                  std::memory_order_release);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// v0.6 #5 — runtime sticky-underrun auto-demote.
// ─────────────────────────────────────────────────────────────────────────

void BinauralMonitor::recordB2BlockTiming(int block_size,
                                          float sample_rate,
                                          long long elapsed_ns) noexcept
{
    if (block_size <= 0 || sample_rate <= 0.f) return;
    // Already demoted — no further bookkeeping needed (avoid pointless
    // atomic traffic on the audio thread once the decision is sticky).
    if (runtime_demoted_.load(std::memory_order_acquire)) return;

    // Block deadline in nanoseconds.
    const long long deadline_ns = static_cast<long long>(
        (static_cast<double>(block_size) /
         static_cast<double>(sample_rate)) * 1e9);
    const long long over_budget_ns = static_cast<long long>(
        static_cast<double>(deadline_ns) *
        static_cast<double>(kRuntimeDemoteBudgetFraction));

    int strikes;
    if (elapsed_ns >= over_budget_ns) {
        strikes = runtime_demote_strikes_.fetch_add(1,
            std::memory_order_acq_rel) + 1;
    } else {
        runtime_demote_strikes_.store(0, std::memory_order_release);
        return;
    }

    if (strikes >= kRuntimeDemoteStrikes) {
        // Sticky demote. Edge-trigger the warning latch via CAS so multiple
        // entrants (audio thread plus theoretical test reseed) can race
        // safely — only the first writer flips the latch.
        bool expected = false;
        if (runtime_demoted_.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel))
        {
            runtime_demote_warning_pending_.store(true,
                std::memory_order_release);
            effective_mode_.store(static_cast<int>(BinauralMode::Direct),
                                  std::memory_order_release);
        }
    }
}

void BinauralMonitor::injectRuntimeUnderrunStrikesForTest() noexcept
{
    runtime_demote_strikes_.store(kRuntimeDemoteStrikes - 1,
                                  std::memory_order_release);
}

void BinauralMonitor::clearRuntimeDemoteForTest() noexcept
{
    runtime_demote_strikes_.store(0, std::memory_order_release);
    runtime_demoted_.store(false, std::memory_order_release);
    runtime_demote_warning_pending_.store(false, std::memory_order_release);
}

// v0.6 D-M2 — time kSteadyClockProbeSamples calls to steady_clock::now()
// and demote the platform to "slow" if the average per-call exceeds
// kSteadyClockFastThresholdNs. Called from initialize() on the control
// thread; the probe itself is not RT-safe (the whole point is to find out
// whether RT-safety holds on this platform).
//
// Sample rationale: 10 000 calls × ~30 ns = 300 µs on a fast vDSO host;
// 10 000 × ~500 ns = 5 ms on a syscall-fallback host. Both are negligible
// at prepareToPlay time. The 200 ns threshold sits roughly halfway between
// known vDSO ranges (20-60 ns on Linux x86_64, 60-120 ns on macOS arm64
// commpage) and known syscall costs (500-5000 ns), so it's stable against
// micro-variations on either side.
void BinauralMonitor::probeSteadyClockSpeed() noexcept
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    // We deliberately throw away the per-call result to prevent the
    // compiler from optimising the loop body away. The sink atomic
    // forces a release-acquire pair on every iteration.
    static std::atomic<long long> sink{0};
    for (int i = 0; i < kSteadyClockProbeSamples; ++i) {
        const auto sample = clock::now().time_since_epoch().count();
        sink.store(sample, std::memory_order_relaxed);
    }
    const auto t1 = clock::now();
    const long long total_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    const long long avg_ns = total_ns / kSteadyClockProbeSamples;
    const bool fast = (avg_ns < kSteadyClockFastThresholdNs);
    steady_clock_fast_.store(fast, std::memory_order_release);
    if (!fast) {
        // Arm the one-shot warning latch so the IO thread emits
        // /sys/binaural_warning ,s "rt_timing_unavailable" exactly once.
        rt_timing_unavailable_pending_.store(true, std::memory_order_release);
    }
}

void BinauralMonitor::injectSteadyClockSlowForTest() noexcept
{
    steady_clock_fast_.store(false, std::memory_order_release);
    rt_timing_unavailable_pending_.store(true, std::memory_order_release);
}

int BinauralMonitor::b2HrirLength(int vs_idx) const noexcept
{
    if (vs_idx < 0 || vs_idx >= kNumVirtualSpeakers || !b2_initialized_)
        return -1;
    return vs_hrir_len_[static_cast<std::size_t>(vs_idx)];
}

const float* BinauralMonitor::b2HrirLeft(int vs_idx) const noexcept
{
    if (vs_idx < 0 || vs_idx >= kNumVirtualSpeakers || !b2_initialized_)
        return nullptr;
    return vs_hrir_L_[static_cast<std::size_t>(vs_idx)].data();
}

const float* BinauralMonitor::b2HrirRight(int vs_idx) const noexcept
{
    if (vs_idx < 0 || vs_idx >= kNumVirtualSpeakers || !b2_initialized_)
        return nullptr;
    return vs_hrir_R_[static_cast<std::size_t>(vs_idx)].data();
}

// ─────────────────────────────────────────────────────────────────────────
// v0.5.1 Q2 — A3 mode-transition crossfade.
// ─────────────────────────────────────────────────────────────────────────

void BinauralMonitor::buildXfadeEnvelopes() noexcept
{
    // Defensive: zero-fill the envelope arrays so unused tail samples (beyond
    // total_blocks * block_size_) remain at 0 even if the caller indexes
    // past the live ramp window.
    xfade_inc_env_1_.fill(0.f);
    xfade_out_env_1_.fill(0.f);
    xfade_inc_env_2_.fill(0.f);
    xfade_out_env_2_.fill(0.f);

    const int bs = block_size_;
    if (bs <= 0 || bs > MAX_BLOCK) {
        xfade_envelopes_built_ = false;
        return;
    }

    // total_blocks = 1: linear ramp 0 → 1 over bs samples.
    // Convention: env[n] = (n + 1) / N where N = total_samples. This avoids
    // the degenerate env[0] = 0 case (which would zero out the first block's
    // incoming branch sample) while still summing to 1 with the outgoing
    // complement (env_out[n] = 1 - env_in[n]).
    {
        const int N = bs;
        for (int n = 0; n < N; ++n) {
            const float inc = static_cast<float>(n + 1) / static_cast<float>(N);
            xfade_inc_env_1_[static_cast<std::size_t>(n)] = inc;
            xfade_out_env_1_[static_cast<std::size_t>(n)] = 1.f - inc;
        }
    }
    // total_blocks = 2: linear ramp across 2 * bs samples.
    {
        const int N = 2 * bs;
        for (int n = 0; n < N; ++n) {
            const float inc = static_cast<float>(n + 1) / static_cast<float>(N);
            xfade_inc_env_2_[static_cast<std::size_t>(n)] = inc;
            xfade_out_env_2_[static_cast<std::size_t>(n)] = 1.f - inc;
        }
    }

    xfade_envelopes_built_ = true;
}

BinauralMonitor::XfadeStep BinauralMonitor::observeAndArmXfade() noexcept
{
    XfadeStep step{};
    const int effective = effective_mode_.load(std::memory_order_acquire);

    if (xfade_blocks_remaining_ == 0 && effective != prev_effective_mode_) {
        // Arm a new ramp. Truncate to 1 block when the probe has surfaced a
        // CPU-warning condition (saves dual-branch render cost at the price
        // of a residual click — surfaced via xfade_truncated_pending_).
        const bool warn =
            probe_warning_set_.load(std::memory_order_acquire);
        const int total = warn ? 1 : kXfadeBlocksDefault;
        outgoing_mode_           = prev_effective_mode_;
        incoming_mode_           = effective;
        xfade_total_blocks_      = total;
        xfade_blocks_remaining_  = total;
        xfade_blocks_remaining_atomic_.store(total, std::memory_order_release);
        if (total == 1) {
            xfade_truncated_pending_.store(true, std::memory_order_release);
        }
    }

    if (xfade_blocks_remaining_ > 0) {
        step.active           = true;
        step.outgoing         = static_cast<BinauralMode>(outgoing_mode_);
        step.incoming         = static_cast<BinauralMode>(incoming_mode_);
        step.steady           = step.incoming;
        step.total_blocks     = xfade_total_blocks_;
        step.blocks_remaining = xfade_blocks_remaining_;
        step.block_index      = xfade_total_blocks_ - xfade_blocks_remaining_;
    } else {
        step.active           = false;
        step.outgoing         = static_cast<BinauralMode>(prev_effective_mode_);
        step.incoming         = static_cast<BinauralMode>(prev_effective_mode_);
        step.steady           = step.outgoing;
        step.total_blocks     = 0;
        step.blocks_remaining = 0;
        step.block_index      = 0;
    }
    return step;
}

void BinauralMonitor::finalizeXfadeBlock() noexcept
{
    if (xfade_blocks_remaining_ > 0) {
        --xfade_blocks_remaining_;
        xfade_blocks_remaining_atomic_.store(xfade_blocks_remaining_,
                                             std::memory_order_release);
        if (xfade_blocks_remaining_ == 0) {
            prev_effective_mode_ = incoming_mode_;
        }
    }
}

const float* BinauralMonitor::xfadeIncomingEnvelope(int total_blocks) const noexcept
{
    if (!xfade_envelopes_built_) return nullptr;
    if (total_blocks == 1) return xfade_inc_env_1_.data();
    if (total_blocks == kXfadeBlocksDefault) return xfade_inc_env_2_.data();
    return nullptr;
}

const float* BinauralMonitor::xfadeOutgoingEnvelope(int total_blocks) const noexcept
{
    if (!xfade_envelopes_built_) return nullptr;
    if (total_blocks == 1) return xfade_out_env_1_.data();
    if (total_blocks == kXfadeBlocksDefault) return xfade_out_env_2_.data();
    return nullptr;
}

void BinauralMonitor::processBlockB2(const float* const* sh_planar,
                                     int                 order,
                                     int                 num_samples,
                                     float*              leftOut,
                                     float*              rightOut)
{
    if (!leftOut || !rightOut) return;
    const std::size_t out_bytes =
        static_cast<std::size_t>(num_samples) * sizeof(float);

    // Structural validity only; mode gate lives in the caller (see header
    // contract — SpatialEngine snapshots effectiveMode() once per block).
    if (!b2_initialized_
        || num_samples <= 0
        || num_samples > MAX_BLOCK
        || sh_planar == nullptr) {
        std::memset(leftOut,  0, out_bytes);
        std::memset(rightOut, 0, out_bytes);
        return;
    }
    // Defense-in-depth (L2): per-channel pointer null check for K=(order+1)².
    const int K_check = (order < 1 ? 1 : (order > 3 ? 3 : order));
    const int K_n     = (K_check + 1) * (K_check + 1);
    for (int k = 0; k < K_n; ++k) {
        if (!sh_planar[k]) {
            std::memset(leftOut,  0, out_bytes);
            std::memset(rightOut, 0, out_bytes);
            return;
        }
    }

    // Clamp order to {1, 2, 3}.
    if (order < 1) order = 1;
    if (order > 3) order = 3;

    // 1. Decode SH → 24 VS interleaved.
    vs_decoder_.decode(order, sh_planar, num_samples,
                       vs_decode_scratch_.data());

    // 2. De-interleave to vs_buf_[i][n].
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        float* dst = vs_buf_[static_cast<std::size_t>(i)].data();
        for (int n = 0; n < num_samples; ++n) {
            dst[n] = vs_decode_scratch_[
                static_cast<std::size_t>(n) * kNumVirtualSpeakers +
                static_cast<std::size_t>(i)];
        }
    }

    // 3. Convolve each VS through its HRIR pair, sum to leftOut/rightOut.
    std::memset(leftOut,  0, out_bytes);
    std::memset(rightOut, 0, out_bytes);
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        vs_conv_L_[static_cast<std::size_t>(i)].process(
            vs_buf_[static_cast<std::size_t>(i)].data(),
            num_samples, vs_conv_L_scratch_.data());
        vs_conv_R_[static_cast<std::size_t>(i)].process(
            vs_buf_[static_cast<std::size_t>(i)].data(),
            num_samples, vs_conv_R_scratch_.data());
        for (int n = 0; n < num_samples; ++n) {
            leftOut[n]  += vs_conv_L_scratch_[static_cast<std::size_t>(n)];
            rightOut[n] += vs_conv_R_scratch_[static_cast<std::size_t>(n)];
        }
    }
}

} // namespace spe::output

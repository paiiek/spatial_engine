// core/src/output_backend/BinauralMonitor.cpp

#include "output_backend/BinauralMonitor.h"
#include "ambi/AllRADTDesigns.hpp"
#include "hrtf/HrtfLookup.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

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

    // v0.9 Lane B (B-M2) — seed the ACTIVE HrtfLookup slot (slot 0). loadSpeh
    // + KD-tree build happen on the control thread here (allocates OK).
    hrtf::SpehResult res = hrtf_.loadIntoActive(cfg.sofaPath, cfg.sampleRate);
    switch (res) {
    case hrtf::SpehResult::Ok:               break;
    case hrtf::SpehResult::FileNotFound:     return InitResult::SofaNotFound;
    case hrtf::SpehResult::SampleRateMismatch: return InitResult::SofaSampleRateMismatch;
    case hrtf::SpehResult::IRLengthUnsupported: return InitResult::SofaIRLengthUnsupported;
    default:                                 return InitResult::SofaInvalidFormat;
    }

    hrtf_loaded_ = true;
    applied_sofa_path_ = cfg.sofaPath;
    pending_sofa_path_ = cfg.sofaPath;

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

    // v0.9 Lane B (B-M2) — look up against the ACTIVE SOFA slot. One
    // acquire-load of the slot index, then read that slot's table/tree (mirror
    // AmbiDecoder::decode() at AmbiDecoder.cpp:241). On a runtime swap this is
    // how B1 self-heals: the next block re-looks-up against the new active slot
    // and reloads through the existing 2-block crossfade.
    const int slot = hrtf_.activeSlot();
    // Record the table slot consumed this block (audio-thread-only local;
    // published by finalizeXfadeBlock() for the control tick's quiescence
    // handshake). setDirection() may run once per object per block — they all
    // read the same active slot, so the last write correctly reflects the slot
    // in use this block.
    last_consumed_table_slot_local_ = slot;
    const hrtf::HrtfPair p = hrtf::lookupHrtfFromTree(
        hrtf_.tableForSlot(slot), hrtf_.treeForSlot(slot), az_rad, el_rad);

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
    for (const auto& slot : vs_conv_L_)
        for (const auto& c : slot) total += c.loadIntoFailures();
    for (const auto& slot : vs_conv_R_)
        for (const auto& c : slot) total += c.loadIntoFailures();
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
    //    SpeakerLayout::kMaxYamlChannel (≥ 64) ≥ 24, so channel_to_idx_ fits.
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

    // 3. Prime ALL 96 VS convolvers (2 slots × 24 × L/R) to worst-case capacity
    //    (allocates; control thread). loadInto() into them is then alloc-free
    //    for any later swap.
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < kNumVirtualSpeakers; ++i) {
            vs_conv_L_[static_cast<std::size_t>(s)][static_cast<std::size_t>(i)]
                .prepareForReload(hrtf::kOlaMaxIRLength, block_size_);
            vs_conv_R_[static_cast<std::size_t>(s)][static_cast<std::size_t>(i)]
                .prepareForReload(hrtf::kOlaMaxIRLength, block_size_);
        }
    }

    b2_initialized_ = true;
    active_vs_slot_.store(0, std::memory_order_release);

    // 4. Populate slot 0's per-VS HRIR cache + convolvers from the active tree.
    //    fillVsSlotFromActiveTree writes a SPECIFIC slot (no publish); we fill
    //    the active slot (0) directly at init since nothing is reading yet.
    fillVsSlotFromActiveTree(0);
}

void BinauralMonitor::fillVsSlotFromActiveTree(int dst_slot)
{
    const std::size_t s = static_cast<std::size_t>(dst_slot);
    const int slot = hrtf_.activeSlot();
    const hrtf::HrtfTable& table = hrtf_.tableForSlot(slot);
    const hrtf::KdTree3D&  tree  = hrtf_.treeForSlot(slot);

    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        const std::size_t vi = static_cast<std::size_t>(i);
        const auto& pt    = spe::ambi::kTDesign24[i];
        const float horiz = std::sqrt(pt.x * pt.x + pt.z * pt.z);
        const float az    = std::atan2(pt.x, pt.z);
        const float el    = std::atan2(pt.y, horiz);

        const hrtf::HrtfPair p =
            hrtf::lookupHrtfFromTree(table, tree, az, el);
        const int ir_len = std::min(p.ir_length, hrtf::kOlaMaxIRLength);
        vs_hrir_len_[s][vi] = ir_len;
        std::fill(vs_hrir_L_[s][vi].begin(), vs_hrir_L_[s][vi].end(), 0.f);
        std::fill(vs_hrir_R_[s][vi].begin(), vs_hrir_R_[s][vi].end(), 0.f);
        std::copy(p.left,  p.left  + ir_len, vs_hrir_L_[s][vi].begin());
        std::copy(p.right, p.right + ir_len, vs_hrir_R_[s][vi].begin());

        vs_conv_L_[s][vi].loadInto(vs_hrir_L_[s][vi].data(), ir_len);
        vs_conv_R_[s][vi].loadInto(vs_hrir_R_[s][vi].data(), ir_len);
    }
}

void BinauralMonitor::rebuildB2FromActiveTree()
{
    if (!b2_initialized_) return;

    // v0.9 Lane B (B-M2) — DOUBLE-BUFFER publish (mirrors AmbiDecoder /
    // obj_slots_.front_idx). Build the INACTIVE VS slot from the new active
    // SOFA tree, then store-release publish it via active_vs_slot_. The audio
    // thread acquire-loads active_vs_slot_ once per block in processBlockB2()
    // and reads only that slot's convolvers. The caller (loadPendingSofa) has
    // already called waitInactiveSlotQuiescent(), so no in-flight audio block
    // is still holding the inactive slot index as its last-consumed slot.
    const int inactive = 1 - active_vs_slot_.load(std::memory_order_relaxed);
    fillVsSlotFromActiveTree(inactive);
    active_vs_slot_.store(inactive, std::memory_order_release);
}

void BinauralMonitor::waitInactiveSlotQuiescent() noexcept
{
    // EXPLICIT QUIESCENCE HANDSHAKE (AmbiDecoder.h:16-25 robust fix). We are
    // about to overwrite the INACTIVE table slot (1 - active_sofa_slot_) and the
    // INACTIVE VS slot (1 - active_vs_slot_). Wait until the audio thread's
    // last-consumed slot is NO LONGER either inactive slot — i.e. every in-flight
    // reader has moved on to the active slot — AND the per-block tick has
    // advanced at least once (so a stale "never consumed" reading can't pass
    // vacuously). The acquire-loads pair with finalizeXfadeBlock()'s release
    // stores, establishing the happens-before that makes the overwrite race-free.
    //
    // Bounded spin: if the tick never advances, no audio thread is consuming
    // slots (headless unit test / stopped engine — finalizeXfadeBlock() is never
    // called), so the inactive slots are trivially quiescent and we proceed.
    const int inactive_table = 1 - hrtf_.activeSlot();
    const int inactive_vs    = 1 - active_vs_slot_.load(std::memory_order_acquire);
    const std::uint64_t start_tick =
        audio_block_tick_.load(std::memory_order_acquire);

    constexpr int kCadenceSpinBudget = 10000000;
    for (int spin = 0; spin < kCadenceSpinBudget; ++spin) {
        const std::uint64_t tick =
            audio_block_tick_.load(std::memory_order_acquire);
        if (tick == start_tick) {
            // No audio block has finished yet — yield the CPU so the audio
            // thread (if it exists) can run. This is the control thread only,
            // so yielding is safe. On a stopped/headless engine the tick never
            // advances and we exit via the budget bound above.
            std::this_thread::yield();
            continue;
        }
        const int lc_table = last_consumed_table_slot_.load(std::memory_order_acquire);
        const int lc_vs     = last_consumed_vs_slot_.load(std::memory_order_acquire);
        // Quiescent once the most-recently-finished block consumed neither
        // inactive slot. (-1 = never consumed that buffer ⇒ trivially clear.)
        const bool table_clear = (lc_table != inactive_table);
        const bool vs_clear     = (lc_vs    != inactive_vs);
        if (table_clear && vs_clear)
            break;
        std::this_thread::yield();
    }
}

// ─────────────────────────────────────────────────────────────────────────
// v0.9 Lane B (B-M2) — runtime SOFA hot-swap (CONTROL THREAD ONLY).
// ─────────────────────────────────────────────────────────────────────────

bool BinauralMonitor::loadPendingSofa(const std::string& path)
{
    // CONTROL THREAD ONLY — ALLOCATES.
    // ≥1-block CADENCE GUARD before overwriting either inactive slot (table or
    // VS bank). On a 2nd+ consecutive swap the inactive slot was the ACTIVE slot
    // one publish ago; an audio block may still hold its index from its last
    // snapshot. Wait until any such in-flight reader has finished (upholds the
    // 2-slot double-buffer invariant — HrtfLookup.h / AmbiDecoder.h:16-25).
    waitInactiveSlotQuiescent();

    // 1. Load + build into the INACTIVE HrtfLookup slot. loadSpeh enforces the
    //    failure contract: IRLengthUnsupported when ir_length > kOlaMaxIRLength,
    //    plus FileNotFound / SampleRateMismatch / format errors.
    const hrtf::SpehResult res = hrtf_.loadIntoInactive(path, sample_rate_);
    if (res != hrtf::SpehResult::Ok) {
        // FAILURE CONTRACT: active slot UNTOUCHED, old SOFA stays live, no
        // publish. Arm the one-shot warning latch with the reason code.
        sofa_load_failed_reason_ = static_cast<int>(res);
        sofa_load_failed_pending_.store(true, std::memory_order_release);
        return false;
    }

    // 2. Publish the freshly-built inactive slot (store-release). The audio
    //    thread's next setDirection() acquire-load sees the new active table/
    //    tree and B1 self-heals via the 2-block crossfade. No per-object
    //    convolver is touched here (that would race the audio-thread write).
    hrtf_.publish();

    // 3. Build the inactive B2 VS slot from the new active tree and
    //    store-release publish active_vs_slot_. No-op when B2 was never
    //    initialised. The inactive slot was already guarded quiescent by
    //    waitInactiveSlotQuiescent() above, so no in-flight reader holds it.
    rebuildB2FromActiveTree();

    // 4. Reset runtime-demote state (fresh measurement environment) via the
    //    existing clear path — but do NOT reset the 60 s user-reset cooldown
    //    clock (runtime_demote_last_reset_ns_), to prevent swap-spam from
    //    bypassing the cooldown.
    clearRuntimeDemoteForTest();

    applied_sofa_path_ = path;
    return true;
}

bool BinauralMonitor::applyPendingSofaChange()
{
    // CONTROL THREAD ONLY — mirrors AmbisonicRenderer::applyPendingDecoderTypeChange().
    if (!hrtf_loaded_) return false;
    if (pending_sofa_path_ == applied_sofa_path_) return false;
    return loadPendingSofa(pending_sofa_path_);
}

const char* BinauralMonitor::sofaLoadFailureReason() const noexcept
{
    switch (static_cast<hrtf::SpehResult>(sofa_load_failed_reason_)) {
    case hrtf::SpehResult::FileNotFound:        return "file_not_found";
    case hrtf::SpehResult::SampleRateMismatch:  return "sample_rate_mismatch";
    case hrtf::SpehResult::IRLengthUnsupported: return "ir_length_unsupported";
    case hrtf::SpehResult::InvalidMagic:        return "invalid_format";
    case hrtf::SpehResult::TruncatedFile:       return "invalid_format";
    default:                                    return "";
    }
}

int BinauralMonitor::activeSofaIrLengthForTest() const noexcept
{
    if (!hrtf_loaded_) return 0;
    return static_cast<int>(hrtf_.activeTable().ir_length);
}

void BinauralMonitor::activeSofaOnsetForTest(float az_rad, float el_rad,
                                             int& onsetL, int& onsetR) const noexcept
{
    onsetL = -1;
    onsetR = -1;
    if (!hrtf_loaded_) return;
    const int slot = hrtf_.activeSlot();
    const hrtf::HrtfPair p = hrtf::lookupHrtfFromTree(
        hrtf_.tableForSlot(slot), hrtf_.treeForSlot(slot), az_rad, el_rad);
    float peakL = -1.f, peakR = -1.f;
    for (int n = 0; n < p.ir_length; ++n) {
        const float aL = std::fabs(p.left[n]);
        const float aR = std::fabs(p.right[n]);
        if (aL > peakL) { peakL = aL; onsetL = n; }
        if (aR > peakR) { peakR = aR; onsetR = n; }
    }
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

    // ── D-S2 (v0.7) — block-size-aware effective_strikes ──────────────────
    // AN-2 rationale: kRuntimeDemoteStrikes = 8 was calibrated for
    // 48 kHz / 128-sample blocks (= 2.67 ms/block). At 32-sample blocks
    // (Logic Pro low-latency) 8 strikes = 5.3 ms — a page fault looks like a
    // demote trigger. At 1024-sample blocks 8 strikes = 170 ms — the user
    // hears 3-4 dropouts before demote fires. Fix: pin the strike window to
    // ~20 ms regardless of block size: effective_strikes = max(8, ceil(0.02s /
    // block_seconds)). kRuntimeDemoteStrikes becomes the FLOOR (preserves
    // v0.6 behavior at 48 kHz/128).
    const double block_seconds = static_cast<double>(block_size)
                                 / static_cast<double>(sample_rate);
    const int effective_strikes = std::max(
        kRuntimeDemoteStrikes,
        static_cast<int>(std::ceil(0.020 / block_seconds)));

    // Block deadline in nanoseconds.
    const long long deadline_ns = static_cast<long long>(
        (static_cast<double>(block_size) /
         static_cast<double>(sample_rate)) * 1e9);
    const long long over_budget_ns = static_cast<long long>(
        static_cast<double>(deadline_ns) *
        static_cast<double>(kRuntimeDemoteBudgetFraction));

    // v0.7 D-S3 — AM-2 relaxed-load-then-store pattern (NOT CAS).
    // Critic §A.2 correction over Architect AM-2: the strictly correct
    // pattern is read-then-store, NOT a single store(max(...)) which is
    // two non-atomic operations evaluated together non-atomically.
    // Single-producer invariant: recordB2BlockTiming() is called exclusively
    // from SpatialEngine::audioBlock() (verified per v0.6 retro §A.2), so no
    // concurrent writer races here. CAS promotion deferred to v0.8 conditional
    // on telemetry-informed precision need.
    const int ratio_x1000 = static_cast<int>(
        (static_cast<double>(elapsed_ns) /
         static_cast<double>(deadline_ns)) * 1000.0);

    int strikes;
    if (elapsed_ns >= over_budget_ns) {
        const int cur_max = runtime_demote_max_ratio_x1000_.load(
            std::memory_order_relaxed);
        if (ratio_x1000 > cur_max) {
            runtime_demote_max_ratio_x1000_.store(ratio_x1000,
                std::memory_order_relaxed);
        }
        // ── Item #8 (v0.7) — saturation cap ──────────────────────────────
        // Belt-and-suspenders: if the demote latch CAS somehow never fires
        // (should not happen in correct runs), the counter would accumulate
        // indefinitely. Cap at kRuntimeDemoteStrikesSaturationCeiling so it
        // never wraps. Single-producer invariant (audio thread only) means
        // load+if+store is race-free here.
        const int cur = runtime_demote_strikes_.load(std::memory_order_acquire);
        if (cur >= kRuntimeDemoteStrikesSaturationCeiling) {
            return; // already saturated; demote latch should have fired
        }
        strikes = runtime_demote_strikes_.fetch_add(1,
            std::memory_order_acq_rel) + 1;
    } else {
        // Good block: reset strike counter and also reset the in-progress
        // max-ratio accumulator (the demote didn't fire; start fresh).
        runtime_demote_strikes_.store(0, std::memory_order_release);
        runtime_demote_max_ratio_x1000_.store(0, std::memory_order_release);
        return;
    }

    if (strikes >= effective_strikes) {
        // Sticky demote. Edge-trigger the warning latch via CAS so multiple
        // entrants (audio thread plus theoretical test reseed) can race
        // safely — only the first writer flips the latch.
        bool expected = false;
        if (runtime_demoted_.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel))
        {
            // v0.7 D-S3 iter-3 §C.2 — snapshot demote-moment context together
            // with release ordering so the IO-thread drain reads a consistent
            // triple. The snapshot captures the audio thread's current values
            // at the exact demote latch firing — even if prepareToPlay()
            // re-inits block_size_/sample_rate_ later, this snapshot is the
            // authoritative demote-moment record.
            runtime_demote_max_ratio_at_event_x1000_.store(
                runtime_demote_max_ratio_x1000_.load(std::memory_order_relaxed),
                std::memory_order_release);
            runtime_demote_block_size_at_event_.store(
                block_size_, std::memory_order_release);
            runtime_demote_sample_rate_at_event_.store(
                static_cast<int>(sample_rate_), std::memory_order_release);

            runtime_demote_warning_pending_.store(true,
                std::memory_order_release);
            effective_mode_.store(static_cast<int>(BinauralMode::Direct),
                                  std::memory_order_release);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// v0.7 D-S1 — user-controlled reset hatch
// ─────────────────────────────────────────────────────────────────────────

BinauralMonitor::ResetResult
BinauralMonitor::resetRuntimeDemoteFromUser(int64_t now_ns) noexcept
{
    // Guard: if not currently demoted, nothing to reset.
    if (!runtime_demoted_.load(std::memory_order_acquire)) {
        return ResetResult::NotDemoted;
    }

    // Cooldown check: compare against last-reset timestamp.
    const int64_t last = runtime_demote_last_reset_ns_.load(
                             std::memory_order_acquire);
    // INT64_MIN means "never reset" — always accept. Otherwise check elapsed.
    if (last != INT64_MIN) {
        const int64_t elapsed = now_ns - last;
        if (elapsed < kResetDemoteCooldownNs) {
            reset_rejected_count_.fetch_add(1, std::memory_order_relaxed);
            // Rate-limit warning to at most once per cooldown window (Critic §D.7).
            bool already_emitted = reset_cooldown_warning_emitted_.load(
                                       std::memory_order_acquire);
            if (!already_emitted) {
                reset_cooldown_warning_emitted_.store(true,
                    std::memory_order_release);
                reset_demote_cooldown_pending_.store(true,
                    std::memory_order_release);
            }
            return ResetResult::CooldownActive;
        }
    }

    // ── AM-1-extended 8-atomic reset (iter-3 §C.2 ordering) ──
    //
    // Order rationale (Critic §C.3 + AM-1):
    //   1. Clear D-S3 telemetry atomics first (Item #3 snapshot slots).
    //   2. Clear v0.6 #5 sticky state: strikes → warning_pending → demoted.
    //      runtime_demoted_ is cleared LAST so the audio thread cannot
    //      race-observe (demoted_=false AND strikes_>=threshold) simultaneously.
    //   3. Snapshot cooldown timestamp + clear warning-rate-limit flag.
    //   4. Arm accepted-warning latch.

    // Step 1 — D-S3 telemetry atomics (4 atomics, Item #3 declares write sites)
    runtime_demote_max_ratio_x1000_.store(0, std::memory_order_release);
    runtime_demote_max_ratio_at_event_x1000_.store(0, std::memory_order_release);
    runtime_demote_block_size_at_event_.store(0, std::memory_order_release);
    runtime_demote_sample_rate_at_event_.store(0, std::memory_order_release);

    // Step 2 — v0.6 #5 sticky state (strikes first, then warning_pending, demoted last)
    runtime_demote_strikes_.store(0, std::memory_order_release);
    runtime_demote_warning_pending_.store(false, std::memory_order_release);
    runtime_demoted_.store(false, std::memory_order_release);  // LAST

    // Step 3 — cooldown snapshot + rate-limit flag reset
    runtime_demote_last_reset_ns_.store(now_ns, std::memory_order_release);
    reset_cooldown_warning_emitted_.store(false, std::memory_order_release);

    // Step 4 — arm accepted-warning latch for IO heartbeat drain
    reset_demote_accepted_pending_.store(true, std::memory_order_release);

    return ResetResult::Accepted;
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
    // v0.7 D-S3 — also clear the snapshot atomics so tests can verify
    // they are reset to 0 after a simulated D-S1 reset.
    runtime_demote_max_ratio_x1000_.store(0, std::memory_order_release);
    runtime_demote_max_ratio_at_event_x1000_.store(0, std::memory_order_release);
    runtime_demote_block_size_at_event_.store(0, std::memory_order_release);
    runtime_demote_sample_rate_at_event_.store(0, std::memory_order_release);
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
    const std::size_t s =
        static_cast<std::size_t>(active_vs_slot_.load(std::memory_order_acquire));
    return vs_hrir_len_[s][static_cast<std::size_t>(vs_idx)];
}

const float* BinauralMonitor::b2HrirLeft(int vs_idx) const noexcept
{
    if (vs_idx < 0 || vs_idx >= kNumVirtualSpeakers || !b2_initialized_)
        return nullptr;
    const std::size_t s =
        static_cast<std::size_t>(active_vs_slot_.load(std::memory_order_acquire));
    return vs_hrir_L_[s][static_cast<std::size_t>(vs_idx)].data();
}

const float* BinauralMonitor::b2HrirRight(int vs_idx) const noexcept
{
    if (vs_idx < 0 || vs_idx >= kNumVirtualSpeakers || !b2_initialized_)
        return nullptr;
    const std::size_t s =
        static_cast<std::size_t>(active_vs_slot_.load(std::memory_order_acquire));
    return vs_hrir_R_[s][static_cast<std::size_t>(vs_idx)].data();
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
    // C1 single per-block snapshot of effective_mode_ (one acquire per block).
    // v0.9 Lane B (B-M2): the B2 VS bank is now DOUBLE-BUFFERED (active_vs_slot_),
    // so a runtime SOFA swap no longer needs to force-Direct this block — the
    // audio thread simply reads whichever VS slot is published. No handshake.
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
    // v0.9 Lane B (B-M2) — END OF BLOCK. finalizeXfadeBlock() is called once per
    // block AFTER the B1/B2 dispatch (which read the active table + VS slots).
    // Publish (release) the slots actually consumed this block for the control
    // tick's quiescence handshake, then bump the per-block tick (liveness
    // signal). The release ordering ensures the control tick's acquire-load of
    // last_consumed_* happens-after this block's slot DATA reads.
    last_consumed_table_slot_.store(last_consumed_table_slot_local_,
                                    std::memory_order_release);
    last_consumed_vs_slot_.store(last_consumed_vs_slot_local_,
                                 std::memory_order_release);
    audio_block_tick_.fetch_add(1, std::memory_order_release);

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
    //    v0.9 Lane B (B-M2): acquire-load the active VS-bank slot ONCE per block
    //    (mirror AmbiDecoder::decode() at AmbiDecoder.cpp:241) and read that
    //    slot's convolvers. A runtime swap publishes the other slot; this read
    //    is alloc-free and race-free under the double-buffer invariant.
    const int vslot_i = active_vs_slot_.load(std::memory_order_acquire);
    last_consumed_vs_slot_local_ = vslot_i;  // audio-thread-only; published at EOB
    const std::size_t vslot = static_cast<std::size_t>(vslot_i);
    std::memset(leftOut,  0, out_bytes);
    std::memset(rightOut, 0, out_bytes);
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        vs_conv_L_[vslot][static_cast<std::size_t>(i)].process(
            vs_buf_[static_cast<std::size_t>(i)].data(),
            num_samples, vs_conv_L_scratch_.data());
        vs_conv_R_[vslot][static_cast<std::size_t>(i)].process(
            vs_buf_[static_cast<std::size_t>(i)].data(),
            num_samples, vs_conv_R_scratch_.data());
        for (int n = 0; n < num_samples; ++n) {
            leftOut[n]  += vs_conv_L_scratch_[static_cast<std::size_t>(n)];
            rightOut[n] += vs_conv_R_scratch_[static_cast<std::size_t>(n)];
        }
    }
}

} // namespace spe::output

// core/src/output_backend/BinauralMonitor.cpp

#include "output_backend/BinauralMonitor.h"
#include "hrtf/HrtfLookup.h"

#include <algorithm>
#include <cstring>

namespace spe::output {

BinauralMonitor::InitResult BinauralMonitor::initialize(const Config& cfg)
{
    block_size_  = std::min(cfg.blockSize, MAX_BLOCK);
    sample_rate_ = cfg.sampleRate;

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
    return total;
}

} // namespace spe::output

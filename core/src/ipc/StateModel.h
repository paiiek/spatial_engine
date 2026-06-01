// core/src/ipc/StateModel.h
// Engine-side authoritative object state.
// apply(Command) performs seq-validated state mutation.
// Tracks osc_reordered_drops and triggers /sys/warning osc_reorder_burst
// when 5+ reorders occur within a 1-second window.

#pragma once
#include "Command.h"
#include "core/Constants.h"
#include <array>
#include <cstdint>
#include <functional>

namespace spe::ipc {

// v0.9 Lane C: control-side store cap derives from the single canonical cap.
static constexpr int STATE_MAX_OBJECTS = spe::MAX_OBJECTS;

// Per-object authoritative state.
struct ObjectEntry {
    uint32_t  last_applied_seq = 0;
    float     az_rad  = 0.f;
    float     el_rad  = 0.f;
    float     dist_m  = 1.f;
    float     gain    = 1.f;
    bool      active  = false;
    Algorithm algo    = Algorithm::VBAP;
    bool      valid   = false; // true once first command applied
};

// Reorder-alert window: track up to WINDOW_SIZE timestamps (ms).
struct ReorderWindow {
    static constexpr int   WINDOW_SIZE   = 16;
    static constexpr int   BURST_THRESH  = 5;   // count within window
    static constexpr uint64_t WINDOW_MS  = 1000; // 1-second window

    uint64_t times[WINDOW_SIZE]{};
    int      head  = 0;
    int      count = 0;

    void push(uint64_t now_ms) noexcept {
        times[head % WINDOW_SIZE] = now_ms;
        ++head;
        ++count;
    }

    // Count events within last WINDOW_MS from now_ms.
    int countInWindow(uint64_t now_ms) const noexcept {
        int n = 0;
        int sz = (count < WINDOW_SIZE) ? count : WINDOW_SIZE;
        for (int i = 0; i < sz; ++i) {
            if (now_ms - times[i] <= WINDOW_MS) ++n;
        }
        return n;
    }
};

class StateModel {
public:
    using WarningCallback = std::function<void(const Reply&)>;

    StateModel() = default;

    // Set callback invoked when /sys/warning osc_reorder_burst triggers.
    void setWarningCallback(WarningCallback cb) { warning_cb_ = std::move(cb); }

    // Apply a command. Returns false if command was dropped (reorder / out-of-seq).
    // now_ms: current time in ms (mockable for tests).
    bool apply(const Command& cmd, uint64_t now_ms = 0) noexcept;

    // Read-only access to object state.
    const ObjectEntry& objectState(uint32_t obj_id) const noexcept {
        if (obj_id >= STATE_MAX_OBJECTS) return dummy_;
        return objects_[obj_id];
    }

    // Engine-wide default algorithm.
    Algorithm defaultAlgo() const noexcept { return default_algo_; }

    // B-M3 — control-side pending SOFA catalog name for /sys/state reflection.
    // The actual atomic publish lives in HrtfLookup (B-M2); this field is the
    // control-thread record of the most recently requested catalog name.
    void setPendingSofaName(const std::string& name) { pending_sofa_name_ = name; }
    const std::string& pendingSofaName() const noexcept { return pending_sofa_name_; }

    // Diagnostics.
    uint64_t reorderedDrops() const noexcept { return osc_reordered_drops_; }
    uint64_t burstAlerts()    const noexcept { return burst_alerts_; }

    void reset() noexcept;

private:
    std::array<ObjectEntry, STATE_MAX_OBJECTS> objects_{};
    ObjectEntry dummy_{}; // returned for out-of-range id
    Algorithm   default_algo_ = Algorithm::VBAP;

    uint64_t osc_reordered_drops_ = 0;
    uint64_t burst_alerts_        = 0;

    ReorderWindow reorder_window_;
    WarningCallback warning_cb_;
    std::string pending_sofa_name_; // B-M3: last requested catalog name (control-thread only)

    void checkBurst(uint64_t now_ms) noexcept;
};

} // namespace spe::ipc

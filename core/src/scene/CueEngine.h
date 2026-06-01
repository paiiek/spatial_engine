// core/src/scene/CueEngine.h
// v0.9 Lane E (E-M3): cue (snapshot) automation engine.
//
// Owns a CueList + a SceneController* (for scene load) + a SceneCrossfade
// (transition primitive). Fires cues, runs dwell auto-advance, and emits
// interpolated per-object updates via an emitFn callback.
//
// THREADING (load-bearing — see plan §0.4 / fix 1a): every method on this
// class runs on the daemon control loop ONLY. CueEngine state (generation_,
// the crossfade clock, the index, current_frame_, all file I/O) is therefore
// single-threaded. CueEngine NEVER pushes to the audio cmd_fifo_ and NEVER
// reads the audio-thread obj_cache_. emitFn posts a Command to the control→UDP
// mailbox; the UDP listener thread forwards it via the existing sink_ so
// cmd_fifo_ keeps exactly ONE physical producer (the UDP thread).
#pragma once

#include "ipc/Command.h"
#include "ipc/SceneController.h"
#include "scene/CueList.h"
#include "scene/SceneCrossfade.h"

#include <array>
#include <cstdint>
#include <functional>

namespace spe::scene {

// Emit callback: posts one Command to the control→UDP outbound mailbox. Returns
// false when the mailbox is full (drop-and-count, fix #9). Control loop only.
using CueEmitFn = std::function<bool(const ipc::Command&)>;

class CueEngine {
public:
    // ctrl / sample_rate / emit are borrowed; ctrl must outlive this engine.
    CueEngine(ipc::SceneController* ctrl, float sample_rate, CueEmitFn emit);

    // Replace the active cue list (control loop only).
    void setCueList(const CueList& cues) { cues_ = cues; }
    const CueList& cueList() const noexcept { return cues_; }

    // ---- transport API (control loop only) ----------------------------------

    // Fire cue `index`. Bumps generation_ (dwell latch); clamps index; loads the
    // target scene via SceneController. On load failure HOLDS the current scene
    // (no crossfade armed, index unchanged, warn). On success arms a crossfade
    // from current_frame_ to the target and records the crossfade-end (+dwell)
    // deadlines tagged with the bumped generation_.
    void go(int index, int64_t now_ms);

    // Advance / retreat by one cue. Clamp at the ends (no wrap).
    void next(int64_t now_ms);
    void prev(int64_t now_ms);

    // Freeze at the current scene and cancel any pending dwell (bumps
    // generation_ so a stale dwell deadline is ignored).
    void stop(int64_t now_ms);

    // Advance the crossfade clock by elapsed wall-time and emit interpolated
    // ObjectFrames. On crossfade completion snaps to target + updates
    // current_frame_, then fires dwell auto-advance if armed and not stale.
    void tick(int64_t now_ms);

    // ---- accessors (test / telemetry) ---------------------------------------
    int          currentIndex()   const noexcept { return current_index_; }
    uint64_t     generation()     const noexcept { return generation_; }
    bool         crossfadeActive() const noexcept { return crossfade_.active(); }
    std::uint64_t droppedUpdates() const noexcept { return dropped_updates_; }
    const std::array<ObjectFrame, MAX_OBJECTS>& currentFrame() const noexcept {
        return current_frame_;
    }

private:
    // Convert the controller's last-loaded SceneSnapshot into a crossfade
    // Snapshot (per-object indexed by id; gain_linear→gain_db; active=!muted;
    // width/reverb_send default 0). See plan E-M3 fix #3.
    Snapshot snapshotToFrames(const ipc::SceneSnapshot& snap) const;

    // Emit one object's interpolated frame as ObjMove/ObjGain/ObjActive/ObjAlgo
    // commands through emit_ (reverse gain conversion on ObjGain, fix #7).
    void emitObject(int obj_id, const ObjectFrame& f);

    ipc::SceneController* ctrl_       = nullptr;
    float                 sample_rate_ = 48000.f;
    CueEmitFn             emit_;

    CueList        cues_;
    SceneCrossfade crossfade_;

    int      current_index_ = -1;   // -1 = no cue fired yet
    uint64_t generation_    = 0;    // dwell latch counter

    // Control-loop-owned crossfade baseline (fix #8): never reads obj_cache_.
    std::array<ObjectFrame, MAX_OBJECTS> current_frame_{};

    // Wall-clock crossfade bookkeeping. crossfade_end_ms_ marks completion;
    // last_tick_ms_ tracks elapsed time fed into SceneCrossfade::advance.
    bool    crossfade_in_progress_ = false;
    int64_t crossfade_end_ms_      = 0;
    int64_t last_tick_ms_          = 0;

    // Dwell deadline + the generation that armed it (D2 latch). Active only when
    // dwell_armed_ is true.
    bool     dwell_armed_       = false;
    int64_t  dwell_deadline_ms_ = 0;
    uint64_t dwell_generation_  = 0;

    std::uint64_t dropped_updates_ = 0; // fix #9 drop-and-count
};

} // namespace spe::scene

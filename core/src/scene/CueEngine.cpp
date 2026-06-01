// core/src/scene/CueEngine.cpp
// v0.9 Lane E (E-M3): cue firing engine + dwell auto-advance.
// Control-loop only. See CueEngine.h for the threading contract.

#include "scene/CueEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace spe::scene {

namespace {
// Algorithm enum range guard (Critic OQ): valid ipc::Algorithm values are
// 0..3 (VBAP/WFS/DBAP/Ambisonic). Out-of-range scene data clamps to VBAP (0).
constexpr int kAlgoMin = 0;
constexpr int kAlgoMax = static_cast<int>(ipc::Algorithm::Ambisonic); // 3

inline ipc::Algorithm safeAlgorithm(int algorithm) noexcept {
    if (algorithm < kAlgoMin || algorithm > kAlgoMax) return ipc::Algorithm::VBAP;
    return static_cast<ipc::Algorithm>(algorithm);
}
} // namespace

CueEngine::CueEngine(ipc::SceneController* ctrl, float sample_rate, CueEmitFn emit)
    : ctrl_(ctrl), sample_rate_(sample_rate > 0.f ? sample_rate : 48000.f),
      emit_(std::move(emit)) {
    // current_frame_ starts neutral (default ObjectFrame: active=false,
    // algorithm=0, gain_db=0, dist=1, others 0).
}

Snapshot CueEngine::snapshotToFrames(const ipc::SceneSnapshot& snap) const {
    Snapshot out; // all slots default-constructed: active=false (fix #3)
    for (const auto& o : snap.objects) {
        if (o.id < 0 || o.id >= MAX_OBJECTS) continue;
        auto& f = out.objects[static_cast<std::size_t>(o.id)];
        f.az_rad    = o.az_rad;
        f.el_rad    = o.el_rad;
        f.dist_m    = o.dist_m;
        f.algorithm = o.algorithm;
        // gain_linear → gain_db (clamp to avoid log10(0)). SceneCrossfade lerps
        // gain in dB; emit() converts back to linear (fix #7).
        const float lin = o.gain_linear > 1e-6f ? o.gain_linear : 1e-6f;
        f.gain_db = 20.f * std::log10(lin);
        // active = !muted for listed objects (fix #3; listed-but-muted → false).
        f.active = !o.muted;
        // width_rad / reverb_send: no source in ObjectSnapshot → default 0 (F4).
        f.width_rad   = 0.f;
        f.reverb_send = 0.f;
    }
    return out;
}

void CueEngine::emitObject(int obj_id, const ObjectFrame& f) {
    if (!emit_) return;
    const uint32_t oid = static_cast<uint32_t>(obj_id);

    auto post = [&](const ipc::Command& c) {
        if (!emit_(c)) ++dropped_updates_; // fix #9: drop-and-count
    };

    // ObjMove (az/el/dist).
    {
        ipc::Command c;
        c.tag = ipc::CommandTag::ObjMove;
        ipc::PayloadObjMove p;
        p.obj_id = oid;
        p.az_rad = f.az_rad;
        p.el_rad = f.el_rad;
        p.dist_m = f.dist_m;
        c.payload = p;
        post(c);
    }
    // ObjGain — REVERSE conversion dB → linear (fix #7). PayloadObjGain.gain is
    // LINEAR; emitting dB here would silently corrupt every cue gain.
    {
        ipc::Command c;
        c.tag = ipc::CommandTag::ObjGain;
        ipc::PayloadObjGain p;
        p.obj_id = oid;
        p.gain   = std::pow(10.f, f.gain_db / 20.f);
        c.payload = p;
        post(c);
    }
    // ObjActive.
    {
        ipc::Command c;
        c.tag = ipc::CommandTag::ObjActive;
        ipc::PayloadObjActive p;
        p.obj_id = oid;
        p.active = f.active;
        c.payload = p;
        post(c);
    }
    // ObjAlgo (range-guarded before enum cast, Critic OQ).
    {
        ipc::Command c;
        c.tag = ipc::CommandTag::ObjAlgo;
        ipc::PayloadObjAlgo p;
        p.obj_id = oid;
        p.algo   = safeAlgorithm(f.algorithm);
        c.payload = p;
        post(c);
    }
}

void CueEngine::go(int index, int64_t now_ms) {
    // Bump generation_ FIRST so any in-flight dwell is latched out (D2),
    // regardless of whether the load below succeeds.
    ++generation_;
    dwell_armed_ = false;

    if (cues_.cues.empty() || !ctrl_) return;

    // Clamp index into range.
    if (index < 0) index = 0;
    if (index >= static_cast<int>(cues_.cues.size()))
        index = static_cast<int>(cues_.cues.size()) - 1;

    const Cue& cue = cues_.cues[static_cast<std::size_t>(index)];

    // Load the target scene via SceneController (control thread).
    ipc::Command load;
    load.tag = ipc::CommandTag::SceneLoad;
    ipc::PayloadSceneLoad p;
    const std::size_t n = cue.scene.size() < 63 ? cue.scene.size() : 63;
    std::memcpy(p.name, cue.scene.c_str(), n);
    p.name[n] = '\0';
    load.payload = p;
    ctrl_->handleCommand(load);

    const auto& loaded = ctrl_->lastLoaded();
    if (!loaded.has_value()) {
        // SceneLoad failure policy: HOLD current scene. No crossfade armed,
        // index unchanged, warn. generation_ already bumped (latch held).
        std::fprintf(stderr,
            "[CueEngine] WARNING: cue %d scene '%s' load failed — holding current scene\n",
            index, cue.scene.c_str());
        return;
    }

    // Success — convert + arm the crossfade from the tracked baseline (fix #8).
    Snapshot target = snapshotToFrames(*loaded);
    Snapshot from;
    from.objects = current_frame_;

    crossfade_.start(from, target, cue.crossfade_ms, sample_rate_);

    current_index_ = index;
    last_tick_ms_  = now_ms;

    if (crossfade_.active()) {
        crossfade_in_progress_ = true;
        crossfade_end_ms_      = now_ms + static_cast<int64_t>(cue.crossfade_ms);
    } else {
        // duration <= 0 → instant snap. Apply target immediately as the new
        // baseline and emit it.
        crossfade_in_progress_ = false;
        crossfade_end_ms_      = now_ms;
        current_frame_         = target.objects;
        for (int i = 0; i < MAX_OBJECTS; ++i)
            emitObject(i, current_frame_[static_cast<std::size_t>(i)]);
    }

    // Arm dwell (tagged with the current generation_) if requested.
    if (cue.dwell_ms.has_value()) {
        dwell_armed_       = true;
        dwell_deadline_ms_ = crossfade_end_ms_ + static_cast<int64_t>(*cue.dwell_ms);
        dwell_generation_  = generation_;
    }
}

void CueEngine::next(int64_t now_ms) {
    if (cues_.cues.empty()) return;
    const int last = static_cast<int>(cues_.cues.size()) - 1;
    if (current_index_ >= last) {
        // At the end — no wrap. Still cancel any pending dwell (manual action).
        ++generation_;
        dwell_armed_ = false;
        return;
    }
    go(current_index_ + 1, now_ms);
}

void CueEngine::prev(int64_t now_ms) {
    if (cues_.cues.empty()) return;
    if (current_index_ <= 0) {
        ++generation_;
        dwell_armed_ = false;
        return;
    }
    go(current_index_ - 1, now_ms);
}

void CueEngine::stop(int64_t now_ms) {
    (void)now_ms;
    ++generation_;            // cancels any pending dwell (latch)
    dwell_armed_ = false;
    // Freeze at the current interpolated state: snap the baseline to where the
    // crossfade currently is so a resumed go() starts from the right place.
    if (crossfade_in_progress_) {
        for (int i = 0; i < MAX_OBJECTS; ++i)
            current_frame_[static_cast<std::size_t>(i)] = crossfade_.currentObject(i);
    }
    crossfade_in_progress_ = false;
}

void CueEngine::tick(int64_t now_ms) {
    // Advance the crossfade clock by elapsed wall-time and emit updates.
    if (crossfade_in_progress_) {
        const int64_t elapsed_ms = now_ms - last_tick_ms_;
        last_tick_ms_ = now_ms;
        if (elapsed_ms > 0) {
            const int num_samples = static_cast<int>(
                static_cast<double>(elapsed_ms) * 1e-3 * static_cast<double>(sample_rate_));
            const bool still_active = crossfade_.advance(num_samples > 0 ? num_samples : 1);

            // Emit the interpolated state for every object.
            for (int i = 0; i < MAX_OBJECTS; ++i)
                emitObject(i, crossfade_.currentObject(i));

            if (!still_active) {
                // Completed — snap baseline to the target (fix #8).
                crossfade_in_progress_ = false;
                const Snapshot& to = crossfade_.toSnapshot();
                current_frame_ = to.objects;
            }
        }
    }

    // Dwell auto-advance: fire next() when the deadline passes AND the tag still
    // matches the current generation (D2 latch — a manual go/stop/next/prev
    // bumped generation_, so a stale dwell is ignored).
    if (dwell_armed_ && !crossfade_in_progress_ &&
        now_ms >= dwell_deadline_ms_ &&
        dwell_generation_ == generation_) {
        dwell_armed_ = false;
        next(now_ms);
    }
}

} // namespace spe::scene

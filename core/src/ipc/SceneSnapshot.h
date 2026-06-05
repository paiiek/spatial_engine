// core/src/ipc/SceneSnapshot.h
// Scene (snapshot) serialisation — no external JSON dependency.
#pragma once
#include <string>
#include <vector>
#include <optional>

namespace spe::ipc {

struct ObjectSnapshot {
    int   id          = 0;
    float az_rad      = 0.f;
    float el_rad      = 0.f;
    float dist_m      = 1.f;
    int   algorithm   = 0;   // Algorithm enum cast to int
    float gain_linear = 1.f;
    bool  muted       = false;
    float width_rad   = 0.f; // source spread in radians (0 = point source)
    float reverb_send = 0.f; // reverb send level (0..1, linear)
};

// Room engine state captured in a scene (the 22 reference room params + the
// enable gate). `present` is false for scenes saved before room integration —
// fromJson leaves it false when no "room" block exists, so /room/preset on such
// a scene is a safe no-op. Field set + defaults mirror ipc::PayloadRoomCtl.
struct RoomSnapshot {
    bool  present              = false;
    bool  enabled              = false;
    float t60                  = 1.2f;
    float sx                   = 6.f;
    float sy                   = 5.f;
    float sz                   = 3.f;
    float early_width_deg      = 45.f;
    float early_balance01      = 0.45f;
    float cluster_send01       = 0.4f;
    float cluster_diffusion01  = 0.48f;
    float cluster_volume_m3    = 630.f;
    float eq_early_hp          = 120.f;
    float eq_early_lp          = 10000.f;
    float late_hf_corner_hz    = 6200.f;
    float late_hf_ratio01      = 0.62f;
    float eq_late_hp           = 45.f;
    float eq_late_lp           = 16000.f;
    float dist_near_m          = 0.5f;
    float dist_far_m           = 24.f;
    float dist_linearity01     = 0.35f;
    float early_gain_close_db  = -10.f;
    float early_gain_far_db    = -18.f;
    float late_gain_close_db   = -12.f;
    float late_gain_far_db     = 0.f;
    float early_predelay_ms    = 20.f;
};

struct SceneSnapshot {
    std::string              name;
    std::vector<ObjectSnapshot> objects;
    RoomSnapshot             room;  // present=false unless the scene carries a room block

    // JSON serialisation (manual, fixed key order, no escape support)
    std::string toJson() const;
    static SceneSnapshot fromJson(const std::string& json);

    // Disk I/O — files live at scenesDir/name.json
    bool saveToDisk(const std::string& scenesDir) const;
    static std::optional<SceneSnapshot> loadFromDisk(const std::string& scenesDir,
                                                      const std::string& name);
    static std::vector<std::string> listScenes(const std::string& scenesDir);
};

} // namespace spe::ipc

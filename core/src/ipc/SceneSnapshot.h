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
    // C6 — additive per-object DSP detail for full-state resync. Defaults match
    // SpatialEngine::ObjCache so an unset object is byte-identical to before.
    // NOT serialized by toJson/fromJson (fixed key order) → scenes stay
    // byte-identical; carried only by the /sys/state_request dump.
    float k_hf          = 0.5f;
    float user_delay_ms = 0.f;
    float eq_gain_db[4] = {0.f, 0.f, 0.f, 0.f};
    // A3 — additive trailing per-object input→object route, carried ONLY by the
    // /sys/state_request resync dump (NOT by toJson/fromJson → scenes stay
    // byte-identical, exactly the C6 k_hf/user_delay_ms/eq_gain_db pattern;
    // F-A3-persist owns the scene-JSON keys later). Defaults match ObjCache.
    int32_t input_src_ch = -1;
    float   input_gain   = 1.f;
};

struct SceneSnapshot {
    std::string              name;
    std::vector<ObjectSnapshot> objects;

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

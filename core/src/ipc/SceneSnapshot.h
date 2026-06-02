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

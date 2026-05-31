// core/src/scene/CueList.h
// Cue list data model + serialisation — no external JSON dependency.
// E-M2: pure data + file I/O; never referenced from the audio path.
#pragma once
#include <string>
#include <vector>
#include <optional>

namespace spe::scene {

struct Cue {
    std::string scene;
    float crossfade_ms = 0.f;
    std::optional<float> dwell_ms;   // nullopt = manual-advance-only
};

class CueList {
public:
    std::vector<Cue> cues;

    // JSON serialisation (manual, fixed key order, SceneSnapshot style)
    std::string toJson() const;
    static CueList fromJson(const std::string& str);

    // Disk I/O — stored at dir/cuelist.json
    bool loadFromDisk(const std::string& dir);
    bool saveToDisk(const std::string& dir) const;
};

} // namespace spe::scene

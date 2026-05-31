// core/src/scene/CueList.cpp
// E-M2: Cue list model + serialisation (manual JSON, SceneSnapshot style).
#include "scene/CueList.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <algorithm>

namespace spe::scene {

// ---- helpers (mirrored from SceneSnapshot.cpp) ------------------------------

static std::string ftos(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return buf;
}

// Find "key": and return raw value token (numbers, booleans, quoted strings).
static std::string jsonGet(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        ++pos;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return {};
        return json.substr(pos, end - pos);
    }
    // number, boolean, or null — read until , } ]
    auto end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') ++end;
    return json.substr(pos, end - pos);
}

static float parseFloatOr(const std::string& s, float defv) noexcept {
    if (s.empty()) return defv;
    try { return std::stof(s); } catch (const std::exception&) { return defv; }
}

// ---- toJson -----------------------------------------------------------------

std::string CueList::toJson() const {
    std::ostringstream os;
    os << "{\"cues\":[";
    for (std::size_t i = 0; i < cues.size(); ++i) {
        const auto& c = cues[i];
        if (i > 0) os << ',';
        os << '{'
           << "\"scene\":\"" << c.scene << "\","
           << "\"crossfade_ms\":" << ftos(c.crossfade_ms);
        if (c.dwell_ms.has_value()) {
            os << ",\"dwell_ms\":" << ftos(*c.dwell_ms);
        }
        os << '}';
    }
    os << "]}";
    return os.str();
}

// ---- fromJson ---------------------------------------------------------------

CueList CueList::fromJson(const std::string& str) {
    CueList cl;
    try {
        static constexpr char kCuesKey[] = "\"cues\":[";
        auto arr_start = str.find(kCuesKey);
        if (arr_start == std::string::npos) return cl;
        arr_start += sizeof(kCuesKey) - 1;

        std::size_t pos = arr_start;
        while (pos < str.size()) {
            auto open = str.find('{', pos);
            if (open == std::string::npos) break;
            auto close = str.find('}', open);
            if (close == std::string::npos) break;

            const std::string obj = str.substr(open, close - open + 1);

            Cue cue;
            cue.scene = jsonGet(obj, "scene");

            // Validation: drop cues with empty scene name
            if (cue.scene.empty()) { pos = close + 1; continue; }

            // crossfade_ms: clamp negatives to 0
            float cf = parseFloatOr(jsonGet(obj, "crossfade_ms"), 0.f);
            cue.crossfade_ms = std::max(0.f, cf);

            // dwell_ms: missing or "null" → nullopt; negative → clamp to 0
            std::string dwell_raw = jsonGet(obj, "dwell_ms");
            if (!dwell_raw.empty() && dwell_raw != "null") {
                float dw = parseFloatOr(dwell_raw, 0.f);
                cue.dwell_ms = std::max(0.f, dw);
            }
            // else leave as nullopt

            cl.cues.push_back(std::move(cue));
            pos = close + 1;
        }
    } catch (...) {
        // Swallow all errors — return whatever was collected (possibly empty)
        cl.cues.clear();
    }
    return cl;
}

// ---- loadFromDisk -----------------------------------------------------------

bool CueList::loadFromDisk(const std::string& dir) {
    std::string path = dir + "/cuelist.json";
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::ostringstream buf;
    buf << ifs.rdbuf();
    *this = CueList::fromJson(buf.str());
    return true;
}

// ---- saveToDisk -------------------------------------------------------------

bool CueList::saveToDisk(const std::string& dir) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;

    std::string path = dir + "/cuelist.json";
    std::ofstream ofs(path);
    if (!ofs) return false;
    ofs << toJson();
    return ofs.good();
}

} // namespace spe::scene

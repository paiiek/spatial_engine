// core/src/ipc/SceneSnapshot.cpp
#include "ipc/SceneSnapshot.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <string>

namespace spe::ipc {

// ---- helpers ----------------------------------------------------------------

static std::string ftos(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return buf;
}

// Very small JSON key extractor: find "key": and return raw value token.
// Only handles simple values (numbers, booleans, quoted strings without escapes).
static std::string jsonGet(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    // skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        // string value
        ++pos;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return {};
        return json.substr(pos, end - pos);
    }
    // number or boolean — read until , } ]
    auto end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') ++end;
    return json.substr(pos, end - pos);
}

// Parse helpers that swallow malformed numerics — keep field at default on failure.
static int   parseIntOr  (const std::string& s, int   defv) noexcept {
    if (s.empty()) return defv;
    try { return std::stoi(s); } catch (const std::exception&) { return defv; }
}
static float parseFloatOr(const std::string& s, float defv) noexcept {
    if (s.empty()) return defv;
    try { return std::stof(s); } catch (const std::exception&) { return defv; }
}

// ---- toJson -----------------------------------------------------------------

std::string SceneSnapshot::toJson() const {
    std::ostringstream os;
    os << "{\"name\":\"" << name << "\",\"objects\":[";
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& o = objects[i];
        if (i > 0) os << ',';
        os << '{'
           << "\"id\":"          << std::to_string(o.id)        << ','
           << "\"az_rad\":"      << ftos(o.az_rad)               << ','
           << "\"el_rad\":"      << ftos(o.el_rad)               << ','
           << "\"dist_m\":"      << ftos(o.dist_m)               << ','
           << "\"algorithm\":"   << std::to_string(o.algorithm)  << ','
           << "\"gain_linear\":" << ftos(o.gain_linear)          << ','
           << "\"muted\":"       << (o.muted ? "true" : "false")
           << '}';
    }
    os << "]}";
    return os.str();
}

// ---- path traversal guard ---------------------------------------------------

static bool isSafeSceneName(const std::string& name) {
    if (name.empty() || name.size() > 63) return false;
    for (char c : name)
        if (c == '/' || c == '\\' || c == '\0' || c == '.') return false;
    return true;
}

// ---- fromJson ---------------------------------------------------------------

SceneSnapshot SceneSnapshot::fromJson(const std::string& json) {
    SceneSnapshot ss;
    ss.name = jsonGet(json, "name");

    // Find "objects":[ and iterate over {...} blocks
    static constexpr char kObjectsKey[] = "\"objects\":[";
    auto arr_start = json.find(kObjectsKey);
    if (arr_start == std::string::npos) return ss;
    arr_start += sizeof(kObjectsKey) - 1;

    std::size_t pos = arr_start;
    while (pos < json.size()) {
        auto open = json.find('{', pos);
        if (open == std::string::npos) break;
        auto close = json.find('}', open);
        if (close == std::string::npos) break;

        const std::string obj_str = json.substr(open, close - open + 1);
        auto getI = [&](const char* k, int   defv) { return parseIntOr  (jsonGet(obj_str, k), defv); };
        auto getF = [&](const char* k, float defv) { return parseFloatOr(jsonGet(obj_str, k), defv); };

        ObjectSnapshot o;
        o.id          = getI("id",          o.id);
        o.az_rad      = getF("az_rad",      o.az_rad);
        o.el_rad      = getF("el_rad",      o.el_rad);
        o.dist_m      = getF("dist_m",      o.dist_m);
        o.algorithm   = getI("algorithm",   o.algorithm);
        o.gain_linear = getF("gain_linear", o.gain_linear);
        o.muted       = (jsonGet(obj_str, "muted") == "true");
        ss.objects.push_back(o);
        pos = close + 1;
    }
    return ss;
}

// ---- saveToDisk -------------------------------------------------------------

bool SceneSnapshot::saveToDisk(const std::string& scenesDir) const {
    if (!isSafeSceneName(name)) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(scenesDir, ec);
    if (ec) return false;

    std::string path = scenesDir + "/" + name + ".json";
    std::ofstream ofs(path);
    if (!ofs) return false;
    ofs << toJson();
    return ofs.good();
}

// ---- loadFromDisk -----------------------------------------------------------

std::optional<SceneSnapshot> SceneSnapshot::loadFromDisk(const std::string& scenesDir,
                                                          const std::string& name) {
    if (!isSafeSceneName(name)) return std::nullopt;
    std::string path = scenesDir + "/" + name + ".json";
    std::ifstream ifs(path);
    if (!ifs) return std::nullopt;
    std::ostringstream buf;
    buf << ifs.rdbuf();
    return fromJson(buf.str());
}

// ---- listScenes -------------------------------------------------------------

std::vector<std::string> SceneSnapshot::listScenes(const std::string& scenesDir) {
    namespace fs = std::filesystem;
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(scenesDir, ec)) {
        if (ec) break;
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

} // namespace spe::ipc

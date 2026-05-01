// core/src/ipc/SceneSnapshot.cpp
#include "ipc/SceneSnapshot.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstring>

namespace spe::ipc {

// ---- helpers ----------------------------------------------------------------

static std::string ftos(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return buf;
}

static std::string itos(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
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

// ---- toJson -----------------------------------------------------------------

std::string SceneSnapshot::toJson() const {
    std::ostringstream os;
    os << "{\"name\":\"" << name << "\",\"objects\":[";
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& o = objects[i];
        if (i > 0) os << ',';
        os << '{'
           << "\"id\":"          << itos(o.id)          << ','
           << "\"az_rad\":"      << ftos(o.az_rad)      << ','
           << "\"el_rad\":"      << ftos(o.el_rad)      << ','
           << "\"dist_m\":"      << ftos(o.dist_m)      << ','
           << "\"algorithm\":"   << itos(o.algorithm)   << ','
           << "\"gain_linear\":" << ftos(o.gain_linear) << ','
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
    auto arr_start = json.find("\"objects\":[");
    if (arr_start == std::string::npos) return ss;
    arr_start += std::strlen("\"objects\":[");

    std::size_t pos = arr_start;
    while (pos < json.size()) {
        auto open = json.find('{', pos);
        if (open == std::string::npos) break;
        auto close = json.find('}', open);
        if (close == std::string::npos) break;

        std::string obj_str = json.substr(open, close - open + 1);
        ObjectSnapshot o;
        try {
            auto s = jsonGet(obj_str, "id");        if (!s.empty()) o.id          = std::stoi(s);
            s = jsonGet(obj_str, "az_rad");         if (!s.empty()) o.az_rad      = std::stof(s);
            s = jsonGet(obj_str, "el_rad");         if (!s.empty()) o.el_rad      = std::stof(s);
            s = jsonGet(obj_str, "dist_m");         if (!s.empty()) o.dist_m      = std::stof(s);
            s = jsonGet(obj_str, "algorithm");      if (!s.empty()) o.algorithm   = std::stoi(s);
            s = jsonGet(obj_str, "gain_linear");    if (!s.empty()) o.gain_linear = std::stof(s);
            s = jsonGet(obj_str, "muted");          o.muted = (s == "true");
        } catch (const std::exception&) {
            // Malformed numeric field — keep defaults for this object.
        }
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

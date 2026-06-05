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
           << "\"muted\":"       << (o.muted ? "true" : "false")  << ','
           << "\"width_rad\":"   << ftos(o.width_rad)             << ','
           << "\"reverb_send\":" << ftos(o.reverb_send)
           << '}';
    }
    os << ']';
    // Room block — emitted only when captured (present), so scenes saved without
    // a room provider stay byte-identical to the pre-room format.
    if (room.present) {
        os << ",\"room\":{"
           << "\"enabled\":"            << (room.enabled ? "true" : "false")   << ','
           << "\"t60\":"                << ftos(room.t60)                << ','
           << "\"sx\":"                 << ftos(room.sx)                 << ','
           << "\"sy\":"                 << ftos(room.sy)                 << ','
           << "\"sz\":"                 << ftos(room.sz)                 << ','
           << "\"early_width_deg\":"    << ftos(room.early_width_deg)    << ','
           << "\"early_balance01\":"    << ftos(room.early_balance01)    << ','
           << "\"cluster_send01\":"     << ftos(room.cluster_send01)     << ','
           << "\"cluster_diffusion01\":"<< ftos(room.cluster_diffusion01)<< ','
           << "\"cluster_volume_m3\":"  << ftos(room.cluster_volume_m3)  << ','
           << "\"eq_early_hp\":"        << ftos(room.eq_early_hp)        << ','
           << "\"eq_early_lp\":"        << ftos(room.eq_early_lp)        << ','
           << "\"late_hf_corner_hz\":"  << ftos(room.late_hf_corner_hz)  << ','
           << "\"late_hf_ratio01\":"    << ftos(room.late_hf_ratio01)    << ','
           << "\"eq_late_hp\":"         << ftos(room.eq_late_hp)         << ','
           << "\"eq_late_lp\":"         << ftos(room.eq_late_lp)         << ','
           << "\"dist_near_m\":"        << ftos(room.dist_near_m)        << ','
           << "\"dist_far_m\":"         << ftos(room.dist_far_m)         << ','
           << "\"dist_linearity01\":"   << ftos(room.dist_linearity01)   << ','
           << "\"early_gain_close_db\":"<< ftos(room.early_gain_close_db)<< ','
           << "\"early_gain_far_db\":"  << ftos(room.early_gain_far_db)  << ','
           << "\"late_gain_close_db\":" << ftos(room.late_gain_close_db) << ','
           << "\"late_gain_far_db\":"   << ftos(room.late_gain_far_db)   << ','
           << "\"early_predelay_ms\":"  << ftos(room.early_predelay_ms)
           << '}';
    }
    os << '}';
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
    // Bound the object scan to the array's closing ']' so a trailing "room":{...}
    // block (object values contain no ']') is never mis-parsed as an object.
    const auto arr_end = json.find(']', arr_start);

    std::size_t pos = arr_start;
    while (pos < json.size()) {
        auto open = json.find('{', pos);
        if (open == std::string::npos) break;
        if (arr_end != std::string::npos && open > arr_end) break;
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
        // F4: default-tolerant — old scenes lacking these keys load both as 0.
        o.width_rad   = getF("width_rad",   o.width_rad);
        o.reverb_send = getF("reverb_send", o.reverb_send);
        ss.objects.push_back(o);
        pos = close + 1;
    }

    // Room block (optional — absent in pre-room scenes → room.present stays false).
    // INVARIANT: the room block must stay FLAT (no nested objects). The close is
    // the first '}' after "room":{ — correct only while every value is a scalar.
    // If a nested object is ever added, switch to brace-matching here.
    static constexpr char kRoomKey[] = "\"room\":{";
    auto room_start = json.find(kRoomKey);
    if (room_start != std::string::npos) {
        auto room_close = json.find('}', room_start);
        const std::string r = (room_close != std::string::npos)
            ? json.substr(room_start, room_close - room_start + 1)
            : json.substr(room_start);
        auto getF = [&](const char* k, float defv) { return parseFloatOr(jsonGet(r, k), defv); };
        RoomSnapshot& rm = ss.room;
        rm.present             = true;
        rm.enabled             = (jsonGet(r, "enabled") == "true");
        rm.t60                 = getF("t60",                 rm.t60);
        rm.sx                  = getF("sx",                  rm.sx);
        rm.sy                  = getF("sy",                  rm.sy);
        rm.sz                  = getF("sz",                  rm.sz);
        rm.early_width_deg     = getF("early_width_deg",     rm.early_width_deg);
        rm.early_balance01     = getF("early_balance01",     rm.early_balance01);
        rm.cluster_send01      = getF("cluster_send01",      rm.cluster_send01);
        rm.cluster_diffusion01 = getF("cluster_diffusion01", rm.cluster_diffusion01);
        rm.cluster_volume_m3   = getF("cluster_volume_m3",   rm.cluster_volume_m3);
        rm.eq_early_hp         = getF("eq_early_hp",         rm.eq_early_hp);
        rm.eq_early_lp         = getF("eq_early_lp",         rm.eq_early_lp);
        rm.late_hf_corner_hz   = getF("late_hf_corner_hz",   rm.late_hf_corner_hz);
        rm.late_hf_ratio01     = getF("late_hf_ratio01",     rm.late_hf_ratio01);
        rm.eq_late_hp          = getF("eq_late_hp",          rm.eq_late_hp);
        rm.eq_late_lp          = getF("eq_late_lp",          rm.eq_late_lp);
        rm.dist_near_m         = getF("dist_near_m",         rm.dist_near_m);
        rm.dist_far_m          = getF("dist_far_m",          rm.dist_far_m);
        rm.dist_linearity01    = getF("dist_linearity01",    rm.dist_linearity01);
        rm.early_gain_close_db = getF("early_gain_close_db", rm.early_gain_close_db);
        rm.early_gain_far_db   = getF("early_gain_far_db",   rm.early_gain_far_db);
        rm.late_gain_close_db  = getF("late_gain_close_db",  rm.late_gain_close_db);
        rm.late_gain_far_db    = getF("late_gain_far_db",    rm.late_gain_far_db);
        rm.early_predelay_ms   = getF("early_predelay_ms",   rm.early_predelay_ms);
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
            // Skip the reserved scene-library sidecar (v0.9 Lane E) — it lives
            // in the scenes dir but is not itself a scene snapshot.
            if (entry.path().filename() == "index.json") continue;
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

} // namespace spe::ipc

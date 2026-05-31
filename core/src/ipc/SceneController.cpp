// core/src/ipc/SceneController.cpp
// Message-thread handler for SceneSave / SceneLoad / SceneList and the v0.9
// Lane E (E-M1) library-management ops (rename / duplicate / delete / meta).
// RT audio thread must never call this code. Pure std::filesystem + string ops.

#include "ipc/SceneController.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace spe::ipc {

namespace {

namespace fs = std::filesystem;

// ---- path traversal guard ---------------------------------------------------
// Mirrors SceneSnapshot.cpp:76 — non-empty, <=63 chars, rejects / \ NUL and '.'
// so names cannot contain path separators or dots.
bool isSafeSceneName(const std::string& name) {
    if (name.empty() || name.size() > 63) return false;
    for (char c : name)
        if (c == '/' || c == '\\' || c == '\0' || c == '.') return false;
    return true;
}

std::string scenePath(const std::string& scenesDir, const std::string& name) {
    return scenesDir + "/" + name + ".json";
}

// ---- minimal JSON helpers (SceneSnapshot.cpp idiom) -------------------------

// Find "key": and return the raw value token (quoted strings without escapes,
// numbers, booleans). Searches from `start` so repeated keys can be iterated.
std::string jsonGetFrom(const std::string& json, const std::string& key,
                        std::size_t start, std::size_t* value_end) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle, start);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        ++pos;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return {};
        if (value_end) *value_end = end + 1;
        return json.substr(pos, end - pos);
    }
    auto end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') ++end;
    if (value_end) *value_end = end;
    return json.substr(pos, end - pos);
}

int64_t parseI64Or(const std::string& s, int64_t defv) noexcept {
    if (s.empty()) return defv;
    try { return static_cast<int64_t>(std::stoll(s)); } catch (const std::exception&) { return defv; }
}

// Parse a JSON string array "[\"a\",\"b\"]" starting at the bracket — only
// simple quoted tokens without escapes (matches the no-escape snapshot policy).
std::vector<std::string> parseStringArray(const std::string& json, std::size_t bracket) {
    std::vector<std::string> out;
    if (bracket >= json.size() || json[bracket] != '[') return out;
    std::size_t pos = bracket + 1;
    while (pos < json.size() && json[pos] != ']') {
        auto q1 = json.find('"', pos);
        if (q1 == std::string::npos || q1 > json.find(']', pos)) break;
        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        out.push_back(json.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }
    return out;
}

int64_t fileMtimeUnix(const fs::path& p) noexcept {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    if (ec) {
        return static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
    // Convert file_clock → system_clock seconds (portable best-effort).
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
}

int64_t nowUnix() noexcept {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace

// ---- construction -----------------------------------------------------------

SceneController::SceneController(const std::string& scenesDir)
    : scenesDir_(scenesDir) {
    // Try-parse index.json; on parse failure OR missing → rebuild from disk.
    if (!loadIndexFromDisk()) {
        rebuildIndex();
    }
}

// ---- index lookup -----------------------------------------------------------

SceneIndexEntry* SceneController::findEntry(const std::string& name) {
    for (auto& e : index_.scenes)
        if (e.name == name) return &e;
    return nullptr;
}

const SceneIndexEntry* SceneController::findEntry(const std::string& name) const {
    for (const auto& e : index_.scenes)
        if (e.name == name) return &e;
    return nullptr;
}

std::string SceneController::indexPath() const {
    return scenesDir_ + "/index.json";
}

// ---- index serialisation ----------------------------------------------------

bool SceneController::persistIndex() const {
    std::error_code ec;
    fs::create_directories(scenesDir_, ec);
    if (ec) return false;

    std::ostringstream os;
    os << "{\"scenes\":[";
    for (std::size_t i = 0; i < index_.scenes.size(); ++i) {
        const auto& e = index_.scenes[i];
        if (i > 0) os << ',';
        os << "{\"name\":\"" << e.name << "\","
           << "\"created_unix\":" << std::to_string(e.created_unix) << ","
           << "\"tags\":[";
        for (std::size_t t = 0; t < e.tags.size(); ++t) {
            if (t > 0) os << ',';
            os << '"' << e.tags[t] << '"';
        }
        os << "],\"note\":\"" << e.note << "\"}";
    }
    os << "]}";

    std::ofstream ofs(indexPath());
    if (!ofs) return false;
    ofs << os.str();
    return ofs.good();
}

bool SceneController::loadIndexFromDisk() {
    std::ifstream ifs(indexPath());
    if (!ifs) return false; // missing → caller rebuilds
    std::ostringstream buf;
    buf << ifs.rdbuf();
    const std::string json = buf.str();

    // Locate the "scenes":[ array; absent/garbage → treat as parse failure.
    static constexpr char kScenesKey[] = "\"scenes\":[";
    auto arr_start = json.find(kScenesKey);
    if (arr_start == std::string::npos) return false;
    arr_start += sizeof(kScenesKey) - 1;

    // The document must be well-formed enough to terminate the scenes array and
    // the object: the last non-space chars are "]}". A truncated file fails this
    // and falls back to rebuildIndex (D3). Note we cannot use the FIRST ']' as
    // the array end because nested tags arrays contain ']'.
    {
        std::size_t tail = json.size();
        while (tail > 0 && (json[tail - 1] == ' ' || json[tail - 1] == '\t' ||
                            json[tail - 1] == '\n' || json[tail - 1] == '\r'))
            --tail;
        if (tail < 2 || json[tail - 1] != '}' || json[tail - 2] != ']') return false;
    }

    SceneIndex parsed;
    std::size_t pos = arr_start;
    while (pos < json.size()) {
        auto open = json.find('{', pos);
        if (open == std::string::npos) break; // no more entries
        auto close = json.find('}', open);
        // A dangling '{' with no matching '}' means the entry is truncated →
        // corrupt index, rebuild from disk.
        if (close == std::string::npos) return false;
        // tags array may contain '}' only inside strings (none here), and the
        // entry has no nested objects, so the first '}' closes the entry.
        const std::string obj = json.substr(open, close - open + 1);

        SceneIndexEntry e;
        e.name = jsonGetFrom(obj, "name", 0, nullptr);
        e.created_unix = parseI64Or(jsonGetFrom(obj, "created_unix", 0, nullptr), 0);
        e.note = jsonGetFrom(obj, "note", 0, nullptr);
        // tags array
        {
            std::string needle = "\"tags\":";
            auto tp = obj.find(needle);
            if (tp != std::string::npos) {
                tp += needle.size();
                while (tp < obj.size() && (obj[tp] == ' ' || obj[tp] == '\t')) ++tp;
                e.tags = parseStringArray(obj, tp);
            }
        }
        if (!e.name.empty()) parsed.scenes.push_back(e);
        pos = close + 1;
    }

    index_ = std::move(parsed);
    return true;
}

// ---- rebuildIndex (D3 rescan fallback) --------------------------------------

void SceneController::rebuildIndex() {
    const auto files = SceneSnapshot::listScenes(scenesDir_);

    // Drop index entries with no backing file.
    SceneIndex rebuilt;
    for (const auto& e : index_.scenes) {
        bool backed = false;
        for (const auto& f : files)
            if (f == e.name) { backed = true; break; }
        if (backed) rebuilt.scenes.push_back(e);
    }

    // Add default entries for files missing from the index.
    for (const auto& f : files) {
        bool present = false;
        for (const auto& e : rebuilt.scenes)
            if (e.name == f) { present = true; break; }
        if (present) continue;
        SceneIndexEntry e;
        e.name = f;
        e.created_unix = fileMtimeUnix(fs::path(scenePath(scenesDir_, f)));
        rebuilt.scenes.push_back(e); // empty tags/note
    }

    index_ = std::move(rebuilt);
    persistIndex();
}

// ---- library ops ------------------------------------------------------------

bool SceneController::rename(const std::string& from, const std::string& to) {
    if (!isSafeSceneName(from) || !isSafeSceneName(to)) return false;
    std::error_code ec;
    const std::string src = scenePath(scenesDir_, from);
    const std::string dst = scenePath(scenesDir_, to);
    if (!fs::exists(src, ec)) return false;
    if (fs::exists(dst, ec)) return false;

    fs::rename(src, dst, ec);
    if (ec) return false;

    if (auto* existing = findEntry(from)) {
        existing->name = to;
    } else {
        SceneIndexEntry e;
        e.name = to;
        e.created_unix = nowUnix();
        index_.scenes.push_back(e);
    }
    persistIndex();
    return true;
}

bool SceneController::duplicate(const std::string& from, const std::string& to) {
    if (!isSafeSceneName(from) || !isSafeSceneName(to)) return false;
    std::error_code ec;
    const std::string src = scenePath(scenesDir_, from);
    const std::string dst = scenePath(scenesDir_, to);
    if (!fs::exists(src, ec)) return false;
    if (fs::exists(dst, ec)) return false;

    fs::copy_file(src, dst, ec);
    if (ec) return false;

    SceneIndexEntry e;
    if (const auto* srcEntry = findEntry(from)) {
        e = *srcEntry; // clone tags/note
    }
    e.name = to;
    e.created_unix = nowUnix(); // fresh creation time
    index_.scenes.push_back(e);
    persistIndex();
    return true;
}

bool SceneController::remove(const std::string& name) {
    if (!isSafeSceneName(name)) return false;
    std::error_code ec;
    const std::string path = scenePath(scenesDir_, name);
    fs::remove(path, ec); // best-effort; index drop happens regardless

    for (auto it = index_.scenes.begin(); it != index_.scenes.end(); ++it) {
        if (it->name == name) { index_.scenes.erase(it); break; }
    }
    persistIndex();
    return true;
}

bool SceneController::setMeta(const std::string& name, const std::string& metaJson) {
    if (!isSafeSceneName(name)) return false;

    SceneIndexEntry* e = findEntry(name);
    if (!e) {
        // Create an entry if a backing file exists; otherwise reject.
        std::error_code ec;
        if (!fs::exists(scenePath(scenesDir_, name), ec)) return false;
        SceneIndexEntry ne;
        ne.name = name;
        ne.created_unix = fileMtimeUnix(fs::path(scenePath(scenesDir_, name)));
        index_.scenes.push_back(ne);
        e = &index_.scenes.back();
    }

    // Parse tags array + note from the meta JSON (hand-rolled, no escapes).
    {
        std::string needle = "\"tags\":";
        auto tp = metaJson.find(needle);
        if (tp != std::string::npos) {
            tp += needle.size();
            while (tp < metaJson.size() && (metaJson[tp] == ' ' || metaJson[tp] == '\t')) ++tp;
            e->tags = parseStringArray(metaJson, tp);
        }
    }
    e->note = jsonGetFrom(metaJson, "note", 0, nullptr);

    persistIndex();
    return true;
}

// ---- handleCommand ----------------------------------------------------------

bool SceneController::handleCommand(const Command& cmd) {
    switch (cmd.tag) {
        case CommandTag::SceneSave: {
            const auto& p = std::get<PayloadSceneSave>(cmd.payload);
            SceneSnapshot snap;
            snap.name = p.name; // null-terminated fixed-size buffer
            if (!snap.saveToDisk(scenesDir_)) return true;
            // Track new scenes in the index (created_unix on first save).
            if (!findEntry(snap.name) && isSafeSceneName(snap.name)) {
                SceneIndexEntry e;
                e.name = snap.name;
                e.created_unix = nowUnix();
                index_.scenes.push_back(e);
                persistIndex();
            }
            return true;
        }
        case CommandTag::SceneLoad: {
            const auto& p = std::get<PayloadSceneLoad>(cmd.payload);
            lastLoaded_ = SceneSnapshot::loadFromDisk(scenesDir_, p.name);
            return true;
        }
        case CommandTag::SceneList:
            lastSceneList_ = SceneSnapshot::listScenes(scenesDir_);
            return true;
        case CommandTag::SceneRename: {
            const auto& p = std::get<PayloadSceneRename>(cmd.payload);
            rename(p.from, p.to);
            return true;
        }
        case CommandTag::SceneDuplicate: {
            const auto& p = std::get<PayloadSceneDuplicate>(cmd.payload);
            duplicate(p.from, p.to);
            return true;
        }
        case CommandTag::SceneDelete: {
            const auto& p = std::get<PayloadSceneDelete>(cmd.payload);
            remove(p.name);
            return true;
        }
        case CommandTag::SceneMeta: {
            const auto& p = std::get<PayloadSceneMeta>(cmd.payload);
            setMeta(p.name, p.meta_json);
            return true;
        }
        default:
            return false;
    }
}

} // namespace spe::ipc

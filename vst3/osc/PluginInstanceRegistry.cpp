// vst3/osc/PluginInstanceRegistry.cpp
// ADR 0011 implementation: file-based JSON registry, writer-side GC,
// atomic tmpfile+rename, advisory flock(LOCK_EX), boot_id staleness guard.
// JUCE-free: no JUCE includes anywhere in this file.

#include "PluginInstanceRegistry.h"
#include "RegistryPath.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/file.h>   // flock
#include <sys/stat.h>   // mkdir, open
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>

namespace spe::vst3::osc {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string readFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void ensureDir(const std::string& file_path)
{
    // Find the last '/' and mkdir -p up to that point.
    auto pos = file_path.rfind('/');
    if (pos == std::string::npos) return;
    std::string dir = file_path.substr(0, pos);
    // Simple recursive mkdir (only two levels deep for XDG path).
    // Try each component.
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            std::string sub = dir.substr(0, i);
            ::mkdir(sub.c_str(), 0755); // ignore EEXIST
        }
    }
}

// ---------------------------------------------------------------------------
// PluginInstanceRegistry::readBootId
// ---------------------------------------------------------------------------

std::string PluginInstanceRegistry::readBootId()
{
    std::string s = readFile("/proc/sys/kernel/random/boot_id");
    // Trim trailing newline/whitespace.
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// PluginInstanceRegistry::pidAlive
// ---------------------------------------------------------------------------

bool PluginInstanceRegistry::pidAlive(pid_t pid)
{
    std::string path = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream f(path);
    return f.is_open() && f.peek() != std::ifstream::traits_type::eof();
}

// ---------------------------------------------------------------------------
// Minimal JSON emit (fixed ADR-0011 schema, no external library)
// ---------------------------------------------------------------------------

// Escape a JSON string value (handles \n, \r, \t, \", \\).
static std::string jsonEscapeStr(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string PluginInstanceRegistry::emitJson(const std::vector<InstanceEntry>& entries)
{
    std::ostringstream o;
    o << "{\n"
      << "  \"schema_version\": " << kSupportedSchemaVersion << ",\n"
      << "  \"spec_commit\": \"v0.3.0-c4-s2\",\n"
      << "  \"instances\": [\n";

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        o << "    {\n"
          << "      \"instance_id\": " << e.instance_id << ",\n"
          << "      \"port\": "        << e.port        << ",\n"
          << "      \"pid\": "         << static_cast<long>(e.pid) << ",\n"
          << "      \"boot_id\": \""   << jsonEscapeStr(e.boot_id) << "\",\n"
          << "      \"schema_version\": " << e.schema_version << "\n"
          << "    }";
        if (i + 1 < entries.size()) o << ",";
        o << "\n";
    }

    o << "  ]\n"
      << "}\n";
    return o.str();
}

// ---------------------------------------------------------------------------
// Minimal JSON parse — hand-rolled for the fixed schema above.
// We parse: schema_version (top-level), instances array with known fields.
// Returns false on any syntax error or unknown schema_version > supported.
// ---------------------------------------------------------------------------

// Skip whitespace.
static void skipWs(const char*& p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;
}

// Expect and consume a specific character, skip surrounding ws. Returns false on mismatch.
static bool expectChar(const char*& p, const char* end, char c)
{
    skipWs(p, end);
    if (p >= end || *p != c) return false;
    ++p;
    return true;
}

// Parse a JSON string literal (within quotes). Returns false on error.
static bool parseString(const char*& p, const char* end, std::string& out)
{
    skipWs(p, end);
    if (p >= end || *p != '"') return false;
    ++p;
    out.clear();
    while (p < end && *p != '"') {
        if (*p == '\\') {
            ++p;
            if (p >= end) return false;
            switch (*p) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += *p;   break;
            }
        } else {
            out += *p;
        }
        ++p;
    }
    if (p >= end) return false;
    ++p; // consume closing '"'
    return true;
}

// Parse a JSON integer (uint64 or negative). Returns false on error.
static bool parseInt64(const char*& p, const char* end, int64_t& out)
{
    skipWs(p, end);
    if (p >= end) return false;
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    if (p >= end || *p < '0' || *p > '9') return false;
    int64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        ++p;
    }
    out = neg ? -v : v;
    return true;
}

bool PluginInstanceRegistry::parseJson(const std::string& json,
                                        std::vector<InstanceEntry>& out,
                                        uint64_t& schema_version_out)
{
    out.clear();
    schema_version_out = 0;

    if (json.empty()) return false;

    const char* p   = json.c_str();
    const char* end = p + json.size();

    // Expect top-level object '{'
    if (!expectChar(p, end, '{')) return false;

    // Parse key-value pairs until '}'
    while (true) {
        skipWs(p, end);
        if (p >= end) return false;
        if (*p == '}') { ++p; break; }
        if (*p == ',') { ++p; continue; }

        std::string key;
        if (!parseString(p, end, key)) return false;
        if (!expectChar(p, end, ':')) return false;

        if (key == "schema_version") {
            int64_t v = 0;
            if (!parseInt64(p, end, v)) return false;
            schema_version_out = static_cast<uint64_t>(v);
        } else if (key == "instances") {
            // Parse array
            skipWs(p, end);
            if (!expectChar(p, end, '[')) return false;

            while (true) {
                skipWs(p, end);
                if (p >= end) return false;
                if (*p == ']') { ++p; break; }
                if (*p == ',') { ++p; continue; }

                // Parse object entry
                if (!expectChar(p, end, '{')) return false;

                InstanceEntry e{};
                while (true) {
                    skipWs(p, end);
                    if (p >= end) return false;
                    if (*p == '}') { ++p; break; }
                    if (*p == ',') { ++p; continue; }

                    std::string field;
                    if (!parseString(p, end, field)) return false;
                    if (!expectChar(p, end, ':')) return false;

                    int64_t ival = 0;
                    std::string sval;

                    if (field == "boot_id") {
                        if (!parseString(p, end, sval)) return false;
                        e.boot_id = sval;
                    } else {
                        if (!parseInt64(p, end, ival)) return false;
                        if      (field == "instance_id")    e.instance_id    = static_cast<uint32_t>(ival);
                        else if (field == "port")           e.port           = static_cast<uint16_t>(ival);
                        else if (field == "pid")            e.pid            = static_cast<pid_t>(ival);
                        else if (field == "schema_version") e.schema_version = static_cast<uint64_t>(ival);
                        // unknown fields: ignore
                    }
                }
                out.push_back(e);
            }
        } else {
            // Unknown top-level key: skip value (string or number).
            skipWs(p, end);
            if (p < end && *p == '"') {
                std::string tmp;
                parseString(p, end, tmp);
            } else {
                int64_t tmp = 0;
                parseInt64(p, end, tmp);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// PluginInstanceRegistry::atomicWrite
// ---------------------------------------------------------------------------

bool PluginInstanceRegistry::atomicWrite(const std::string& registry_path,
                                          const std::string& json_content)
{
    ensureDir(registry_path);

    // Open (or create) the final path file just for flock purposes.
    // We use O_CREAT|O_RDONLY so existing content is preserved while locked.
    int lock_fd = ::open(registry_path.c_str(),
                         O_RDWR | O_CREAT, 0644);
    if (lock_fd < 0) {
        std::fprintf(stderr, "[PluginInstanceRegistry] open lock fd failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    // Retry flock up to 10 times with 50ms backoff (ADR 0011 §2).
    bool locked = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (::flock(lock_fd, LOCK_EX | LOCK_NB) == 0) {
            locked = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!locked) {
        std::fprintf(stderr, "[PluginInstanceRegistry] flock timeout, skipping write\n");
        ::close(lock_fd);
        return false;
    }

    // Write to tmpfile in same directory.
    std::string tmp_path = registry_path + ".tmp." + std::to_string(static_cast<long>(::getpid()));
    int tmp_fd = ::open(tmp_path.c_str(),
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tmp_fd < 0) {
        std::fprintf(stderr, "[PluginInstanceRegistry] open tmp failed: %s\n",
                     std::strerror(errno));
        ::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return false;
    }

    const char* buf = json_content.c_str();
    size_t remaining = json_content.size();
    while (remaining > 0) {
        ssize_t written = ::write(tmp_fd, buf, remaining);
        if (written < 0) {
            std::fprintf(stderr, "[PluginInstanceRegistry] write failed: %s\n",
                         std::strerror(errno));
            ::close(tmp_fd);
            ::unlink(tmp_path.c_str());
            ::flock(lock_fd, LOCK_UN);
            ::close(lock_fd);
            return false;
        }
        buf       += written;
        remaining -= static_cast<size_t>(written);
    }

    ::fsync(tmp_fd);
    ::close(tmp_fd);

    // Atomic rename.
    if (::rename(tmp_path.c_str(), registry_path.c_str()) != 0) {
        std::fprintf(stderr, "[PluginInstanceRegistry] rename failed: %s\n",
                     std::strerror(errno));
        ::unlink(tmp_path.c_str());
        ::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return false;
    }

    ::flock(lock_fd, LOCK_UN);
    ::close(lock_fd);
    return true;
}

// ---------------------------------------------------------------------------
// PluginInstanceRegistry::registerSelf
// ---------------------------------------------------------------------------

// Global instance_id counter (per-process; unique within a single DAW process).
static std::atomic<uint32_t> g_instance_id_counter{0};

InstanceEntry PluginInstanceRegistry::registerSelf(uint16_t requested_port,
                                                     std::string_view /*plugin_comm*/)
{
    const std::string path = registryPath();
    const std::string boot_id = readBootId();
    const pid_t       my_pid  = ::getpid();
    const uint32_t    my_id   = ++g_instance_id_counter;

    // Read existing entries.
    std::string existing_json = readFile(path);
    std::vector<InstanceEntry> entries;
    uint64_t file_schema_version = 0;

    if (!existing_json.empty()) {
        if (!parseJson(existing_json, entries, file_schema_version)) {
            // Corrupted file — start fresh.
            entries.clear();
            file_schema_version = 0;
        }
        if (file_schema_version > kSupportedSchemaVersion) {
            std::fprintf(stderr,
                "[PluginInstanceRegistry] schema_version %llu > supported %llu, refusing\n",
                static_cast<unsigned long long>(file_schema_version),
                static_cast<unsigned long long>(kSupportedSchemaVersion));
            // Return error entry.
            return InstanceEntry{my_id, 0, my_pid, boot_id, kSupportedSchemaVersion};
        }
    }

    // Writer-side GC: drop dead PIDs and stale boot_ids.
    std::vector<InstanceEntry> fresh;
    for (const auto& e : entries) {
        if (!pidAlive(e.pid)) continue;          // (a) dead PID
        if (e.boot_id != boot_id) continue;      // (b) stale boot_id
        fresh.push_back(e);
    }

    // The UDP port resolution happens outside this class (SpatialEnginePluginUdp
    // resolves the port by actually binding the socket).  For now, accept the
    // requested_port as resolved; SpatialEnginePluginUdp will call registerSelf
    // with the already-bound port.
    InstanceEntry my_entry{my_id, requested_port, my_pid, boot_id, kSupportedSchemaVersion};
    fresh.push_back(my_entry);

    std::string new_json = emitJson(fresh);
    atomicWrite(path, new_json);

    return my_entry;
}

// ---------------------------------------------------------------------------
// PluginInstanceRegistry::unregisterSelf
// ---------------------------------------------------------------------------

void PluginInstanceRegistry::unregisterSelf(uint32_t instance_id)
{
    const std::string path = registryPath();
    const std::string boot_id = readBootId();
    const pid_t       my_pid  = ::getpid();

    std::string existing_json = readFile(path);
    if (existing_json.empty()) return;

    std::vector<InstanceEntry> entries;
    uint64_t file_schema_version = 0;
    if (!parseJson(existing_json, entries, file_schema_version)) return;
    if (file_schema_version > kSupportedSchemaVersion) return;

    // Remove our entry and GC dead PIDs.
    std::vector<InstanceEntry> fresh;
    for (const auto& e : entries) {
        if (e.instance_id == instance_id && e.pid == my_pid) continue; // remove self
        if (!pidAlive(e.pid)) continue;
        if (e.boot_id != boot_id) continue;
        fresh.push_back(e);
    }

    std::string new_json = emitJson(fresh);
    atomicWrite(path, new_json);
}

// ---------------------------------------------------------------------------
// PluginInstanceRegistry::listActive
// ---------------------------------------------------------------------------

std::vector<InstanceEntry> PluginInstanceRegistry::listActive()
{
    const std::string path = registryPath();
    std::string json = readFile(path);
    if (json.empty()) return {};

    std::vector<InstanceEntry> entries;
    uint64_t schema_version = 0;
    if (!parseJson(json, entries, schema_version)) return {};
    if (schema_version > kSupportedSchemaVersion) return {};
    return entries;
}

} // namespace spe::vst3::osc

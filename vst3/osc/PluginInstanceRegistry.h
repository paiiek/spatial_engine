// vst3/osc/PluginInstanceRegistry.h
// File-based JSON registry for in-plugin UDP discovery (ADR 0011).
// JUCE-free: zero #include <juce_...> allowed in this file.
// Writer-side GC, atomic tmpfile+rename, advisory flock LOCK_EX.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>  // pid_t

namespace spe::vst3::osc {

static constexpr uint64_t kSupportedSchemaVersion = 1;

struct InstanceEntry {
    uint32_t    instance_id;     // monotonically-assigned per-process counter
    uint16_t    port;            // UDP bind port (resolved)
    pid_t       pid;             // OS process ID
    std::string boot_id;         // content of /proc/sys/kernel/random/boot_id
    uint64_t    schema_version;  // always kSupportedSchemaVersion when written
    uint64_t    starttime{0};    // field 22 of /proc/{pid}/stat (clock ticks since boot)
};

class PluginInstanceRegistry {
public:
    // Atomically add this process's entry and return the resolved entry
    // (port may differ from requested_port if EADDRINUSE).
    // Performs writer-side GC before writing:
    //   (a) drops entries where /proc/{pid}/comm is unreadable (dead PID)
    //   (b) drops entries where boot_id differs from current boot_id
    // Refuses (returns entry with port=0) if file's schema_version >
    // kSupportedSchemaVersion.
    InstanceEntry registerSelf(uint16_t requested_port,
                               std::string_view plugin_comm);

    // Atomically remove this process's entry (called on plugin termination).
    void unregisterSelf(uint32_t instance_id);

    // Read-only: list all entries present in the file.
    // Returns empty list on parse error or schema_version mismatch.
    std::vector<InstanceEntry> listActive();

private:
    // Read current boot_id from /proc/sys/kernel/random/boot_id.
    static std::string readBootId();

    // Read field 22 (starttime, clock ticks since boot) from /proc/{pid}/stat.
    // Returns 0 if the file cannot be read (process does not exist).
    static uint64_t readPidStarttime(pid_t pid);

    // Check PID is alive and starttime matches the stored value.
    // If stored_starttime == 0 (legacy entry), falls back to comm-existence check.
    static bool pidAlive(pid_t pid, uint64_t stored_starttime = 0);

    // Low-level atomic write: takes the new JSON content and writes it via
    // tmpfile + flock + fsync + rename. Returns true on success.
    static bool atomicWrite(const std::string& registry_path,
                            const std::string& json_content);

    // Minimal JSON emit/parse for the fixed ADR-0011 schema.
    static std::string emitJson(const std::vector<InstanceEntry>& entries);

    // Parse JSON. Returns false on any syntax/schema error.
    static bool parseJson(const std::string& json,
                          std::vector<InstanceEntry>& out,
                          uint64_t& schema_version_out);
};

} // namespace spe::vst3::osc

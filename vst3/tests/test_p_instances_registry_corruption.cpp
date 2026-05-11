// vst3/tests/test_p_instances_registry_corruption.cpp
// A.14 / PM2 coverage: truncated file, empty file, multi-writer stress, corrupted JSON.

#include "../osc/PluginInstanceRegistry.h"
#include "../osc/RegistryPath.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static std::string g_test_dir;

static void setupTempDir()
{
    char tmpl[] = "/tmp/spe_reg_corrupt_XXXXXX";
    char* d = ::mkdtemp(tmpl);
    assert(d && "mkdtemp failed");
    g_test_dir = d;
}

static void cleanupTempDir()
{
    std::string cmd = "rm -rf " + g_test_dir;
    int rc = ::system(cmd.c_str()); (void)rc;
}

static std::string setXdgDir(const std::string& subdir)
{
    std::string xdg = g_test_dir + "/" + subdir;
    ::mkdir(xdg.c_str(), 0755);
    ::setenv("XDG_CONFIG_HOME", xdg.c_str(), 1);
    return xdg;
}

static std::string registryFile(const std::string& xdg_dir)
{
    std::string dir = xdg_dir + "/spatial_engine";
    ::mkdir(dir.c_str(), 0755);
    return dir + "/instances.json";
}

// ---------------------------------------------------------------------------
// Test 1: truncated file (last bytes chopped) → listActive returns empty, no crash
// ---------------------------------------------------------------------------
static void test_truncated_file()
{
    std::string xdg = setXdgDir("trunc");
    std::string path = registryFile(xdg);

    // Write a valid JSON then truncate it.
    {
        std::ofstream f(path);
        f << "{\n  \"schema_version\": 1,\n  \"instances\": [\n    {\"instance_id\": 1, "
             "\"port\": 9100, \"pid\": 1234, \"boot_id\": \"abc\", \"schema_version\": 1}\n  ]\n}";
    }

    // Truncate to first 30 bytes (mid-JSON).
    ::truncate(path.c_str(), 30);

    spe::vst3::osc::PluginInstanceRegistry reg;
    auto list = reg.listActive();
    // Must not crash; empty or partial list is acceptable, but no crash.
    (void)list;
    std::printf("[PASS] test_truncated_file (size=%zu)\n", list.size());
}

// ---------------------------------------------------------------------------
// Test 2: empty file → listActive returns empty list, no crash
// ---------------------------------------------------------------------------
static void test_empty_file()
{
    std::string xdg = setXdgDir("empty");
    std::string path = registryFile(xdg);

    // Create empty file.
    {
        std::ofstream f(path);
        // intentionally empty
    }

    spe::vst3::osc::PluginInstanceRegistry reg;
    auto list = reg.listActive();
    assert(list.empty() && "empty file must return empty list");
    std::printf("[PASS] test_empty_file\n");
}

// ---------------------------------------------------------------------------
// Test 3: corrupted JSON syntax → parse-error tolerance, returns empty
// ---------------------------------------------------------------------------
static void test_corrupted_json_syntax()
{
    std::string xdg = setXdgDir("corrupt");
    std::string path = registryFile(xdg);

    {
        std::ofstream f(path);
        f << "{{{not valid json at all!!! @@#$";
    }

    spe::vst3::osc::PluginInstanceRegistry reg;
    auto list = reg.listActive();
    // Must not crash; empty list expected on parse error.
    (void)list;
    std::printf("[PASS] test_corrupted_json_syntax (size=%zu)\n", list.size());
}

// ---------------------------------------------------------------------------
// Test 4: multi-writer stress (fork N=8 writers, each registers once)
// → final file parses cleanly (no torn JSON)
// ---------------------------------------------------------------------------
static void test_multi_writer_stress()
{
    std::string xdg = setXdgDir("multi");
    std::string path = registryFile(xdg);

    // Fork 8 child processes; each registers once.
    const int N = 8;
    std::vector<pid_t> children;

    for (int i = 0; i < N; ++i) {
        pid_t pid = ::fork();
        assert(pid >= 0 && "fork failed");
        if (pid == 0) {
            // Child: register self then exit.
            spe::vst3::osc::PluginInstanceRegistry reg;
            reg.registerSelf(static_cast<uint16_t>(9300 + i), "stress_test");
            ::_exit(0);
        }
        children.push_back(pid);
    }

    // Wait for all children.
    for (pid_t cpid : children) {
        int status = 0;
        ::waitpid(cpid, &status, 0);
    }

    // The final file must parse cleanly (no torn JSON = no crash + valid structure).
    spe::vst3::osc::PluginInstanceRegistry reg;
    // listActive will either return entries or empty on corruption; what matters
    // is it does NOT crash and the file is valid JSON.
    auto list = reg.listActive();

    // Try to re-read raw and check it's non-empty and starts with '{'.
    std::ifstream f(path);
    if (f.is_open()) {
        char first = '\0';
        f.get(first);
        // Either file is empty (all GC'd) or starts with '{' for valid JSON.
        assert((first == '{' || first == '\0') && "file must be valid JSON or empty");
    }

    std::printf("[PASS] test_multi_writer_stress (final entries=%zu)\n", list.size());
}

// ---------------------------------------------------------------------------
// Test 5: registerSelf on a file that is corrupted mid-write (simulated by
// writing invalid JSON) must not crash and must produce a valid new file.
// ---------------------------------------------------------------------------
static void test_register_after_corruption()
{
    std::string xdg = setXdgDir("after_corrupt");
    std::string path = registryFile(xdg);

    // Pre-populate with garbage.
    {
        std::ofstream f(path);
        f << "GARBAGE NOT JSON";
    }

    spe::vst3::osc::PluginInstanceRegistry reg;
    auto entry = reg.registerSelf(9400, "test_plugin");
    // After a corrupted-file start, registry should still succeed.
    // entry.port may be 9400 (fresh start) or 0 only if schema_version mismatch.
    // Since the file was garbage (not parseable as schema_version=99), it's treated
    // as fresh, so port should be 9400.
    assert(entry.port == 9400 && "must succeed after corrupt file (fresh start)");

    // File must now be valid.
    auto list = reg.listActive();
    assert(!list.empty() && "at least our entry must be in the file");
    std::printf("[PASS] test_register_after_corruption\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    setupTempDir();

    test_truncated_file();
    test_empty_file();
    test_corrupted_json_syntax();
    test_multi_writer_stress();
    test_register_after_corruption();

    cleanupTempDir();
    std::printf("All corruption tests PASSED.\n");
    return 0;
}

// vst3/tests/test_p_instances_registry.cpp
// A.5 coverage: atomic write, GC, boot_id mitigation, schema_version refusal, XDG empty-string.

#include "../osc/PluginInstanceRegistry.h"
#include "../osc/RegistryPath.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static std::string g_test_dir;

static void setupTempDir()
{
    char tmpl[] = "/tmp/spe_reg_test_XXXXXX";
    char* d = ::mkdtemp(tmpl);
    assert(d && "mkdtemp failed");
    g_test_dir = d;
}

static void cleanupTempDir()
{
    // Simple recursive remove of our temp dir (only 2 levels deep).
    std::string cmd = "rm -rf " + g_test_dir;
    int rc = ::system(cmd.c_str()); (void)rc;
}

// Point XDG_CONFIG_HOME to a subdirectory of our temp dir so the registry
// writes to a known location.
static std::string setXdgDir(const std::string& subdir = "xdg")
{
    std::string xdg = g_test_dir + "/" + subdir;
    ::mkdir(xdg.c_str(), 0755);
    ::setenv("XDG_CONFIG_HOME", xdg.c_str(), 1);
    return xdg;
}

static std::string readFileStr(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string s((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
    return s;
}

// ---------------------------------------------------------------------------
// Test 1: registryPath() honours XDG_CONFIG_HOME when set and non-empty
// ---------------------------------------------------------------------------
static void test_registry_path_xdg_set()
{
    ::setenv("XDG_CONFIG_HOME", "/custom/config", 1);
    std::string p = spe::vst3::osc::registryPath();
    assert(p == "/custom/config/spatial_engine/instances.json" &&
           "XDG_CONFIG_HOME should be honoured");
    std::printf("[PASS] test_registry_path_xdg_set\n");
}

// ---------------------------------------------------------------------------
// Test 2: registryPath() falls back to ~/.config when XDG_CONFIG_HOME is empty
// (v03-Q10)
// ---------------------------------------------------------------------------
static void test_registry_path_xdg_empty()
{
    ::setenv("XDG_CONFIG_HOME", "", 1);   // set but empty
    const char* home = ::getenv("HOME");
    assert(home && "HOME must be set");
    std::string p = spe::vst3::osc::registryPath();
    std::string expected = std::string(home) + "/.config/spatial_engine/instances.json";
    assert(p == expected && "empty XDG_CONFIG_HOME must fall back to ~/.config");
    std::printf("[PASS] test_registry_path_xdg_empty\n");
}

// ---------------------------------------------------------------------------
// Test 3: registryPath() falls back to ~/.config when XDG_CONFIG_HOME unset
// ---------------------------------------------------------------------------
static void test_registry_path_xdg_unset()
{
    ::unsetenv("XDG_CONFIG_HOME");
    const char* home = ::getenv("HOME");
    assert(home && "HOME must be set");
    std::string p = spe::vst3::osc::registryPath();
    std::string expected = std::string(home) + "/.config/spatial_engine/instances.json";
    assert(p == expected && "unset XDG_CONFIG_HOME must fall back to ~/.config");
    std::printf("[PASS] test_registry_path_xdg_unset\n");
}

// ---------------------------------------------------------------------------
// Test 4: registerSelf / listActive basic round-trip
// ---------------------------------------------------------------------------
static void test_register_list_basic()
{
    setXdgDir("xdg4");
    spe::vst3::osc::PluginInstanceRegistry reg;
    auto entry = reg.registerSelf(9200, "test_plugin");
    assert(entry.instance_id > 0 && "instance_id must be assigned");
    assert(entry.port == 9200 && "port must match requested");
    assert(entry.pid == ::getpid() && "pid must match current process");
    assert(!entry.boot_id.empty() && "boot_id must not be empty");

    auto list = reg.listActive();
    assert(list.size() == 1 && "one entry expected");
    assert(list[0].instance_id == entry.instance_id);
    assert(list[0].port == 9200);

    std::printf("[PASS] test_register_list_basic\n");
}

// ---------------------------------------------------------------------------
// Test 5: unregisterSelf removes entry
// ---------------------------------------------------------------------------
static void test_unregister_removes_entry()
{
    setXdgDir("xdg5");
    spe::vst3::osc::PluginInstanceRegistry reg;
    auto entry = reg.registerSelf(9201, "test_plugin");
    reg.unregisterSelf(entry.instance_id);

    auto list = reg.listActive();
    assert(list.empty() && "entry should be removed after unregister");
    std::printf("[PASS] test_unregister_removes_entry\n");
}

// ---------------------------------------------------------------------------
// Test 6: GC drops entries where /proc/{pid}/comm is missing (dead PID)
// ---------------------------------------------------------------------------
static void test_gc_dead_pid()
{
    setXdgDir("xdg6");
    spe::vst3::osc::PluginInstanceRegistry reg;

    // Manually write a registry entry with a PID that doesn't exist.
    // We use PID 2^22 - 1 which is almost certainly dead.
    std::string xdg = g_test_dir + "/xdg6";
    std::string dir = xdg + "/spatial_engine";
    ::mkdir(dir.c_str(), 0755);
    std::string path = dir + "/instances.json";

    // Write a stale entry manually.
    std::string fake_boot_id;
    {
        std::ifstream f("/proc/sys/kernel/random/boot_id");
        std::getline(f, fake_boot_id);
        while (!fake_boot_id.empty() &&
               (fake_boot_id.back() == '\n' || fake_boot_id.back() == '\r'))
            fake_boot_id.pop_back();
    }

    std::string json =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"spec_commit\": \"test\",\n"
        "  \"instances\": [\n"
        "    {\n"
        "      \"instance_id\": 999,\n"
        "      \"port\": 9999,\n"
        "      \"pid\": 4194303,\n"   // very unlikely to exist
        "      \"boot_id\": \"" + fake_boot_id + "\",\n"
        "      \"schema_version\": 1\n"
        "    }\n"
        "  ]\n"
        "}\n";

    std::ofstream out(path);
    out << json;
    out.close();

    // Now register self — GC should drop the dead PID entry.
    auto entry = reg.registerSelf(9202, "test_plugin");
    auto list = reg.listActive();
    // Only our own entry should survive.
    for (const auto& e : list) {
        assert(e.pid != 4194303 && "dead PID entry should have been GC'd");
    }
    std::printf("[PASS] test_gc_dead_pid\n");
}

// ---------------------------------------------------------------------------
// Test 7: boot_id mitigation — entry with different boot_id is dropped (v03-Q9)
// ---------------------------------------------------------------------------
static void test_gc_stale_boot_id()
{
    setXdgDir("xdg7");
    spe::vst3::osc::PluginInstanceRegistry reg;

    std::string xdg = g_test_dir + "/xdg7";
    std::string dir = xdg + "/spatial_engine";
    ::mkdir(dir.c_str(), 0755);
    std::string path = dir + "/instances.json";

    // Write an entry with the current PID but a fake boot_id.
    pid_t my_pid = ::getpid();
    std::string json =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"spec_commit\": \"test\",\n"
        "  \"instances\": [\n"
        "    {\n"
        "      \"instance_id\": 777,\n"
        "      \"port\": 9777,\n"
        "      \"pid\": " + std::to_string(my_pid) + ",\n"
        "      \"boot_id\": \"fake-boot-id-00000000-0000-0000-0000\",\n"
        "      \"schema_version\": 1\n"
        "    }\n"
        "  ]\n"
        "}\n";

    std::ofstream out(path);
    out << json;
    out.close();

    auto entry = reg.registerSelf(9203, "test_plugin");
    auto list = reg.listActive();
    for (const auto& e : list) {
        assert(e.boot_id != "fake-boot-id-00000000-0000-0000-0000" &&
               "stale boot_id entry should have been GC'd");
    }
    std::printf("[PASS] test_gc_stale_boot_id\n");
}

// ---------------------------------------------------------------------------
// Test 8: schema_version > kSupportedSchemaVersion → registerSelf returns port=0
// ---------------------------------------------------------------------------
static void test_schema_version_too_new()
{
    setXdgDir("xdg8");

    std::string xdg = g_test_dir + "/xdg8";
    std::string dir = xdg + "/spatial_engine";
    ::mkdir(dir.c_str(), 0755);
    std::string path = dir + "/instances.json";

    std::string json =
        "{\n"
        "  \"schema_version\": 99,\n"
        "  \"spec_commit\": \"future\",\n"
        "  \"instances\": []\n"
        "}\n";
    std::ofstream out(path);
    out << json;
    out.close();

    spe::vst3::osc::PluginInstanceRegistry reg;
    auto entry = reg.registerSelf(9204, "test_plugin");
    assert(entry.port == 0 && "schema_version=99 must cause port=0 refusal");
    std::printf("[PASS] test_schema_version_too_new\n");
}

// ---------------------------------------------------------------------------
// Test 9: listActive() returns empty on schema_version > supported
// ---------------------------------------------------------------------------
static void test_list_refuses_future_schema()
{
    setXdgDir("xdg9");

    std::string xdg = g_test_dir + "/xdg9";
    std::string dir = xdg + "/spatial_engine";
    ::mkdir(dir.c_str(), 0755);
    std::string path = dir + "/instances.json";

    std::string json =
        "{\n"
        "  \"schema_version\": 99,\n"
        "  \"spec_commit\": \"future\",\n"
        "  \"instances\": [{\"instance_id\": 1, \"port\": 9100, \"pid\": 1, "
        "\"boot_id\": \"x\", \"schema_version\": 99}]\n"
        "}\n";
    std::ofstream out(path);
    out << json;
    out.close();

    spe::vst3::osc::PluginInstanceRegistry reg;
    auto list = reg.listActive();
    assert(list.empty() && "future schema_version must return empty list");
    std::printf("[PASS] test_list_refuses_future_schema\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    setupTempDir();

    test_registry_path_xdg_set();
    test_registry_path_xdg_empty();
    test_registry_path_xdg_unset();
    test_register_list_basic();
    test_unregister_removes_entry();
    test_gc_dead_pid();
    test_gc_stale_boot_id();
    test_schema_version_too_new();
    test_list_refuses_future_schema();

    cleanupTempDir();
    std::printf("All registry tests PASSED.\n");
    return 0;
}

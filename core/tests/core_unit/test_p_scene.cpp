// core/tests/core_unit/test_p_scene.cpp
// US-001: SceneSnapshot and scene command decode tests.
#include "ipc/SceneSnapshot.h"
#include "ipc/SceneController.h"
#include "ipc/Command.h"
#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <filesystem>

using namespace spe::ipc;

// ---- helpers ----------------------------------------------------------------

static void CHECK(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

// Build a minimal OSC packet: address + ",s" type tag + padded string arg.
static std::vector<uint8_t> makeOscStringPacket(const char* addr, const char* strArg) {
    auto pad4 = [](std::size_t n) { return (n + 3) & ~std::size_t(3); };

    std::string addrStr(addr);
    std::string tagStr(",s");
    std::string argStr(strArg);

    std::size_t addrPad = pad4(addrStr.size() + 1);
    std::size_t tagPad  = pad4(tagStr.size() + 1);
    std::size_t argPad  = pad4(argStr.size() + 1);

    std::vector<uint8_t> pkt(addrPad + tagPad + argPad, 0);
    std::memcpy(pkt.data(),                    addrStr.c_str(), addrStr.size());
    std::memcpy(pkt.data() + addrPad,          tagStr.c_str(),  tagStr.size());
    std::memcpy(pkt.data() + addrPad + tagPad, argStr.c_str(),  argStr.size());
    return pkt;
}

// Build a minimal OSC packet with no arguments (just address + "," tag).
static std::vector<uint8_t> makeOscNoArgPacket(const char* addr) {
    auto pad4 = [](std::size_t n) { return (n + 3) & ~std::size_t(3); };
    std::string addrStr(addr);
    std::string tagStr(",");
    std::size_t addrPad = pad4(addrStr.size() + 1);
    std::size_t tagPad  = pad4(tagStr.size() + 1);
    std::vector<uint8_t> pkt(addrPad + tagPad, 0);
    std::memcpy(pkt.data(),           addrStr.c_str(), addrStr.size());
    std::memcpy(pkt.data() + addrPad, tagStr.c_str(),  tagStr.size());
    return pkt;
}

// ---- Test 1: toJson / fromJson round-trip with 2 objects --------------------

static void test_roundtrip() {
    SceneSnapshot ss;
    ss.name = "test_scene";

    ObjectSnapshot o1;
    o1.id = 1; o1.az_rad = 0.5f; o1.el_rad = 0.1f; o1.dist_m = 2.0f;
    o1.algorithm = 2; o1.gain_linear = 0.8f; o1.muted = false;

    ObjectSnapshot o2;
    o2.id = 2; o2.az_rad = -1.0f; o2.el_rad = 0.3f; o2.dist_m = 1.5f;
    o2.algorithm = 0; o2.gain_linear = 1.0f; o2.muted = true;

    ss.objects = {o1, o2};

    std::string json = ss.toJson();
    SceneSnapshot loaded = SceneSnapshot::fromJson(json);

    CHECK(loaded.name == "test_scene", "roundtrip: name");
    CHECK(loaded.objects.size() == 2,  "roundtrip: object count");

    CHECK(loaded.objects[0].id == 1,   "roundtrip: o1 id");
    CHECK(loaded.objects[0].muted == false, "roundtrip: o1 muted");

    CHECK(loaded.objects[1].id == 2,   "roundtrip: o2 id");
    CHECK(loaded.objects[1].muted == true, "roundtrip: o2 muted");

    // float tolerance ~1e-5
    float diff = loaded.objects[0].az_rad - 0.5f;
    if (diff < 0) diff = -diff;
    CHECK(diff < 1e-4f, "roundtrip: o1 az_rad");

    std::printf("PASS: test_roundtrip\n");
}

// ---- Test 2: /scene/save decode → SceneSave, name matches ------------------

static void test_decode_save() {
    auto pkt = makeOscStringPacket("/scene/save", "myScene");
    CommandDecoder dec;
    Command cmd = dec.decode({pkt.data(), pkt.size()});
    CHECK(cmd.tag == CommandTag::SceneSave, "decode_save: tag");
    auto& p = std::get<PayloadSceneSave>(cmd.payload);
    CHECK(std::string(p.name) == "myScene", "decode_save: name");
    std::printf("PASS: test_decode_save\n");
}

// ---- Test 3: /scene/load decode → SceneLoad, name matches ------------------

static void test_decode_load() {
    auto pkt = makeOscStringPacket("/scene/load", "myScene");
    CommandDecoder dec;
    Command cmd = dec.decode({pkt.data(), pkt.size()});
    CHECK(cmd.tag == CommandTag::SceneLoad, "decode_load: tag");
    auto& p = std::get<PayloadSceneLoad>(cmd.payload);
    CHECK(std::string(p.name) == "myScene", "decode_load: name");
    std::printf("PASS: test_decode_load\n");
}

// ---- Test 4: /scene/list decode → SceneList --------------------------------

static void test_decode_list() {
    auto pkt = makeOscNoArgPacket("/scene/list");
    CommandDecoder dec;
    Command cmd = dec.decode({pkt.data(), pkt.size()});
    CHECK(cmd.tag == CommandTag::SceneList, "decode_list: tag");
    std::printf("PASS: test_decode_list\n");
}

// ---- Test 5: SceneController save → list → load roundtrip -----------------

static void test_scene_controller_roundtrip() {
    namespace fs = std::filesystem;
    // Use a temp directory unique to this test run.
    auto tmpdir = fs::temp_directory_path() / "spe_scene_ctrl_test";
    fs::remove_all(tmpdir);
    fs::create_directories(tmpdir);

    std::string dir = tmpdir.string();
    spe::ipc::SceneController ctrl(dir);

    // Build SceneSave command.
    spe::ipc::Command saveCmd;
    saveCmd.tag = spe::ipc::CommandTag::SceneSave;
    spe::ipc::PayloadSceneSave sp{};
    std::strncpy(sp.name, "myScene", sizeof(sp.name) - 1);
    saveCmd.payload = sp;

    bool handled = ctrl.handleCommand(saveCmd);
    CHECK(handled, "controller: SceneSave handled");

    // SceneList
    spe::ipc::Command listCmd;
    listCmd.tag = spe::ipc::CommandTag::SceneList;
    listCmd.payload = spe::ipc::PayloadSceneList{};
    handled = ctrl.handleCommand(listCmd);
    CHECK(handled, "controller: SceneList handled");
    CHECK(ctrl.lastSceneList().size() == 1, "controller: list has 1 scene");
    CHECK(ctrl.lastSceneList()[0] == "myScene", "controller: scene name in list");

    // SceneLoad
    spe::ipc::Command loadCmd;
    loadCmd.tag = spe::ipc::CommandTag::SceneLoad;
    spe::ipc::PayloadSceneLoad lp{};
    std::strncpy(lp.name, "myScene", sizeof(lp.name) - 1);
    loadCmd.payload = lp;
    handled = ctrl.handleCommand(loadCmd);
    CHECK(handled, "controller: SceneLoad handled");
    CHECK(ctrl.lastLoaded().has_value(), "controller: loaded scene present");
    CHECK(ctrl.lastLoaded()->name == "myScene", "controller: loaded scene name");

    fs::remove_all(tmpdir);
    std::printf("PASS: test_scene_controller_roundtrip\n");
}

// ---- Test 6: path traversal blocked ----------------------------------------

static void test_path_traversal_blocked() {
    namespace fs = std::filesystem;
    auto tmpdir = fs::temp_directory_path() / "spe_traversal_test";
    fs::remove_all(tmpdir);
    fs::create_directories(tmpdir);

    spe::ipc::SceneSnapshot bad;
    bad.name = "../evil";
    bool ok = bad.saveToDisk(tmpdir.string());
    CHECK(!ok, "path traversal: '../evil' must be rejected");

    auto loaded = spe::ipc::SceneSnapshot::loadFromDisk(tmpdir.string(), "../evil");
    CHECK(!loaded.has_value(), "path traversal: loadFromDisk must return nullopt");

    fs::remove_all(tmpdir);
    std::printf("PASS: test_path_traversal_blocked\n");
}

// ---- Test 7: boundary cases — empty name, max length, oversize -------------

static void test_scene_name_boundaries() {
    namespace fs = std::filesystem;
    auto tmpdir = fs::temp_directory_path() / "spe_scene_bounds_test";
    fs::remove_all(tmpdir);
    fs::create_directories(tmpdir);
    const std::string dir = tmpdir.string();

    // Empty name: rejected by isSafeSceneName.
    {
        spe::ipc::SceneSnapshot ss;
        ss.name = "";
        CHECK(!ss.saveToDisk(dir), "empty name: saveToDisk must reject");
        CHECK(!spe::ipc::SceneSnapshot::loadFromDisk(dir, "").has_value(),
              "empty name: loadFromDisk must reject");
    }

    // 63-char name: accepted (max allowed).
    {
        spe::ipc::SceneSnapshot ss;
        ss.name.assign(63, 'a');
        CHECK(ss.saveToDisk(dir), "63-char name: saveToDisk must succeed");
        CHECK(spe::ipc::SceneSnapshot::loadFromDisk(dir, ss.name).has_value(),
              "63-char name: loadFromDisk must succeed");
    }

    // 64-char name: rejected (exceeds limit).
    {
        spe::ipc::SceneSnapshot ss;
        ss.name.assign(64, 'b');
        CHECK(!ss.saveToDisk(dir), "64-char name: saveToDisk must reject");
    }

    fs::remove_all(tmpdir);
    std::printf("PASS: test_scene_name_boundaries\n");
}

// ---- main ------------------------------------------------------------------

int main() {
    test_roundtrip();
    test_decode_save();
    test_decode_load();
    test_decode_list();
    test_scene_controller_roundtrip();
    test_path_traversal_blocked();
    test_scene_name_boundaries();
    std::printf("ALL PASS\n");
    return 0;
}

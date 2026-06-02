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
    o1.width_rad = 1.2f; o1.reverb_send = 0.35f;

    ObjectSnapshot o2;
    o2.id = 2; o2.az_rad = -1.0f; o2.el_rad = 0.3f; o2.dist_m = 1.5f;
    o2.algorithm = 0; o2.gain_linear = 1.0f; o2.muted = true;
    o2.width_rad = 0.7f; o2.reverb_send = 0.9f;

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

    // AC1 — width_rad / reverb_send round-trip within 1e-4 for >=2 objects.
    auto fclose = [](float a, float b) { float d = a - b; return (d < 0 ? -d : d) < 1e-4f; };
    CHECK(fclose(loaded.objects[0].width_rad,   1.2f),  "roundtrip: o1 width_rad");
    CHECK(fclose(loaded.objects[0].reverb_send, 0.35f), "roundtrip: o1 reverb_send");
    CHECK(fclose(loaded.objects[1].width_rad,   0.7f),  "roundtrip: o2 width_rad");
    CHECK(fclose(loaded.objects[1].reverb_send, 0.9f),  "roundtrip: o2 reverb_send");

    std::printf("PASS: test_roundtrip\n");
}

// ---- AC2: backward-compat — JSON lacking width/reverb keys parses as 0 ------

static void test_backward_compat() {
    // An "old" scene file with NO width_rad / reverb_send keys.
    const std::string old_json =
        "{\"name\":\"old\",\"objects\":["
        "{\"id\":5,\"az_rad\":0.25,\"el_rad\":0,\"dist_m\":2,"
        "\"algorithm\":1,\"gain_linear\":0.5,\"muted\":false}]}";

    SceneSnapshot loaded = SceneSnapshot::fromJson(old_json);
    CHECK(loaded.objects.size() == 1, "compat: object loaded");
    CHECK(loaded.objects[0].id == 5,  "compat: id parsed");
    CHECK(loaded.objects[0].width_rad   == 0.f, "compat: missing width_rad → 0");
    CHECK(loaded.objects[0].reverb_send == 0.f, "compat: missing reverb_send → 0");
    std::printf("PASS: test_backward_compat\n");
}

// ---- AC5: /obj/dsp decoder param routing + reject ---------------------------

// Round-trip an /obj/dsp via the production encoder (",ii" seq/id + iif payload)
// so the wire format matches what the engine actually sends/receives.
static Command decodeObjDsp(int obj_id, int param, float value) {
    Command in;
    in.tag = CommandTag::ObjDsp;
    PayloadObjDsp p;
    p.obj_id = static_cast<uint32_t>(obj_id);
    p.param  = static_cast<PayloadObjDsp::Param>(param);
    p.value  = value;
    in.payload = p;
    std::vector<uint8_t> pkt;
    CommandDecoder dec;
    bool ok = dec.encode(in, pkt, WireDialect::Legacy);
    CHECK(ok, "objdsp: encode succeeded");
    return dec.decode({pkt.data(), pkt.size()});
}

// Encode rejects out-of-range params (variant only holds 0..7); for the reject
// arm build the wire packet by hand at the desired (invalid) param value.
static std::vector<uint8_t> makeObjDspWire(int obj_id, int param, float value) {
    // Format: addr "/obj/dsp", tags ",iiiif" (seq, id, obj_id, param, value).
    auto pad4 = [](std::size_t n) { return (n + 3) & ~std::size_t(3); };
    std::string addrStr("/obj/dsp");
    std::string tagStr(",iiiif");
    std::size_t addrPad = pad4(addrStr.size() + 1);
    std::size_t tagPad  = pad4(tagStr.size() + 1);
    std::vector<uint8_t> pkt(addrPad + tagPad + 20, 0);
    std::memcpy(pkt.data(),           addrStr.c_str(), addrStr.size());
    std::memcpy(pkt.data() + addrPad, tagStr.c_str(),  tagStr.size());
    auto putU32be = [](uint8_t* p, uint32_t v) {
        p[0]=uint8_t(v>>24); p[1]=uint8_t(v>>16); p[2]=uint8_t(v>>8); p[3]=uint8_t(v);
    };
    uint8_t* a = pkt.data() + addrPad + tagPad;
    putU32be(a + 0, 0u);                              // seq
    putU32be(a + 4, 0u);                              // id
    putU32be(a + 8, static_cast<uint32_t>(obj_id));   // obj_id
    putU32be(a + 12, static_cast<uint32_t>(param));   // param
    uint32_t fu; std::memcpy(&fu, &value, 4); putU32be(a + 16, fu); // value
    return pkt;
}

static void test_decode_objdsp_param7_and_reject() {
    // param 7 (Width) must now decode to ObjDsp{param=Width}, NOT mis-route to EqLow.
    {
        Command cmd = decodeObjDsp(3, 7, 1.1f);
        CHECK(cmd.tag == CommandTag::ObjDsp, "objdsp7: tag ObjDsp");
        auto& p = std::get<PayloadObjDsp>(cmd.payload);
        CHECK(p.param == PayloadObjDsp::Param::Width, "objdsp7: param == Width(7)");
        CHECK(p.obj_id == 3u, "objdsp7: obj_id");
        float d = p.value - 1.1f; if (d < 0) d = -d;
        CHECK(d < 1e-4f, "objdsp7: value");
    }
    // param 6 (ReverbSend) still decodes correctly.
    {
        Command cmd = decodeObjDsp(2, 6, 0.42f);
        CHECK(cmd.tag == CommandTag::ObjDsp, "objdsp6: tag ObjDsp");
        auto& p = std::get<PayloadObjDsp>(cmd.payload);
        CHECK(p.param == PayloadObjDsp::Param::ReverbSend, "objdsp6: param == ReverbSend(6)");
    }
    // Out-of-range param (9) must reject → Unknown (no silent EqLow write).
    {
        CommandDecoder dec;
        auto pkt = makeObjDspWire(1, 9, 0.5f);
        Command cmd = dec.decode({pkt.data(), pkt.size()});
        CHECK(cmd.tag == CommandTag::Unknown, "objdsp9: out-of-range → Unknown");
    }
    std::printf("PASS: test_decode_objdsp_param7_and_reject\n");
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
    test_backward_compat();
    test_decode_objdsp_param7_and_reject();
    test_decode_save();
    test_decode_load();
    test_decode_list();
    test_scene_controller_roundtrip();
    test_path_traversal_blocked();
    test_scene_name_boundaries();
    std::printf("ALL PASS\n");
    return 0;
}

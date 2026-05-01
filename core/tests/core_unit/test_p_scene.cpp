// core/tests/core_unit/test_p_scene.cpp
// US-001: SceneSnapshot and scene command decode tests.
#include "ipc/SceneSnapshot.h"
#include "ipc/Command.h"
#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cstring>
#include <cstdio>

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

// ---- main ------------------------------------------------------------------

int main() {
    test_roundtrip();
    test_decode_save();
    test_decode_load();
    test_decode_list();
    std::printf("ALL PASS\n");
    return 0;
}

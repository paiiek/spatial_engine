// test_p_max_objects.cpp — US-002: MAX_OBJECTS scale-up validation
//
// 1. Compile-time assert: MAX_OBJECTS == 64
// 2. ObjMove encode→decode roundtrip for obj_id 0..63: all succeed
// 3. obj_id == 64 (out-of-range): StateModel.apply() returns false

#include "core/Constants.h"
#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"
#include "ipc/StateModel.h"

#include <cassert>
#include <cstdio>

static_assert(spe::MAX_OBJECTS == 64,
              "MAX_OBJECTS must be 64 (US-002)");

using namespace spe::ipc;

int main() {
    // --- Test 1: compile-time constant already asserted above ---
    std::printf("[PASS] static_assert MAX_OBJECTS == 64\n");

    // --- Test 2: obj_id 0..63 all encode/decode as ObjMove ---
    CommandDecoder dec;
    int failures = 0;

    for (int id = 0; id < spe::MAX_OBJECTS; ++id) {
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = static_cast<uint32_t>(id + 1);
        cmd.id  = static_cast<uint32_t>(id);
        PayloadObjMove p;
        p.obj_id = static_cast<uint32_t>(id);
        p.az_rad = 1.0f;
        p.el_rad = 0.5f;
        p.dist_m = 2.0f;
        cmd.payload = p;

        std::vector<uint8_t> buf;
        bool enc_ok = dec.encode(cmd, buf);
        if (!enc_ok) {
            std::printf("[FAIL] encode failed for obj_id=%d\n", id);
            ++failures;
            continue;
        }

        Command rt = dec.decode(std::span<const uint8_t>(buf));
        if (rt.tag != CommandTag::ObjMove) {
            std::printf("[FAIL] obj_id=%d decoded tag=%d (expected ObjMove)\n",
                        id, static_cast<int>(rt.tag));
            ++failures;
            continue;
        }
        auto& rp = std::get<PayloadObjMove>(rt.payload);
        if (static_cast<int>(rp.obj_id) != id) {
            std::printf("[FAIL] obj_id=%d: decoded obj_id=%u\n", id, rp.obj_id);
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("[PASS] obj_id 0..63 all encode/decode as ObjMove\n");
    }

    // --- Test 3: obj_id == MAX_OBJECTS (64) rejected by StateModel ---
    {
        StateModel sm;
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = 1;
        cmd.id  = 999;
        PayloadObjMove p;
        p.obj_id = static_cast<uint32_t>(spe::MAX_OBJECTS); // 64 — out of range
        p.az_rad = 0.f;
        p.el_rad = 0.f;
        p.dist_m = 1.f;
        cmd.payload = p;

        bool accepted = sm.apply(cmd);
        if (!accepted) {
            std::printf("[PASS] obj_id=64 rejected by StateModel\n");
        } else {
            std::printf("[FAIL] obj_id=64 was accepted by StateModel (should be rejected)\n");
            ++failures;
        }
    }

    if (failures > 0) {
        std::printf("[RESULT] FAIL (%d failures)\n", failures);
        return 1;
    }
    std::printf("[RESULT] PASS\n");
    return 0;
}

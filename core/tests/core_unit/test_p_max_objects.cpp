// test_p_max_objects.cpp — US-002 / v0.9 Lane C: MAX_OBJECTS scale-up validation
//
// 1. Compile-time assert: MAX_OBJECTS tracks the configured SPE_MAX_OBJECTS
//    (cap-agnostic — passes at both the 64 and 128 builds).
// 2. ObjMove encode→decode roundtrip for obj_id 0..MAX_OBJECTS-1: all succeed
// 3. Boundary: obj_id == MAX_OBJECTS (out-of-range) rejected by StateModel,
//    obj_id == MAX_OBJECTS-1 (in-range) accepted.

#include "core/Constants.h"
#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"
#include "ipc/StateModel.h"

#include <cassert>
#include <cstdio>

// v0.9 Lane C: value-agnostic — the cap is whatever the cmake option configured
// (SPE_MAX_OBJECTS ∈ {64,128}). MUST NOT hard-pin 64 or the 128 build won't
// compile.
static_assert(spe::MAX_OBJECTS == SPE_MAX_OBJECTS,
              "MAX_OBJECTS must equal the configured SPE_MAX_OBJECTS");
static_assert(spe::MAX_OBJECTS == 64 || spe::MAX_OBJECTS == 128,
              "MAX_OBJECTS must be 64 or 128 (v0.9 Lane C)");

using namespace spe::ipc;

int main() {
    // --- Test 1: compile-time constant already asserted above ---
    std::printf("[PASS] static_assert MAX_OBJECTS == %d (configured cap)\n",
                spe::MAX_OBJECTS);

    // --- Test 2: obj_id 0..MAX_OBJECTS-1 all encode/decode as ObjMove ---
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
        std::printf("[PASS] obj_id 0..%d all encode/decode as ObjMove\n",
                    spe::MAX_OBJECTS - 1);
    }

    // --- Test 3: boundary semantics (cap-relative — correct at 64 AND 128) ---
    // obj_id == MAX_OBJECTS is out-of-range → StateModel rejects.
    {
        StateModel sm;
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = 1;
        cmd.id  = 999;
        PayloadObjMove p;
        p.obj_id = static_cast<uint32_t>(spe::MAX_OBJECTS); // out of range
        p.az_rad = 0.f;
        p.el_rad = 0.f;
        p.dist_m = 1.f;
        cmd.payload = p;

        bool accepted = sm.apply(cmd);
        if (!accepted) {
            std::printf("[PASS] obj_id=%d (==MAX_OBJECTS) rejected by StateModel\n",
                        spe::MAX_OBJECTS);
        } else {
            std::printf("[FAIL] obj_id=%d (==MAX_OBJECTS) was accepted (should be rejected)\n",
                        spe::MAX_OBJECTS);
            ++failures;
        }
    }

    // obj_id == MAX_OBJECTS-1 is the highest in-range id → StateModel accepts.
    {
        StateModel sm;
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = 1;
        cmd.id  = 998;
        PayloadObjMove p;
        p.obj_id = static_cast<uint32_t>(spe::MAX_OBJECTS - 1); // highest in-range
        p.az_rad = 0.f;
        p.el_rad = 0.f;
        p.dist_m = 1.f;
        cmd.payload = p;

        bool accepted = sm.apply(cmd);
        if (accepted) {
            std::printf("[PASS] obj_id=%d (==MAX_OBJECTS-1) accepted by StateModel\n",
                        spe::MAX_OBJECTS - 1);
        } else {
            std::printf("[FAIL] obj_id=%d (==MAX_OBJECTS-1) was rejected (should be accepted)\n",
                        spe::MAX_OBJECTS - 1);
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

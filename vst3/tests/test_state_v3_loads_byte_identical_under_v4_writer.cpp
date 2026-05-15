// vst3/tests/test_state_v3_loads_byte_identical_under_v4_writer.cpp
// v0.4 P0 — merge-gate test (per Architect r2 amendment A6).
//
// What this guards: a v0.3 user session (state v3 blob) MUST load through
// the new v0.4 reader and produce a parameter state that is byte-identical
// (`memcmp`) when re-emitted via the v4 writer and reloaded into a fresh
// processor.
//
// Why: the v3→v4 schema migration is a one-way street. If the v4 reader
// silently drops a v3 byte or the writer corrupts a float, every saved
// v0.3 session in the wild breaks on first open in v0.4. This test fails
// loudly at PR review time.
//
// The plan permits in-test construction of the v3 blob (no committed
// fixture file required). We use the same byte layout the v3 writer
// produced in v0.3.1 (40 bytes, magic 'SPE1', version=3, 8 floats).

#include "SpatialEngineProcessor.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int32  kMagicLegacy = 0x31455053; // 'SPE1' LE (v1/v2/v3)
static constexpr int32  kMagicV4     = 0x34455053; // 'SPE4' LE
static constexpr int    kStateBytesV3 = 40;

static MemoryStream* makeStream(const uint8_t* buf, int len) {
    MemoryStream* ms = new MemoryStream();
    int32 written = 0;
    ms->write(const_cast<uint8_t*>(buf), len, &written);
    int64 res = 0;
    ms->seek(0, IBStream::kIBSeekSet, &res);
    return ms;
}

static void setupProc(spe::vst3::SpatialEngineProcessor& proc) {
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = 48000.0;
    proc.setupProcessing(setup);
    proc.setActive(true);
}

// Construct a v3 blob (40 bytes) matching the v0.3.1 writer's byte layout.
static std::vector<uint8_t> buildV3Blob(const float vals[8]) {
    std::vector<uint8_t> out(kStateBytesV3, 0);
    int32 magic = kMagicLegacy;
    uint16 version = 3, nparams = 8;
    std::memcpy(out.data() + 0, &magic,   4);
    std::memcpy(out.data() + 4, &version, 2);
    std::memcpy(out.data() + 6, &nparams, 2);
    for (int i = 0; i < 8; ++i)
        std::memcpy(out.data() + 8 + i * 4, &vals[i], 4);
    return out;
}

// Drain proc.getState into a heap-allocated buffer.
static std::vector<uint8_t> dumpState(spe::vst3::SpatialEngineProcessor& proc) {
    MemoryStream* ms = new MemoryStream();
    proc.getState(ms);
    int64 end = 0;
    ms->seek(0, IBStream::kIBSeekEnd, &end);
    int64 zero = 0;
    ms->seek(0, IBStream::kIBSeekSet, &zero);
    std::vector<uint8_t> out(static_cast<size_t>(end));
    if (end > 0) {
        int32 nr = 0;
        ms->read(out.data(), static_cast<int32>(end), &nr);
        out.resize(static_cast<size_t>(nr));
    }
    ms->release();
    return out;
}

// Locate engine_core (0x0001) section in a v4 blob and return its 32-byte
// payload pointer (or nullptr if missing/short).
static const uint8_t* findEngineCore(const std::vector<uint8_t>& blob) {
    if (blob.size() < 8) return nullptr;
    int32 magic = 0;
    std::memcpy(&magic, blob.data() + 0, 4);
    if (magic != kMagicV4) return nullptr;
    uint16 sc = 0;
    std::memcpy(&sc, blob.data() + 6, 2);
    size_t off = 8;
    for (uint16 s = 0; s < sc; ++s) {
        if (off + 6 > blob.size()) return nullptr;
        uint16 id = 0; uint32 len = 0;
        std::memcpy(&id, blob.data() + off + 0, 2);
        std::memcpy(&len, blob.data() + off + 2, 4);
        off += 6;
        if (off + len > blob.size()) return nullptr;
        if (id == 0x0001 && len >= 32) return blob.data() + off;
        off += len;
    }
    return nullptr;
}

int main() {
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; std::printf("PASS %s\n", name); }
        else      { ++fail; std::fprintf(stderr, "FAIL %s\n", name); }
    };

    // Synthetic v3 user session — 8 distinct param values incl. kMute=0.75.
    const float kV3Vals[8] = {0.30f, 0.70f, 0.20f, 0.80f, 0.50f, 0.25f, 0.0f, 0.75f};

    // ----------------------------------------------------------------------
    // Step 1: load v3 into a fresh proc1 via the v4-capable reader.
    // ----------------------------------------------------------------------
    spe::vst3::SpatialEngineProcessor proc1;
    setupProc(proc1);
    {
        auto v3 = buildV3Blob(kV3Vals);
        MemoryStream* ms = makeStream(v3.data(), static_cast<int>(v3.size()));
        tresult r = proc1.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "merge_gate_v3_blob_load_ok");
    }

    // Capture engine_core payload from proc1's v4 emission.
    std::vector<uint8_t> blob1 = dumpState(proc1);
    const uint8_t* core1 = findEngineCore(blob1);
    CHECK(core1 != nullptr, "merge_gate_proc1_engine_core_present");

    // Verify proc1's engine_core floats are byte-identical to the v3 input.
    bool all_match_input = true;
    for (int i = 0; i < 8 && core1; ++i) {
        float got = 0.f;
        std::memcpy(&got, core1 + i * 4, 4);
        if (std::memcmp(&got, &kV3Vals[i], 4) != 0) all_match_input = false;
    }
    CHECK(all_match_input, "merge_gate_v3_floats_byte_identical_after_v4_load");

    // ----------------------------------------------------------------------
    // Step 2: load the v4 blob (proc1's emission) into proc2.
    // ----------------------------------------------------------------------
    spe::vst3::SpatialEngineProcessor proc2;
    setupProc(proc2);
    {
        MemoryStream* ms = makeStream(blob1.data(), static_cast<int>(blob1.size()));
        tresult r = proc2.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "merge_gate_v4_reload_into_proc2_ok");
    }

    std::vector<uint8_t> blob2 = dumpState(proc2);
    const uint8_t* core2 = findEngineCore(blob2);
    CHECK(core2 != nullptr, "merge_gate_proc2_engine_core_present");

    // ----------------------------------------------------------------------
    // MERGE GATE: engine_core payload of proc2 (after v3→v4→v4 round-trip)
    // must be byte-identical to proc1's engine_core (after v3→v4).
    // ----------------------------------------------------------------------
    bool bytewise_equal =
        (core1 != nullptr && core2 != nullptr
         && std::memcmp(core1, core2, 32) == 0);
    CHECK(bytewise_equal, "MERGE_GATE_engine_core_byte_identical");

    std::printf("merge_gate: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

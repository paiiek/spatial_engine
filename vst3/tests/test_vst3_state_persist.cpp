// vst3/tests/test_vst3_state_persist.cpp
// Step 3.4 — State persistence gate: 12 assertions.
// Uses SDK MemoryStream (whitelisted: memorystream.cpp).
//
// State format (32 bytes, LE):
//   [0-3]  magic  = 0x31455053 ('SPE1')
//   [4-5]  version uint16 = 1
//   [6-7]  param_count uint16 = 6
//   [8-31] 6 x float32 normalized values
//
// Assertions:
//   1-6:   v1 roundtrip: 6 norm values match within 1e-5 tolerance
//   7:     version=2 fallback -> defaults retained (norm_values unchanged)
//   8:     empty state fallback -> defaults retained
//   9:     magic mismatch ('XYZ1') -> defaults retained
//  10:     param_count=7 mismatch -> defaults retained
//  11:     truncated stream (16 bytes) -> defaults retained
//  12:     getState returns exactly 32 bytes written

#include "SpatialEngineProcessor.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static constexpr int kStateBytes = 32;
static constexpr int32 kMagicOk  = 0x31455053; // 'SPE1' LE

// Build a valid 32-byte state buffer with given values
static void buildStateBuf(uint8_t buf[kStateBytes], const float vals[6],
                          int32 magic = kMagicOk,
                          uint16_t version = 1,
                          uint16_t nparams = 6)
{
    std::memcpy(buf + 0, &magic,   4);
    std::memcpy(buf + 4, &version, 2);
    std::memcpy(buf + 6, &nparams, 2);
    for (int i = 0; i < 6; ++i)
        std::memcpy(buf + 8 + i * 4, &vals[i], 4);
}

// Write buf into a MemoryStream positioned at start, then rewind to 0.
static MemoryStream* makeStream(const uint8_t* buf, int len)
{
    // MemoryStream default ctor: writable, starts empty
    MemoryStream* ms = new MemoryStream();
    int32 written = 0;
    ms->write(const_cast<uint8_t*>(buf), len, &written);
    // Rewind to start so setState can read from position 0
    int64 res = 0;
    ms->seek(0, IBStream::kIBSeekSet, &res);
    return ms;
}

// Read norm_values from processor via getState (roundtrip helper)
static void extractNorms(spe::vst3::SpatialEngineProcessor& proc, float out[6])
{
    MemoryStream* ms = new MemoryStream();
    proc.getState(ms);
    int64 res = 0;
    ms->seek(0, IBStream::kIBSeekSet, &res);
    uint8_t buf[kStateBytes]{};
    int32 nr = 0;
    ms->read(buf, kStateBytes, &nr);
    ms->release();
    for (int i = 0; i < 6; ++i)
        std::memcpy(&out[i], buf + 8 + i * 4, 4);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; printf("PASS %s\n", name); }
        else       { ++fail; fprintf(stderr, "FAIL %s\n", name); }
    };

    // Known defaults: all 0.f (processor initializes norm_values_[6]{} = 0)
    static const float kDefaults[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};

    // Test values for roundtrip
    static const float kTestVals[6] = {0.1f, 0.2f, 0.35f, 0.5f, 0.75f, 0.9f};

    // -----------------------------------------------------------------------
    // Assertions 1-6: v1 roundtrip
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        // Setup required before setState dispatches params
        ProcessSetup setup{};
        setup.processMode        = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 512;
        setup.sampleRate         = 48000.0;
        proc.setupProcessing(setup);

        uint8_t buf[kStateBytes];
        buildStateBuf(buf, kTestVals);

        MemoryStream* ms = makeStream(buf, kStateBytes);
        proc.setState(ms);
        ms->release();

        float got[6];
        extractNorms(proc, got);

        for (int i = 0; i < 6; ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "roundtrip_param_%d", i);
            CHECK(std::fabs(got[i] - kTestVals[i]) < 1e-5f, name);
        }
    }

    // -----------------------------------------------------------------------
    // Assertion 7: version=2 fallback -> defaults retained
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        uint8_t buf[kStateBytes];
        buildStateBuf(buf, kTestVals, kMagicOk, /*version=*/2, 6);

        MemoryStream* ms = makeStream(buf, kStateBytes);
        proc.setState(ms);
        ms->release();

        float got[6];
        extractNorms(proc, got);
        bool all_default = true;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(got[i] - kDefaults[i]) >= 1e-5f) all_default = false;
        CHECK(all_default, "7_version2_fallback_defaults");
    }

    // -----------------------------------------------------------------------
    // Assertion 8: empty state -> defaults retained
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        MemoryStream* ms = new MemoryStream(); // empty, position 0
        proc.setState(ms);
        ms->release();

        float got[6];
        extractNorms(proc, got);
        bool all_default = true;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(got[i] - kDefaults[i]) >= 1e-5f) all_default = false;
        CHECK(all_default, "8_empty_state_defaults");
    }

    // -----------------------------------------------------------------------
    // Assertion 9: magic mismatch ('XYZ1') -> defaults retained
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        uint8_t buf[kStateBytes];
        int32 bad_magic = 0x315A5958; // 'XYZ1'
        buildStateBuf(buf, kTestVals, bad_magic, 1, 6);

        MemoryStream* ms = makeStream(buf, kStateBytes);
        proc.setState(ms);
        ms->release();

        float got[6];
        extractNorms(proc, got);
        bool all_default = true;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(got[i] - kDefaults[i]) >= 1e-5f) all_default = false;
        CHECK(all_default, "9_magic_mismatch_defaults");
    }

    // -----------------------------------------------------------------------
    // Assertion 10: param_count=7 mismatch -> defaults retained
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        uint8_t buf[kStateBytes];
        buildStateBuf(buf, kTestVals, kMagicOk, 1, /*nparams=*/7);

        MemoryStream* ms = makeStream(buf, kStateBytes);
        proc.setState(ms);
        ms->release();

        float got[6];
        extractNorms(proc, got);
        bool all_default = true;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(got[i] - kDefaults[i]) >= 1e-5f) all_default = false;
        CHECK(all_default, "10_param_count_mismatch_defaults");
    }

    // -----------------------------------------------------------------------
    // Assertion 11: truncated stream (16 bytes) -> defaults retained
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        uint8_t buf[16];
        // Write valid magic/version/count but only 16 bytes total
        int32 magic = kMagicOk;
        uint16_t v = 1, n = 6;
        std::memcpy(buf + 0, &magic, 4);
        std::memcpy(buf + 4, &v,     2);
        std::memcpy(buf + 6, &n,     2);
        std::memset(buf + 8, 0,      8); // only 8 bytes of param data, total 16

        MemoryStream* ms = makeStream(buf, 16);
        proc.setState(ms);
        ms->release();

        float got[6];
        extractNorms(proc, got);
        bool all_default = true;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(got[i] - kDefaults[i]) >= 1e-5f) all_default = false;
        CHECK(all_default, "11_truncated_stream_defaults");
    }

    // -----------------------------------------------------------------------
    // Assertion 12: getState writes exactly 32 bytes
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;

        MemoryStream* ms = new MemoryStream();
        tresult r = proc.getState(ms);

        int64 pos = 0;
        ms->seek(0, IBStream::kIBSeekEnd, &pos);
        ms->release();

        CHECK(r == kResultOk && pos == 32, "12_getState_32_bytes");
    }

    printf("state_persist: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

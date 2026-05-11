// vst3/tests/test_vst3_state_persist.cpp
// C2B postmortem S6 — State persistence: 19 assertions (was 12).
// State format v2 (36 bytes, LE):
//   [0-3]  magic  = 0x31455053 ('SPE1')
//   [4-5]  version uint16 = 2
//   [6-7]  param_count uint16 = 7
//   [8-35] 7 x float32 normalized values (incl. bypass at [6])
//
// v1 format (32 bytes): version=1, param_count=6, 6 floats.
// Assertions 1-12 (existing, #12 renamed to 12_getState_36_bytes).
// Assertions 13-19: new negative-path / upgrade coverage (S6 + A5/A8/A10).
#include "SpatialEngineProcessor.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int   kStateBytesV1 = 32;
static constexpr int   kStateBytesV2 = 36;
static constexpr int   kStateBytesV3 = 40;  // C4-S7: writer now emits v3
static constexpr int32 kMagicOk      = 0x31455053; // 'SPE1' LE

// Build a v1 32-byte state buffer
static void buildV1Buf(uint8_t buf[kStateBytesV1], const float vals[6],
                       int32 magic = kMagicOk,
                       uint16_t version = 1, uint16_t nparams = 6)
{
    std::memcpy(buf + 0, &magic,   4);
    std::memcpy(buf + 4, &version, 2);
    std::memcpy(buf + 6, &nparams, 2);
    for (int i = 0; i < 6; ++i) std::memcpy(buf + 8 + i*4, &vals[i], 4);
}

// Build a v2 36-byte state buffer (7 floats)
static void buildV2Buf(uint8_t buf[kStateBytesV2], const float vals[7],
                       int32 magic = kMagicOk,
                       uint16_t version = 2, uint16_t nparams = 7)
{
    std::memcpy(buf + 0, &magic,   4);
    std::memcpy(buf + 4, &version, 2);
    std::memcpy(buf + 6, &nparams, 2);
    for (int i = 0; i < 7; ++i) std::memcpy(buf + 8 + i*4, &vals[i], 4);
}

static MemoryStream* makeStream(const uint8_t* buf, int len)
{
    MemoryStream* ms = new MemoryStream();
    int32 written = 0;
    ms->write(const_cast<uint8_t*>(buf), len, &written);
    int64 res = 0;
    ms->seek(0, IBStream::kIBSeekSet, &res);
    return ms;
}

// Extract raw 7 floats from getState output.
// C4-S7: writer now emits v3 (40 bytes); floats 0-6 are at same offsets as v2.
static void extractNormsV2(spe::vst3::SpatialEngineProcessor& proc, float out[7])
{
    MemoryStream* ms = new MemoryStream();
    proc.getState(ms);
    int64 res = 0;
    ms->seek(0, IBStream::kIBSeekSet, &res);
    uint8_t buf[kStateBytesV3]{};
    int32 nr = 0;
    ms->read(buf, kStateBytesV3, &nr);
    ms->release();
    for (int i = 0; i < 7; ++i) std::memcpy(&out[i], buf + 8 + i*4, 4);
}

static void setupProc(spe::vst3::SpatialEngineProcessor& proc)
{
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = 48000.0;
    proc.setupProcessing(setup);
    proc.setActive(true);
}

int main()
{
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; printf("PASS %s\n", name); }
        else       { ++fail; fprintf(stderr, "FAIL %s\n", name); }
    };

    static const float kDefaults7[7] = {0.f,0.f,0.f,0.f,0.f,0.f,0.f};
    static const float kTestVals[6]  = {0.1f,0.2f,0.35f,0.5f,0.75f,0.9f};
    static const float kTestVals7[7] = {0.1f,0.2f,0.35f,0.5f,0.75f,0.9f,1.0f};

    // -------------------------------------------------------------------
    // Assertions 1-6: v1 roundtrip (6 params, write v1 -> setState -> getState v2)
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        uint8_t buf[kStateBytesV1];
        buildV1Buf(buf, kTestVals);
        MemoryStream* ms = makeStream(buf, kStateBytesV1);
        proc.setState(ms); ms->release();

        float got[7];
        extractNormsV2(proc, got);
        for (int i = 0; i < 6; ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "roundtrip_param_%d", i);
            CHECK(std::fabs(got[i] - kTestVals[i]) < 1e-5f, name);
        }
    }

    // -------------------------------------------------------------------
    // Assertion 7: version=2 nparams=6 (inconsistent) -> defaults retained
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        uint8_t buf[kStateBytesV1];
        buildV1Buf(buf, kTestVals, kMagicOk, /*version=*/2, 6);
        MemoryStream* ms = makeStream(buf, kStateBytesV1);
        proc.setState(ms); ms->release();

        float got[7];
        extractNormsV2(proc, got);
        bool all_default = true;
        for (int i = 0; i < 7; ++i)
            if (std::fabs(got[i] - kDefaults7[i]) >= 1e-5f) all_default = false;
        CHECK(all_default, "7_version2_nparams6_inconsistent_defaults");
    }

    // -------------------------------------------------------------------
    // Assertion 8: empty state -> defaults retained
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        MemoryStream* ms = new MemoryStream();
        proc.setState(ms); ms->release();
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i = 0; i < 7; ++i) if (std::fabs(got[i] - kDefaults7[i]) >= 1e-5f) ok=false;
        CHECK(ok, "8_empty_state_defaults");
    }

    // -------------------------------------------------------------------
    // Assertion 9: magic mismatch -> defaults retained
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        uint8_t buf[kStateBytesV1];
        int32 bad_magic = 0x315A5958; // 'XYZ1'
        buildV1Buf(buf, kTestVals, bad_magic, 1, 6);
        MemoryStream* ms = makeStream(buf, kStateBytesV1);
        proc.setState(ms); ms->release();
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i = 0; i < 7; ++i) if (std::fabs(got[i] - kDefaults7[i]) >= 1e-5f) ok=false;
        CHECK(ok, "9_magic_mismatch_defaults");
    }

    // -------------------------------------------------------------------
    // Assertion 10: v1 header with nparams=7 (v1 only allows 6) -> defaults
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        uint8_t buf[kStateBytesV1];
        buildV1Buf(buf, kTestVals, kMagicOk, 1, /*nparams=*/7);
        MemoryStream* ms = makeStream(buf, kStateBytesV1);
        proc.setState(ms); ms->release();
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i = 0; i < 7; ++i) if (std::fabs(got[i] - kDefaults7[i]) >= 1e-5f) ok=false;
        CHECK(ok, "10_param_count_mismatch_defaults");
    }

    // -------------------------------------------------------------------
    // Assertion 11: truncated stream (16 bytes) -> defaults retained
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        uint8_t buf[16];
        int32 magic = kMagicOk; uint16_t v=1, n=6;
        std::memcpy(buf+0,&magic,4); std::memcpy(buf+4,&v,2); std::memcpy(buf+6,&n,2);
        std::memset(buf+8,0,8);
        MemoryStream* ms = makeStream(buf, 16);
        proc.setState(ms); ms->release();
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i = 0; i < 7; ++i) if (std::fabs(got[i] - kDefaults7[i]) >= 1e-5f) ok=false;
        CHECK(ok, "11_truncated_stream_defaults");
    }

    // -------------------------------------------------------------------
    // Assertion 12 (renamed A8): getState writes exactly 36 bytes (v2)
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        MemoryStream* ms = new MemoryStream();
        tresult r = proc.getState(ms);
        int64 pos = 0;
        ms->seek(0, IBStream::kIBSeekEnd, &pos);
        ms->release();
        // C4-S7: writer now emits v3 (40 bytes)
        CHECK(r == kResultOk && pos == 40, "12_getState_40_bytes");
    }

    // -------------------------------------------------------------------
    // Assertion 13: fresh-instance bit-equal after same v2 setState
    // Two fresh procs, same v2 buf, same 512-sample block -> outputs bit-equal
    // -------------------------------------------------------------------
    {
        uint8_t buf[kStateBytesV2];
        buildV2Buf(buf, kTestVals7);

        auto runProc = [&](float* outBuf) {
            spe::vst3::SpatialEngineProcessor proc;
            setupProc(proc);
            MemoryStream* ms = makeStream(buf, kStateBytesV2);
            proc.setState(ms); ms->release();

            static float in0[512]={}, in1[512]={};
            static float out0[512]={}, out1[512]={};
            float* inB[2]={in0,in1}; float* outB[2]={out0,out1};
            AudioBusBuffers ib{}, ob{};
            ib.numChannels=2; ib.channelBuffers32=inB;
            ob.numChannels=2; ob.channelBuffers32=outB;
            ProcessData data{};
            data.processMode=kRealtime; data.symbolicSampleSize=kSample32;
            data.numInputs=1; data.numOutputs=1;
            data.inputs=&ib; data.outputs=&ob; data.numSamples=512;
            proc.process(data);
            std::memcpy(outBuf, out0, 512*sizeof(float));
        };

        float outA[512], outB[512];
        runProc(outA);
        runProc(outB);

        bool bit_equal = (std::memcmp(outA, outB, 512*sizeof(float)) == 0);
        CHECK(bit_equal, "13_fresh_instance_bit_equal");
    }

    // -------------------------------------------------------------------
    // Assertion 14: oversize stream (64 bytes, valid v2 hdr + trailing) ->
    // first 36 bytes parsed, no crash, all 7 norms restored
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        uint8_t buf[64]{};
        buildV2Buf(buf, kTestVals7);
        // trailing 28 bytes are garbage (but stream has 64 total)
        for (int i = 36; i < 64; ++i) buf[i] = 0xFF;
        MemoryStream* ms = makeStream(buf, 64);
        proc.setState(ms); ms->release();
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i = 0; i < 7; ++i)
            if (std::fabs(got[i] - kTestVals7[i]) >= 1e-5f) ok=false;
        CHECK(ok, "14_oversize_stream_36plus_bytes");
    }

    // -------------------------------------------------------------------
    // Assertion 15: concurrent setState/getState 1000-iter (A11 reduced)
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);

        uint8_t v2buf[kStateBytesV2];
        buildV2Buf(v2buf, kTestVals7);

        std::atomic<bool> done{false};
        std::atomic<int>  crashes{0};

        std::thread setter([&]() {
            for (int i = 0; i < 1000 && !done.load(); ++i) {
                MemoryStream* ms = makeStream(v2buf, kStateBytesV2);
                proc.setState(ms);
                ms->release();
            }
        });

        for (int i = 0; i < 1000; ++i) {
            MemoryStream* ms = new MemoryStream();
            proc.getState(ms);
            int64 pos = 0;
            ms->seek(0, IBStream::kIBSeekEnd, &pos);
            ms->release();
            if (pos != 40) ++crashes;  // C4-S7: getState now emits v3 (40 bytes)
        }
        done.store(true);
        setter.join();
        CHECK(crashes == 0, "15_concurrent_set_get_state_1000iter");
    }

    // -------------------------------------------------------------------
    // Assertion 16: v1 -> v2 upgrade path
    // setState(v1) -> getState -> setState fresh v2 proc -> 6 norms + bypass=0
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc1;
        setupProc(proc1);
        uint8_t v1buf[kStateBytesV1];
        buildV1Buf(v1buf, kTestVals);
        {
            MemoryStream* ms = makeStream(v1buf, kStateBytesV1);
            proc1.setState(ms); ms->release();
        }
        // getState -> v3 buf (C4-S7: writer emits v3 = 40 bytes)
        uint8_t v3out[kStateBytesV3]{};
        {
            MemoryStream* ms = new MemoryStream();
            proc1.getState(ms);
            int64 res = 0;
            ms->seek(0, IBStream::kIBSeekSet, &res);
            int32 nr = 0;
            ms->read(v3out, kStateBytesV3, &nr);
            ms->release();
        }
        // Fresh proc reads v3 stream
        spe::vst3::SpatialEngineProcessor proc2;
        setupProc(proc2);
        {
            MemoryStream* ms = makeStream(v3out, kStateBytesV3);
            proc2.setState(ms); ms->release();
        }
        float got[7];
        extractNormsV2(proc2, got);
        bool ok = true;
        for (int i = 0; i < 6; ++i)
            if (std::fabs(got[i] - kTestVals[i]) >= 1e-5f) ok=false;
        if (std::fabs(got[6] - 0.f) >= 1e-5f) ok=false; // bypass=0
        CHECK(ok, "16_v1_to_v3_upgrade");
    }

    // -------------------------------------------------------------------
    // Assertion 17: v2 -> v2 full roundtrip (7 distinct vals)
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        uint8_t buf[kStateBytesV2];
        buildV2Buf(buf, kTestVals7);
        {
            MemoryStream* ms = makeStream(buf, kStateBytesV2);
            proc.setState(ms); ms->release();
        }
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i = 0; i < 7; ++i)
            if (std::fabs(got[i] - kTestVals7[i]) >= 1e-5f) ok=false;
        CHECK(ok, "17_v2_to_v2_roundtrip");
    }

    // -------------------------------------------------------------------
    // Assertion 18: v2 header + only 5 floats (28 bytes total) -> defaults
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        uint8_t buf[28]{};
        int32 magic = kMagicOk; uint16_t v=2, n=7;
        std::memcpy(buf+0,&magic,4); std::memcpy(buf+4,&v,2); std::memcpy(buf+6,&n,2);
        // only 5 floats (20 bytes) after header -> total 28, short for v2 (needs 36)
        for (int i=0;i<5;++i) { float f=kTestVals7[i]; std::memcpy(buf+8+i*4,&f,4); }
        MemoryStream* ms = makeStream(buf, 28);
        proc.setState(ms); ms->release();
        float got[7];
        extractNormsV2(proc, got);
        bool ok = true;
        for (int i=0;i<7;++i) if (std::fabs(got[i]-kDefaults7[i])>=1e-5f) ok=false;
        CHECK(ok, "18_v2_truncated_at_byte_28");
    }

    // -------------------------------------------------------------------
    // Assertion 19 (A10): setState(v2 bypass=1) then setState(v1) -> bypass=0
    // -------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);

        // Set v2 with bypass=1
        uint8_t v2buf[kStateBytesV2];
        buildV2Buf(v2buf, kTestVals7); // kTestVals7[6]=1.0 (bypass on)
        {
            MemoryStream* ms = makeStream(v2buf, kStateBytesV2);
            proc.setState(ms); ms->release();
        }
        float got1[7];
        extractNormsV2(proc, got1);
        bool bypass_on = (std::fabs(got1[6] - 1.f) < 1e-5f);

        // Now setState v1 -> bypass should reset to 0
        uint8_t v1buf[kStateBytesV1];
        buildV1Buf(v1buf, kTestVals);
        {
            MemoryStream* ms = makeStream(v1buf, kStateBytesV1);
            proc.setState(ms); ms->release();
        }
        float got2[7];
        extractNormsV2(proc, got2);
        bool bypass_reset = (std::fabs(got2[6] - 0.f) < 1e-5f);
        CHECK(bypass_on && bypass_reset, "19_multicall_v2_then_v1_bypass_reset");
    }

    printf("state_persist: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

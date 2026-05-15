// vst3/tests/test_vst3_state_v3_back_compat.cpp
// v0.4 P0 — v3 reader fall-through under the v4-capable reader.
//
// This test confirms a v3 blob still loads through the new setState() path
// and the resulting state matches what a v0.3.1 plugin produced. Distinct
// from the merge-gate test (which checks the v3→v4 transition is byte-
// equal); here we confirm the legacy reader code-path is preserved.

#include "SpatialEngineProcessor.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int32  kMagicLegacy = 0x31455053; // 'SPE1' LE
static constexpr int32  kMagicV4     = 0x34455053; // 'SPE4' LE
static constexpr int    kStateBytesV1 = 32;
static constexpr int    kStateBytesV2 = 36;
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

static const uint8_t* findEngineCore(const std::vector<uint8_t>& blob, uint32& out_len) {
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
        if (id == 0x0001) { out_len = len; return blob.data() + off; }
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

    // -----------------------------------------------------------------------
    // v1 blob (32 bytes) — 6 params, kBypass and kMute default to 0.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        const float vals[6] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
        uint8_t buf[kStateBytesV1]{};
        uint16 version = 1, nparams = 6;
        std::memcpy(buf + 0, &kMagicLegacy, 4);
        std::memcpy(buf + 4, &version, 2);
        std::memcpy(buf + 6, &nparams, 2);
        for (int i = 0; i < 6; ++i) std::memcpy(buf + 8 + i * 4, &vals[i], 4);
        MemoryStream* ms = makeStream(buf, kStateBytesV1);
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "v3_compat_v1_setState_ok");

        auto blob = dumpState(proc);
        uint32 core_len = 0;
        const uint8_t* core = findEngineCore(blob, core_len);
        CHECK(core && core_len == 32, "v3_compat_v1_engine_core_present");
        bool ok = (core != nullptr);
        for (int i = 0; i < 6 && ok; ++i) {
            float got = 0.f;
            std::memcpy(&got, core + i * 4, 4);
            if (std::fabs(got - vals[i]) >= 1e-6f) ok = false;
        }
        if (ok) {
            // kBypass=0, kMute=0 defaults
            float by = 1.f, mu = 1.f;
            std::memcpy(&by, core + 6 * 4, 4);
            std::memcpy(&mu, core + 7 * 4, 4);
            if (std::fabs(by) >= 1e-6f || std::fabs(mu) >= 1e-6f) ok = false;
        }
        CHECK(ok, "v3_compat_v1_six_params_plus_defaults");
    }

    // -----------------------------------------------------------------------
    // v2 blob (36 bytes) — 7 params; kMute defaults to 0.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        const float vals[7] = {0.11f, 0.22f, 0.33f, 0.44f, 0.55f, 0.66f, 1.0f};
        uint8_t buf[kStateBytesV2]{};
        uint16 version = 2, nparams = 7;
        std::memcpy(buf + 0, &kMagicLegacy, 4);
        std::memcpy(buf + 4, &version, 2);
        std::memcpy(buf + 6, &nparams, 2);
        for (int i = 0; i < 7; ++i) std::memcpy(buf + 8 + i * 4, &vals[i], 4);
        MemoryStream* ms = makeStream(buf, kStateBytesV2);
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "v3_compat_v2_setState_ok");

        auto blob = dumpState(proc);
        uint32 core_len = 0;
        const uint8_t* core = findEngineCore(blob, core_len);
        bool ok = (core != nullptr && core_len == 32);
        for (int i = 0; i < 7 && ok; ++i) {
            float got = 0.f;
            std::memcpy(&got, core + i * 4, 4);
            if (std::fabs(got - vals[i]) >= 1e-6f) ok = false;
        }
        if (ok) {
            float mu = 1.f;
            std::memcpy(&mu, core + 7 * 4, 4);
            if (std::fabs(mu) >= 1e-6f) ok = false;
        }
        CHECK(ok, "v3_compat_v2_seven_params_kMute_default");
    }

    // -----------------------------------------------------------------------
    // v3 blob (40 bytes) — 8 params including kMute=0.75; all should round-trip.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        const float vals[8] = {0.12f, 0.24f, 0.36f, 0.48f, 0.6f, 0.72f, 0.0f, 0.75f};
        uint8_t buf[kStateBytesV3]{};
        uint16 version = 3, nparams = 8;
        std::memcpy(buf + 0, &kMagicLegacy, 4);
        std::memcpy(buf + 4, &version, 2);
        std::memcpy(buf + 6, &nparams, 2);
        for (int i = 0; i < 8; ++i) std::memcpy(buf + 8 + i * 4, &vals[i], 4);
        MemoryStream* ms = makeStream(buf, kStateBytesV3);
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "v3_compat_v3_setState_ok");

        auto blob = dumpState(proc);
        uint32 core_len = 0;
        const uint8_t* core = findEngineCore(blob, core_len);
        bool ok = (core != nullptr && core_len == 32);
        for (int i = 0; i < 8 && ok; ++i) {
            float got = 0.f;
            std::memcpy(&got, core + i * 4, 4);
            if (std::fabs(got - vals[i]) >= 1e-6f) ok = false;
        }
        CHECK(ok, "v3_compat_v3_eight_params_full_roundtrip");
    }

    std::printf("v3_back_compat: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

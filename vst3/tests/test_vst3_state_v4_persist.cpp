// vst3/tests/test_vst3_state_v4_persist.cpp
// v0.4 P0 — state v4 sectioned/TLV round-trip.
//
// Coverage:
//   1. Plugin writes v4 (magic 'SPE4', version=4, ≥4 sections).
//   2. Round-trip: write → read → identical 8-float param block.
//   3. Layout/sofa path sections round-trip a UTF-8 path string set via the
//      engine setters before getState.
//   4. binaural_state section round-trips enable=1, mode=0.
//   5. Unknown future section IDs are skipped without error.

#include "SpatialEngineProcessor.hpp"
#include "core/SpatialEngine.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int32  kMagicV4    = 0x34455053; // 'SPE4' LE
static constexpr uint16 kSecEngineCore    = 0x0001;
static constexpr uint16 kSecLayoutPath    = 0x0002;
static constexpr uint16 kSecSofaPath      = 0x0003;
static constexpr uint16 kSecBinauralState = 0x0004;

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

// Locate a section payload in a v4 blob. Returns null if missing.
static const uint8_t* findSection(const std::vector<uint8_t>& blob,
                                  uint16 sec_id, uint32& out_len) {
    if (blob.size() < 8) return nullptr;
    int32 magic = 0;
    std::memcpy(&magic, blob.data() + 0, 4);
    if (magic != kMagicV4) return nullptr;
    uint16 version = 0;
    std::memcpy(&version, blob.data() + 4, 2);
    if (version != 4) return nullptr;
    uint16 section_count = 0;
    std::memcpy(&section_count, blob.data() + 6, 2);

    size_t off = 8;
    for (uint16 s = 0; s < section_count; ++s) {
        if (off + 6 > blob.size()) return nullptr;
        uint16 id = 0;
        uint32 len = 0;
        std::memcpy(&id, blob.data() + off + 0, 2);
        std::memcpy(&len, blob.data() + off + 2, 4);
        off += 6;
        if (off + len > blob.size()) return nullptr;
        if (id == sec_id) { out_len = len; return blob.data() + off; }
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
    // 1. Round-trip via v4 setState → getState. Pre-seed engine_core through
    //    a hand-crafted v4 blob; verify writer re-emits identical payload.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);

        // Build a v4 blob with engine_core, layout_path="cfg.yaml", sofa_path="x.speh", binaural_state=(1,0)
        const float kVals[8] = {0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f};
        std::string layout = "configs/lab_8ch.yaml";
        std::string sofa   = "data/hrtf.speh";

        std::vector<uint8_t> in;
        in.resize(8);
        std::memcpy(in.data() + 0, &kMagicV4, 4);
        uint16 v = 4;          std::memcpy(in.data() + 4, &v, 2);
        uint16 sc = 4;         std::memcpy(in.data() + 6, &sc, 2);

        auto append_section = [&](uint16 sec_id, const uint8_t* payload, uint32 len) {
            uint8_t hdr[6];
            std::memcpy(hdr + 0, &sec_id, 2);
            std::memcpy(hdr + 2, &len, 4);
            in.insert(in.end(), hdr, hdr + 6);
            in.insert(in.end(), payload, payload + len);
        };
        uint8_t core_payload[32];
        for (int i = 0; i < 8; ++i) std::memcpy(core_payload + i * 4, &kVals[i], 4);
        append_section(kSecEngineCore, core_payload, 32);
        append_section(kSecLayoutPath,
                       reinterpret_cast<const uint8_t*>(layout.data()),
                       static_cast<uint32>(layout.size()));
        append_section(kSecSofaPath,
                       reinterpret_cast<const uint8_t*>(sofa.data()),
                       static_cast<uint32>(sofa.size()));
        uint8_t bin_payload[2] = {1, 0};
        append_section(kSecBinauralState, bin_payload, 2);

        MemoryStream* ms = makeStream(in.data(), static_cast<int>(in.size()));
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "v4_setState_ok");

        // Verify getState round-trips every section
        auto blob = dumpState(proc);
        CHECK(blob.size() >= 8, "v4_getState_nonempty");

        // engine_core: 8 floats match
        uint32 core_len = 0;
        const uint8_t* core = findSection(blob, kSecEngineCore, core_len);
        bool core_ok = (core != nullptr && core_len == 32);
        if (core_ok) {
            for (int i = 0; i < 8; ++i) {
                float v_got = 0.f;
                std::memcpy(&v_got, core + i * 4, 4);
                if (std::fabs(v_got - kVals[i]) >= 1e-6f) core_ok = false;
            }
        }
        CHECK(core_ok, "v4_engine_core_8_floats_bytewise");

        // layout_path
        uint32 lp_len = 0;
        const uint8_t* lp = findSection(blob, kSecLayoutPath, lp_len);
        bool lp_ok = (lp != nullptr && lp_len == layout.size()
                      && std::memcmp(lp, layout.data(), lp_len) == 0);
        CHECK(lp_ok, "v4_layout_path_roundtrip");

        // sofa_path
        uint32 sp_len = 0;
        const uint8_t* sp = findSection(blob, kSecSofaPath, sp_len);
        bool sp_ok = (sp != nullptr && sp_len == sofa.size()
                      && std::memcmp(sp, sofa.data(), sp_len) == 0);
        CHECK(sp_ok, "v4_sofa_path_roundtrip");

        // binaural_state
        uint32 bs_len = 0;
        const uint8_t* bs = findSection(blob, kSecBinauralState, bs_len);
        CHECK(bs != nullptr && bs_len == 2 && bs[0] == 1 && bs[1] == 0,
              "v4_binaural_state_enable_eq_1_mode_eq_0");
    }

    // -----------------------------------------------------------------------
    // 2. Empty paths round-trip as zero-length sections.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        auto blob = dumpState(proc);
        uint32 lp_len = 999;
        const uint8_t* lp = findSection(blob, kSecLayoutPath, lp_len);
        CHECK(lp != nullptr && lp_len == 0, "v4_layout_path_empty_default");

        uint32 sp_len = 999;
        const uint8_t* sp = findSection(blob, kSecSofaPath, sp_len);
        CHECK(sp != nullptr && sp_len == 0, "v4_sofa_path_empty_default");

        uint32 bs_len = 0;
        const uint8_t* bs = findSection(blob, kSecBinauralState, bs_len);
        CHECK(bs != nullptr && bs_len == 2 && bs[0] == 0 && bs[1] == 0,
              "v4_binaural_state_default_disabled");
    }

    // -----------------------------------------------------------------------
    // 3. Unknown section IDs are skipped (forward-compat). Build a v4 blob
    //    with section_count=5: engine_core + unknown 0x00AA + standard 3.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);

        std::vector<uint8_t> in;
        in.resize(8);
        std::memcpy(in.data() + 0, &kMagicV4, 4);
        uint16 v = 4;   std::memcpy(in.data() + 4, &v, 2);
        uint16 sc = 5;  std::memcpy(in.data() + 6, &sc, 2);

        auto append_section = [&](uint16 sec_id, const uint8_t* payload, uint32 len) {
            uint8_t hdr[6];
            std::memcpy(hdr + 0, &sec_id, 2);
            std::memcpy(hdr + 2, &len, 4);
            in.insert(in.end(), hdr, hdr + 6);
            in.insert(in.end(), payload, payload + len);
        };

        uint8_t core_payload[32]{};
        const float v0 = 0.42f;
        std::memcpy(core_payload + 0 * 4, &v0, 4);
        append_section(kSecEngineCore, core_payload, 32);

        // Unknown section 0x00AA — should be skipped silently.
        uint8_t unknown_payload[10] = {1,2,3,4,5,6,7,8,9,10};
        append_section(0x00AA, unknown_payload, 10);

        append_section(kSecLayoutPath,
                       reinterpret_cast<const uint8_t*>("k"), 1);
        append_section(kSecSofaPath,
                       reinterpret_cast<const uint8_t*>("z"), 1);
        uint8_t bin_payload[2] = {1, 0};
        append_section(kSecBinauralState, bin_payload, 2);

        MemoryStream* ms = makeStream(in.data(), static_cast<int>(in.size()));
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "v4_unknown_section_skipped_setState_ok");

        auto blob = dumpState(proc);
        uint32 core_len = 0;
        const uint8_t* core = findSection(blob, kSecEngineCore, core_len);
        bool ok = (core && core_len == 32);
        if (ok) {
            float got = 0.f;
            std::memcpy(&got, core + 0, 4);
            ok = std::fabs(got - v0) < 1e-6f;
        }
        CHECK(ok, "v4_unknown_section_does_not_corrupt_engine_core");
    }

    std::printf("state_v4_persist: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

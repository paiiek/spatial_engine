// vst3/tests/test_vst3_state_v4_binaural_section.cpp
// v0.5 P5 — state v4 binaural_state (0x0004) extended payload (4 bytes).
//
// Layout (v0.5):
//   byte[0] = binaural_enable u8 (1=on, 0=off)
//   byte[1] = effective_mode  u8 (0=Direct, 1=AmbiVS) — telemetry; reader
//             ignores (effective recomputed by probe).
//   byte[2] = requested_mode  u8 (0=Direct, 1=AmbiVS) — user intent;
//             preserved across B2→Direct fallback.
//   byte[3] = reserved padding (0).
//
// Coverage:
//   1. Round-trip enable=1, requested=AmbiVS — proc2 ends up with
//      binauralEnabled()=true and binauralMode()=1.
//   2. Writer-side fallback layout — even when effective=0 (Direct), the
//      writer emits byte[2]=requested_mode=1; reader dispatches off byte[2]
//      and proc2's binauralMode() == 1 (requested preserved).
//   3. Short-payload back-compat — a synthetic v0.4-style 2-byte payload
//      (enable=1, mode_legacy=0) is accepted; enable applied, requested
//      mode is left untouched on proc2 (i.e. defaults to 0=Direct).

#include "SpatialEngineProcessor.hpp"
#include "core/SpatialEngine.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int32  kMagicV4          = 0x34455053; // 'SPE4' LE
static constexpr uint16 kSecEngineCore    = 0x0001;
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
    // 1. Round-trip {enable=1, requested_mode=AmbiVS} via proc1 → proc2.
    //    Verifies writer emits requested_mode and reader dispatches via
    //    setBinauralMode().
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc1;
        setupProc(proc1);
        proc1.engine().setBinauralEnabled(true);
        proc1.engine().setBinauralMode(1);  // AmbiVS

        // Writer-side: payload[0]=1, payload[2]=1 (requested AmbiVS).
        auto blob1 = dumpState(proc1);
        uint32 bs_len = 0;
        const uint8_t* bs = findSection(blob1, kSecBinauralState, bs_len);
        CHECK(bs != nullptr && bs_len == 4,
              "p5_writer_binaural_section_is_4_bytes");
        CHECK(bs != nullptr && bs[0] == 1,
              "p5_writer_emits_enable_eq_1");
        CHECK(bs != nullptr && bs[2] == 1,
              "p5_writer_emits_requested_mode_eq_AmbiVS");
        CHECK(bs != nullptr && bs[3] == 0,
              "p5_writer_emits_reserved_pad_eq_0");

        // Reader: a fresh proc must end up enabled with requested=AmbiVS.
        spe::vst3::SpatialEngineProcessor proc2;
        setupProc(proc2);
        MemoryStream* ms = makeStream(blob1.data(),
                                      static_cast<int>(blob1.size()));
        tresult r = proc2.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "p5_reader_setState_ok");
        CHECK(proc2.engine().binauralEnabled() == true,
              "p5_reader_enable_round_trips");
        CHECK(proc2.engine().binauralMode() == 1,
              "p5_reader_requested_mode_round_trips_to_AmbiVS");
    }

    // -----------------------------------------------------------------------
    // 2. White-box: simulate a B2 fallback scenario by synthesising the v4
    //    blob byte-for-byte with effective=0, requested=1. Reader must
    //    dispatch off payload[2] only — proc2's binauralMode() == 1.
    // -----------------------------------------------------------------------
    {
        std::vector<uint8_t> in;
        in.resize(8);
        std::memcpy(in.data() + 0, &kMagicV4, 4);
        uint16 v = 4;  std::memcpy(in.data() + 4, &v, 2);
        uint16 sc = 2; std::memcpy(in.data() + 6, &sc, 2);

        auto append_section = [&](uint16 sec_id, const uint8_t* payload,
                                  uint32 len) {
            uint8_t hdr[6];
            std::memcpy(hdr + 0, &sec_id, 2);
            std::memcpy(hdr + 2, &len, 4);
            in.insert(in.end(), hdr, hdr + 6);
            in.insert(in.end(), payload, payload + len);
        };

        // engine_core (8 floats = 32 bytes; all zeros is acceptable).
        uint8_t core_payload[32]{};
        append_section(kSecEngineCore, core_payload, 32);

        // binaural_state: enable=1, effective=0 (fallback), requested=1 (AmbiVS), pad=0.
        uint8_t bin_payload[4] = {1, 0, 1, 0};
        append_section(kSecBinauralState, bin_payload, 4);

        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        MemoryStream* ms = makeStream(in.data(),
                                      static_cast<int>(in.size()));
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "p5_fallback_synth_setState_ok");
        CHECK(proc.engine().binauralEnabled() == true,
              "p5_fallback_enable_applied");
        CHECK(proc.engine().binauralMode() == 1,
              "p5_fallback_requested_preserved_even_when_effective_eq_0");
    }

    // -----------------------------------------------------------------------
    // 3. Short-payload back-compat — v0.4 produced a 2-byte payload. Reader
    //    must accept it, apply enable, and NOT touch requested mode.
    // -----------------------------------------------------------------------
    {
        std::vector<uint8_t> in;
        in.resize(8);
        std::memcpy(in.data() + 0, &kMagicV4, 4);
        uint16 v = 4;  std::memcpy(in.data() + 4, &v, 2);
        uint16 sc = 2; std::memcpy(in.data() + 6, &sc, 2);

        auto append_section = [&](uint16 sec_id, const uint8_t* payload,
                                  uint32 len) {
            uint8_t hdr[6];
            std::memcpy(hdr + 0, &sec_id, 2);
            std::memcpy(hdr + 2, &len, 4);
            in.insert(in.end(), hdr, hdr + 6);
            in.insert(in.end(), payload, payload + len);
        };

        uint8_t core_payload[32]{};
        append_section(kSecEngineCore, core_payload, 32);

        // v0.4 short payload: enable=1, mode_legacy=0 (only 2 bytes).
        uint8_t bin_payload[2] = {1, 0};
        append_section(kSecBinauralState, bin_payload, 2);

        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        // Seed requested mode to AmbiVS BEFORE setState — a short payload
        // must NOT clobber it (reader skips the requested_mode dispatch).
        proc.engine().setBinauralMode(1);

        MemoryStream* ms = makeStream(in.data(),
                                      static_cast<int>(in.size()));
        tresult r = proc.setState(ms);
        ms->release();
        CHECK(r == kResultOk, "p5_short_payload_setState_ok");
        CHECK(proc.engine().binauralEnabled() == true,
              "p5_short_payload_enable_applied");
        CHECK(proc.engine().binauralMode() == 1,
              "p5_short_payload_does_not_touch_requested_mode");
    }

    std::printf("state_v4_binaural_section: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

// core/tests/core_unit/test_v04_binaural_bus_placeholder.cpp
// v0.4 P1 — verify bus 1 emits the -6 dB speaker→binaural downmix placeholder.
//
// Strategy: drive the VST3 processor with a 2-channel speaker bus (negotiated
// via setBusArrangements) and a stereo binaural bus 1. Inject a known signal
// directly into bus 0 channels via the bypass path (which copies input → bus 0).
// Bus 1 must then carry 0.5 * (bus0_L + bus0_R) — bit-exactly equal across
// L and R outputs.
//
// This test lives under core/tests/core_unit per the v0.4 plan, but it pulls
// in the VST3 processor sources directly so we can drive process() without
// dlopen.

#include "SpatialEngineProcessor.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static void setupProc(spe::vst3::SpatialEngineProcessor& proc) {
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = 48000.0;
    proc.setupProcessing(setup);
    proc.setActive(true);
}

static void negotiate2ch(spe::vst3::SpatialEngineProcessor& proc) {
    SpeakerArrangement outs[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo};
    SpeakerArrangement ins[1]  = {SpeakerArr::kStereo};
    proc.setBusArrangements(ins, 1, outs, 2);
}

int main() {
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; std::printf("PASS %s\n", name); }
        else      { ++fail; std::fprintf(stderr, "FAIL %s\n", name); }
    };

    // -----------------------------------------------------------------------
    // 1. Bypass path: input → bus 0; bus 1 must be 0.5 * (bus0_L + bus0_R).
    // Bypass is the simplest way to put a known signal on bus 0 without
    // running the full engine.
    // -----------------------------------------------------------------------
    {
        spe::vst3::SpatialEngineProcessor proc;
        proc.initialize(nullptr);
        negotiate2ch(proc);
        setupProc(proc);

        // Enable bypass via setState (kBypass = norm_values_[6] = 1.0)
        // We cannot easily reach norm_values_ from outside; the kBypass
        // path is the canonical RT-safe identity copy. Instead, push a v4
        // state blob via setState.
        // engine_core: bypass=1.0, all others 0.
        const float vals[8] = {0.f,0.f,0.f,0.f,0.f,0.f, 1.0f, 0.f};
        // Build a tiny v4 blob with just engine_core.
        std::vector<uint8_t> blob;
        blob.resize(8);
        int32 magic = 0x34455053;
        uint16 ver = 4; uint16 sc = 1;
        std::memcpy(blob.data() + 0, &magic, 4);
        std::memcpy(blob.data() + 4, &ver, 2);
        std::memcpy(blob.data() + 6, &sc, 2);
        uint16 id = 0x0001;
        uint32 len = 32;
        uint8_t hdr[6];
        std::memcpy(hdr + 0, &id, 2);
        std::memcpy(hdr + 2, &len, 4);
        blob.insert(blob.end(), hdr, hdr + 6);
        for (int i = 0; i < 8; ++i) {
            uint8_t fb[4];
            std::memcpy(fb, &vals[i], 4);
            blob.insert(blob.end(), fb, fb + 4);
        }
        {
            MemoryStream* ms = new MemoryStream();
            int32 written = 0;
            ms->write(blob.data(), static_cast<int32>(blob.size()), &written);
            int64 res = 0;
            ms->seek(0, IBStream::kIBSeekSet, &res);
            proc.setState(ms);
            ms->release();
        }

        constexpr int kN = 64;
        float in_l[kN], in_r[kN];
        float out0_l[kN]{}, out0_r[kN]{};
        float out1_l[kN]{}, out1_r[kN]{};

        // Distinct sine on L and R so the downmix is unambiguous.
        for (int n = 0; n < kN; ++n) {
            in_l[n] = std::sin(2.f * 3.14159265f * 220.f * n / 48000.f);
            in_r[n] = std::sin(2.f * 3.14159265f * 440.f * n / 48000.f);
        }

        float* in_ptrs[2]   = {in_l, in_r};
        float* out0_ptrs[2] = {out0_l, out0_r};
        float* out1_ptrs[2] = {out1_l, out1_r};

        AudioBusBuffers inBus{};   inBus.numChannels = 2; inBus.channelBuffers32 = in_ptrs;
        AudioBusBuffers out0Bus{}; out0Bus.numChannels = 2; out0Bus.channelBuffers32 = out0_ptrs;
        AudioBusBuffers out1Bus{}; out1Bus.numChannels = 2; out1Bus.channelBuffers32 = out1_ptrs;
        AudioBusBuffers outs[2] = {out0Bus, out1Bus};

        ProcessData data{};
        data.processMode        = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numInputs          = 1;
        data.numOutputs         = 2;
        data.inputs             = &inBus;
        data.outputs            = outs;
        data.numSamples         = kN;

        tresult r = proc.process(data);
        CHECK(r == kResultOk, "process_ok_with_two_buses");

        // Under bypass, bus 0 should be identity copy of input.
        bool bus0_identity = true;
        for (int n = 0; n < kN; ++n) {
            if (std::fabs(outs[0].channelBuffers32[0][n] - in_l[n]) > 1e-6f) bus0_identity = false;
            if (std::fabs(outs[0].channelBuffers32[1][n] - in_r[n]) > 1e-6f) bus0_identity = false;
        }
        CHECK(bus0_identity, "bypass_bus0_identity_copy");

        // Bus 1 L and R must be bit-exactly equal (mono downmix property).
        bool LR_equal = true;
        for (int n = 0; n < kN; ++n) {
            uint32_t aL, aR;
            std::memcpy(&aL, outs[1].channelBuffers32[0] + n, 4);
            std::memcpy(&aR, outs[1].channelBuffers32[1] + n, 4);
            if (aL != aR) { LR_equal = false; break; }
        }
        CHECK(LR_equal, "bus1_L_eq_R_bitwise");

        // Bus 1 sample[n] must equal 0.5 * (bus0_L[n] + bus0_R[n]) bit-exactly.
        bool downmix_ok = true;
        float peak = 0.f;
        for (int n = 0; n < kN; ++n) {
            const float expected =
                0.5f * (outs[0].channelBuffers32[0][n]
                        + outs[0].channelBuffers32[1][n]);
            if (std::fabs(outs[1].channelBuffers32[0][n] - expected) > 1e-6f) {
                downmix_ok = false;
                break;
            }
            const float ab = std::fabs(outs[1].channelBuffers32[0][n]);
            if (ab > peak) peak = ab;
        }
        CHECK(downmix_ok, "bus1_eq_half_sum_of_bus0_ch0_ch1");

        // The downmix must be non-zero (test the placeholder is wired, not
        // silent). Peak should be at least 0.01 with our sine input.
        CHECK(peak > 0.01f, "bus1_downmix_audible_nonzero");

        // Bus 1 peak should not exceed bus 0 peak — -6 dB sum is safe.
        float bus0_peak = 0.f;
        for (int n = 0; n < kN; ++n) {
            const float a = std::fabs(outs[0].channelBuffers32[0][n]);
            const float b = std::fabs(outs[0].channelBuffers32[1][n]);
            if (a > bus0_peak) bus0_peak = a;
            if (b > bus0_peak) bus0_peak = b;
        }
        CHECK(peak <= bus0_peak + 1e-6f, "bus1_peak_le_bus0_peak");

        proc.terminate();
    }

    std::printf("v04_binaural_bus_placeholder: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

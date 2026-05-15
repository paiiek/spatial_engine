// vst3/tests/test_vst3_bus_arrangement_speaker_binaural.cpp
// v0.4 P1 — VST3 multi-bus arrangement: speakers (bus 0) + binaural (bus 1).
//
// Coverage:
//   1. getBusCount(kAudio, kOutput) == 2.
//   2. getBusInfo(kOutput, 1) reports a 2-channel stereo bus named "Binaural".
//   3. setBusArrangements({stereo|quad|surround5.1|7.1|...}, kStereo) returns
//      kResultTrue and getBusArrangement round-trips both buses.
//   4. setBusArrangements with bus 1 != kStereo is rejected.
//   5. setBusArrangements with bus 0 = non-standard channel count is rejected.

#include "SpatialEngineProcessor.hpp"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;

int main() {
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; std::printf("PASS %s\n", name); }
        else      { ++fail; std::fprintf(stderr, "FAIL %s\n", name); }
    };

    spe::vst3::SpatialEngineProcessor proc;
    proc.initialize(nullptr);

    // -----------------------------------------------------------------------
    // 1. Bus count: 1 input, 2 outputs.
    // -----------------------------------------------------------------------
    CHECK(proc.getBusCount(kAudio, kInput)  == 1, "bus_count_input_is_1");
    CHECK(proc.getBusCount(kAudio, kOutput) == 2, "bus_count_output_is_2");

    // -----------------------------------------------------------------------
    // 2. Bus 1 metadata.
    // -----------------------------------------------------------------------
    {
        BusInfo info{};
        tresult r = proc.getBusInfo(kAudio, kOutput, 1, info);
        CHECK(r == kResultOk, "bus_info_out1_ok");
        CHECK(info.channelCount == 2, "bus_info_out1_stereo");
        CHECK(info.busType == kAux, "bus_info_out1_aux");
    }

    // -----------------------------------------------------------------------
    // 3. setBusArrangements with various speaker bus widths.
    // -----------------------------------------------------------------------
    auto tryArr = [&](SpeakerArrangement spk, SpeakerArrangement bin,
                      bool expect_ok, const char* label) {
        SpeakerArrangement outs[2] = {spk, bin};
        SpeakerArrangement ins[1]  = {SpeakerArr::kStereo};
        tresult r = proc.setBusArrangements(ins, 1, outs, 2);
        bool got_ok = (r == kResultTrue);
        if (got_ok != expect_ok) {
            std::fprintf(stderr,
                "  setBusArrangements(%s) expected=%d got=%d (r=%d)\n",
                label, (int)expect_ok, (int)got_ok, (int)r);
        }
        CHECK(got_ok == expect_ok, label);

        if (got_ok) {
            SpeakerArrangement back0 = 0, back1 = 0;
            proc.getBusArrangement(kOutput, 0, back0);
            proc.getBusArrangement(kOutput, 1, back1);
            char nm[128];
            std::snprintf(nm, sizeof(nm), "%s_roundtrip", label);
            CHECK(back0 == spk && back1 == bin, nm);
        }
    };

    tryArr(SpeakerArr::kStereo, SpeakerArr::kStereo, true,  "spk_stereo_ok");
    tryArr(SpeakerArr::k40Music, SpeakerArr::kStereo, true, "spk_quad_4ch_ok");
    tryArr(SpeakerArr::k51, SpeakerArr::kStereo, true,      "spk_51_6ch_ok");
    tryArr(SpeakerArr::k71Music, SpeakerArr::kStereo, true, "spk_71_8ch_ok");
    tryArr(SpeakerArr::k71_4, SpeakerArr::kStereo, true,    "spk_12ch_ok");
    tryArr(SpeakerArr::k222, SpeakerArr::kStereo, true,     "spk_24ch_ok"); // 22.2 = 24 channels

    // -----------------------------------------------------------------------
    // 4. Bus 1 must be exactly kStereo.
    // -----------------------------------------------------------------------
    tryArr(SpeakerArr::kStereo, SpeakerArr::kMono, false, "bin_mono_rejected");
    tryArr(SpeakerArr::kStereo, SpeakerArr::k51, false,   "bin_5_1_rejected");

    // -----------------------------------------------------------------------
    // 5. Non-supported bus 0 channel counts are rejected.
    // -----------------------------------------------------------------------
    tryArr(SpeakerArr::kMono, SpeakerArr::kStereo, false, "spk_mono_rejected");
    tryArr(SpeakerArr::k30Cine, SpeakerArr::kStereo, false, "spk_3ch_rejected");

    // -----------------------------------------------------------------------
    // 6. Bus info channel count tracks the negotiated arrangement.
    // -----------------------------------------------------------------------
    {
        SpeakerArrangement outs[2] = {SpeakerArr::k71Music, SpeakerArr::kStereo};
        SpeakerArrangement ins[1]  = {SpeakerArr::kStereo};
        proc.setBusArrangements(ins, 1, outs, 2);

        BusInfo info{};
        proc.getBusInfo(kAudio, kOutput, 0, info);
        CHECK(info.channelCount == 8, "bus_info_out0_tracks_negotiated_71");
    }

    proc.terminate();

    std::printf("bus_arrangement_speaker_binaural: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

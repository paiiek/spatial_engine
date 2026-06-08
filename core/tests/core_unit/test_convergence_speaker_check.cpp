// test_convergence_speaker_check.cpp
// Dreamscape convergence — Phase 4.2c speaker-check input passthrough.
//
// /noise/{ch}/type passthrough (mode 3) + /noise/{ch}/source <in> routes a
// hardware input channel straight to a speaker so an engineer can confirm the
// physical wiring (reference checkMode 3, AudioEngine.cpp:998-1002). Verified
// by driving the REAL audioBlock() with a synthetic input block and capturing
// the output:
//   1. passthrough copies input[source] → output[channel] (× gain).
//   2. only the addressed speaker is affected; others stay silent.
//   3. no input backend (input_channels == null) → silence, not a crash.

#include "core/SpatialEngine.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

using namespace spe;

static geometry::SpeakerLayout make_layout(int n) {
    geometry::SpeakerLayout l;
    l.name = "test_passthrough";
    for (int i = 0; i < n; ++i) {
        geometry::Speaker s;
        s.channel = i + 1;                       // 1-based YAML channels 1..n
        const float az = (-90.f + 180.f * i / (n - 1)) * 3.14159265f / 180.f;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

static void dispatch_type(core::SpatialEngine& e, uint32_t ch, uint8_t mode) {
    ipc::Command c; c.tag = ipc::CommandTag::NoiseType;
    ipc::PayloadNoiseType p; p.channel = ch; p.mode = mode; c.payload = p;
    e.dispatchCommand(c);
}
static void dispatch_source(core::SpatialEngine& e, uint32_t ch, int32_t src) {
    ipc::Command c; c.tag = ipc::CommandTag::NoiseSource;
    ipc::PayloadNoiseSource p; p.channel = ch; p.source = src; c.payload = p;
    e.dispatchCommand(c);
}
static void dispatch_gain(core::SpatialEngine& e, uint32_t ch, float db) {
    ipc::Command c; c.tag = ipc::CommandTag::NoiseGain;
    ipc::PayloadNoiseGain p; p.channel = ch; p.gain_db = db; c.payload = p;
    e.dispatchCommand(c);
}

// Drive one block; out captured; in_vals[c] is a constant fill for input ch c
// (empty → no input backend, input_channels == nullptr).
static std::vector<std::vector<float>> drive(core::SpatialEngine& e, int n_out,
                                             const std::vector<float>& in_vals) {
    constexpr int F = 64;
    std::vector<std::vector<float>> out((size_t) n_out, std::vector<float>(F, 0.f));
    std::vector<float*> outp((size_t) n_out);
    for (int s = 0; s < n_out; ++s) outp[(size_t) s] = out[(size_t) s].data();

    std::vector<std::vector<float>> in;
    std::vector<const float*> inp;
    for (float v : in_vals) in.emplace_back(F, v);
    for (auto& b : in) inp.push_back(b.data());

    audio_io::AudioBlock blk;
    blk.output_channels = outp.data();
    blk.output_channel_count = n_out;
    blk.input_channels = in_vals.empty() ? nullptr : inp.data();
    blk.input_channel_count = (int) in_vals.size();
    blk.num_frames = F;
    blk.sample_rate = 48000.0;
    e.audioBlock(blk);
    return out;
}

static float mid(const std::vector<float>& v) { return v[v.size() / 2]; }

int main() {
    const int N = 4;
    core::SpatialEngine engine(0);
    engine.setLayout(make_layout(N));
    engine.prepareToPlay(48000.0, 64);

    // Speaker channel 1 (idx 0) ← input channel 2, passthrough, unity gain.
    dispatch_type(engine, 1, 3);          // passthrough
    dispatch_source(engine, 1, 2);
    dispatch_gain(engine, 1, 0.f);        // 0 dB → gain_lin 1

    // Input: ch0=0.1, ch1=0.2, ch2=0.5 (the routed one), ch3=0.9.
    auto out = drive(engine, N, {0.1f, 0.2f, 0.5f, 0.9f});
    CHECK(std::fabs(mid(out[0]) - 0.5f) < 1e-5f, "passthrough routes input[2] -> speaker 1");
    CHECK(std::fabs(mid(out[1])) < 1e-6f, "speaker 2 untouched");
    CHECK(std::fabs(mid(out[2])) < 1e-6f, "speaker 3 untouched");
    CHECK(std::fabs(mid(out[3])) < 1e-6f, "speaker 4 untouched");
    std::printf("[%s] passthrough input[2]=0.5 -> spk1=%.3f, others silent\n",
                (std::fabs(mid(out[0]) - 0.5f) < 1e-5f) ? "PASS" : "FAIL", (double) mid(out[0]));

    // Re-point source to input ch 0 (=0.1); speaker 1 now follows it.
    dispatch_source(engine, 1, 0);
    auto out2 = drive(engine, N, {0.1f, 0.2f, 0.5f, 0.9f});
    CHECK(std::fabs(mid(out2[0]) - 0.1f) < 1e-5f, "source repoint -> input[0]");
    std::printf("[%s] source repoint input[0]=0.1 -> spk1=%.3f\n",
                (std::fabs(mid(out2[0]) - 0.1f) < 1e-5f) ? "PASS" : "FAIL", (double) mid(out2[0]));

    // No input backend (input_channels == null): graceful silence, no crash.
    auto out3 = drive(engine, N, {});
    CHECK(std::fabs(mid(out3[0])) < 1e-6f, "null input -> silence (no crash)");
    std::printf("[%s] null input backend -> silence\n",
                (std::fabs(mid(out3[0])) < 1e-6f) ? "PASS" : "FAIL");

    // Out-of-range source (>= input_channel_count) → silence, no OOB read.
    dispatch_source(engine, 1, 9);
    auto out4 = drive(engine, N, {0.1f, 0.2f});
    CHECK(std::fabs(mid(out4[0])) < 1e-6f, "out-of-range source -> silence");
    std::printf("[%s] out-of-range source -> silence (no OOB)\n",
                (std::fabs(mid(out4[0])) < 1e-6f) ? "PASS" : "FAIL");

    // Transition + persistence: passthrough(src=2) → white → passthrough.
    // Switching to white must STOP reading input; in_src must survive the
    // round-trip so passthrough resumes routing input[2] without re-sending source.
    const std::vector<float> in = {0.1f, 0.2f, 0.5f, 0.9f};
    dispatch_source(engine, 1, 2);
    dispatch_type(engine, 1, 3); auto pa = drive(engine, N, in);     // passthrough → 0.5
    dispatch_type(engine, 1, 0); auto wh = drive(engine, N, in);     // white → noise, not 0.5
    dispatch_type(engine, 1, 3); auto pb = drive(engine, N, in);     // resumes → 0.5 (in_src persisted)
    CHECK(std::fabs(mid(pa[0]) - 0.5f) < 1e-5f, "passthrough routes input[2]");
    CHECK(std::fabs(mid(wh[0]) - 0.5f) > 1e-3f, "white mode stops reading input");
    CHECK(std::fabs(mid(pb[0]) - 0.5f) < 1e-5f, "in_src persists: passthrough resumes input[2]");
    std::printf("[%s] mode 3->0->3: white stops input (%.3f), in_src persists (%.3f)\n",
                (std::fabs(mid(pb[0]) - 0.5f) < 1e-5f && std::fabs(mid(wh[0]) - 0.5f) > 1e-3f)
                    ? "PASS" : "FAIL", (double) mid(wh[0]), (double) mid(pb[0]));

    if (failures) { std::printf("[RESULT] FAIL (%d)\n", failures); return 1; }
    std::printf("[RESULT] PASS\n");
    return 0;
}

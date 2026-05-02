// core/tests/core_unit/test_p_channel_limiter.cpp
// M9: ChannelLimiter unit tests

#include "dsp/ChannelLimiter.h"
#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace spe::dsp;
using namespace spe::ipc;

static void test_limiter_below_threshold() {
    ChannelLimiter lim;
    lim.prepare(48000.0);
    // default threshold = 1.0

    // Run 4800 samples of 0.5-amplitude sine; output should remain == input
    // (envelope never reaches threshold of 1.0)
    float max_diff = 0.f;
    float phase = 0.f;
    const float omega = 2.f * 3.14159265f * 1000.f / 48000.f;
    for (int n = 0; n < 4800; ++n) {
        float in  = 0.5f * std::sin(phase);
        float out = lim.processSample(in);
        float diff = std::abs(out - in);
        if (diff > max_diff) max_diff = diff;
        phase += omega;
    }
    // No gain reduction expected; allow tiny floating-point error
    assert(max_diff < 1e-6f && "below-threshold: output should equal input");
    std::printf("PASS test_limiter_below_threshold (max_diff=%.2e)\n", static_cast<double>(max_diff));
}

static void test_limiter_compression() {
    ChannelLimiter lim;
    lim.prepare(48000.0);
    lim.setThreshold(0.5f);

    // Run 9600 samples (200 ms) of 1.0-amplitude sine.
    // After attack (1 ms ≈ 48 samples), gain reduction should kick in.
    float max_abs = 0.f;
    float phase = 0.f;
    const float omega = 2.f * 3.14159265f * 440.f / 48000.f;
    for (int n = 0; n < 9600; ++n) {
        float out = lim.processSample(std::sin(phase));
        float a = std::abs(out);
        if (a > max_abs) max_abs = a;
        phase += omega;
    }
    // With threshold=0.5 and 10% margin → output abs max ≤ 0.55
    assert(max_abs <= 0.55f && "compression: output should be ≤ 0.55");
    std::printf("PASS test_limiter_compression (max_abs=%.4f)\n", static_cast<double>(max_abs));
}

static void test_limiter_setThreshold_db() {
    ChannelLimiter lim;
    lim.prepare(48000.0);
    lim.setThreshold(0.708f);
    float t = lim.getThreshold();
    assert(std::abs(t - 0.708f) < 1e-6f && "setThreshold stores value");
    std::printf("PASS test_limiter_setThreshold_db (threshold=%.4f)\n", static_cast<double>(t));
}

static void test_output_gain_round_trip() {
    CommandDecoder dec;
    // Build /output/0/gain ,f -6.0
    Command cmd;
    cmd.tag = CommandTag::OutputGain;
    PayloadOutputGain pg; pg.channel = 0; pg.gain_db = -6.f;
    cmd.payload = pg;

    std::vector<uint8_t> buf;
    bool ok = dec.encode(cmd, buf);
    assert(ok && "OutputGain encode should succeed");

    Command decoded = dec.decode(std::span<const uint8_t>(buf.data(), buf.size()));
    assert(decoded.tag == CommandTag::OutputGain && "OutputGain tag round-trip");
    auto* pp = std::get_if<PayloadOutputGain>(&decoded.payload);
    assert(pp && pp->channel == 0 && "OutputGain channel");
    assert(std::abs(pp->gain_db - (-6.f)) < 1e-4f && "OutputGain gain_db");
    std::printf("PASS test_output_gain_round_trip\n");
}

static void test_output_limit_round_trip() {
    CommandDecoder dec;
    Command cmd;
    cmd.tag = CommandTag::OutputLimit;
    PayloadOutputLimit pl; pl.channel = 2; pl.threshold_db = -3.f;
    cmd.payload = pl;

    std::vector<uint8_t> buf;
    bool ok = dec.encode(cmd, buf);
    assert(ok && "OutputLimit encode should succeed");

    Command decoded = dec.decode(std::span<const uint8_t>(buf.data(), buf.size()));
    assert(decoded.tag == CommandTag::OutputLimit && "OutputLimit tag round-trip");
    auto* pp = std::get_if<PayloadOutputLimit>(&decoded.payload);
    assert(pp && pp->channel == 2 && "OutputLimit channel");
    assert(std::abs(pp->threshold_db - (-3.f)) < 1e-4f && "OutputLimit threshold_db");
    std::printf("PASS test_output_limit_round_trip\n");
}

int main() {
    test_limiter_below_threshold();
    test_limiter_compression();
    test_limiter_setThreshold_db();
    test_output_gain_round_trip();
    test_output_limit_round_trip();
    std::printf("All M9 limiter tests passed.\n");
    return 0;
}

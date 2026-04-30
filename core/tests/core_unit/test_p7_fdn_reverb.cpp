// core/tests/core_unit/test_p7_fdn_reverb.cpp
// P7 tests: FdnReverb denormal stability, decay, IR metadata validation.

#include "reverb/FdnReverb.h"
#include "reverb/IRConvolutionStub.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool isFiniteAndSmall(float v, float limit) {
    return std::isfinite(v) && std::abs(v) < limit;
}

static float blockRMS(const float* buf, int n) {
    float sum = 0.f;
    for (int i = 0; i < n; ++i) sum += buf[i] * buf[i];
    return std::sqrt(sum / static_cast<float>(n));
}

static float blockAbsMax(const float* buf, int n) {
    float mx = 0.f;
    for (int i = 0; i < n; ++i) {
        float a = std::abs(buf[i]);
        if (a > mx) mx = a;
    }
    return mx;
}

// ---------------------------------------------------------------------------
// p7_fdn_idle_denormal
// Feed FDN a single impulse then 64 blocks of silence.
// After warmup, per-block abs max must stay < 1.0 (no blow-up / denormal spike).
// ---------------------------------------------------------------------------
static int test_fdn_idle_denormal() {
    spe::reverb::FdnReverb fdn;
    fdn.prepareToPlay(48000.0, 64);

    const int kBlock = 64;
    std::vector<float> buf(kBlock, 0.f);

    // Single impulse.
    buf[0] = 1.f;
    fdn.process(buf.data(), kBlock);

    // 64 blocks of silence — check stability.
    for (int b = 0; b < 64; ++b) {
        std::fill(buf.begin(), buf.end(), 0.f);
        fdn.process(buf.data(), kBlock);
        float mx = blockAbsMax(buf.data(), kBlock);
        if (!isFiniteAndSmall(mx, 1.0f)) {
            std::fprintf(stderr,
                "p7_fdn_idle_denormal FAIL: block %d abs_max=%g (expected < 1.0)\n",
                b, static_cast<double>(mx));
            return 1;
        }
    }
    std::printf("p7_fdn_idle_denormal PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// p7_fdn_decay
// Feed impulse; collect RMS per block for 32 blocks.
// RMS should be monotonically non-increasing after the first block.
// ---------------------------------------------------------------------------
static int test_fdn_decay() {
    spe::reverb::FdnReverb fdn;
    fdn.prepareToPlay(48000.0, 64);

    const int kBlock = 64;
    const int kBlocks = 32;
    std::vector<float> buf(kBlock, 0.f);

    // Impulse block.
    buf[0] = 1.f;
    fdn.process(buf.data(), kBlock);
    float prevRMS = blockRMS(buf.data(), kBlock);

    // Track whether RMS ever goes up significantly (allow tiny floating-point noise).
    // Accept monotone within a generous 5% tolerance.
    constexpr float kTol = 1.05f;
    int violations = 0;
    for (int b = 1; b < kBlocks; ++b) {
        std::fill(buf.begin(), buf.end(), 0.f);
        fdn.process(buf.data(), kBlock);
        float rms = blockRMS(buf.data(), kBlock);
        if (rms > prevRMS * kTol) {
            std::fprintf(stderr,
                "p7_fdn_decay: block %d rms=%g > prev %g * %.2f\n",
                b, static_cast<double>(rms), static_cast<double>(prevRMS), static_cast<double>(kTol));
            ++violations;
        }
        prevRMS = rms;
    }
    if (violations > 0) {
        std::fprintf(stderr, "p7_fdn_decay FAIL: %d monotone violations\n", violations);
        return 1;
    }
    std::printf("p7_fdn_decay PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// p7_ir_metadata_valid
// Valid metadata at matching sample rate → Ok.
// ---------------------------------------------------------------------------
static int test_ir_metadata_valid() {
    spe::reverb::IRConvolutionStub stub;
    stub.setEngineSampleRate(48000);

    spe::reverb::IRMetadata meta;
    meta.sampleRate     = 48000;
    meta.channelCount   = 1;
    meta.irLengthFrames = 48000; // 1 s

    auto err = stub.validate(meta);
    if (err != spe::reverb::IRValidationError::Ok) {
        std::fprintf(stderr,
            "p7_ir_metadata_valid FAIL: expected Ok, got %d, msg: %s\n",
            static_cast<int>(err), stub.lastError().c_str());
        return 1;
    }
    std::printf("p7_ir_metadata_valid PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// p7_ir_metadata_invalid
// Sample rate mismatch → SampleRateMismatch error code.
// ---------------------------------------------------------------------------
static int test_ir_metadata_invalid() {
    spe::reverb::IRConvolutionStub stub;
    stub.setEngineSampleRate(48000);

    spe::reverb::IRMetadata meta;
    meta.sampleRate     = 44100;  // mismatch
    meta.channelCount   = 1;
    meta.irLengthFrames = 44100;

    auto err = stub.validate(meta);
    if (err != spe::reverb::IRValidationError::SampleRateMismatch) {
        std::fprintf(stderr,
            "p7_ir_metadata_invalid FAIL: expected SampleRateMismatch(%d), got %d\n",
            static_cast<int>(spe::reverb::IRValidationError::SampleRateMismatch),
            static_cast<int>(err));
        return 1;
    }
    if (stub.lastError().empty()) {
        std::fprintf(stderr,
            "p7_ir_metadata_invalid FAIL: lastError() is empty on rejection\n");
        return 1;
    }
    std::printf("p7_ir_metadata_invalid PASS (error: %s)\n", stub.lastError().c_str());
    return 0;
}

// ---------------------------------------------------------------------------
int main() {
    int rc = 0;
    rc |= test_fdn_idle_denormal();
    rc |= test_fdn_decay();
    rc |= test_ir_metadata_valid();
    rc |= test_ir_metadata_invalid();
    return rc;
}

// test_p_ir_reverb.cpp
// M3: IRConvReverb (OLA) unit tests.

#include "reverb/IRConvReverb.h"
#include "reverb/ReverbEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace spe::reverb;

// Helper: absolute max of a buffer.
static float absMax(const float* buf, int n) {
    float m = 0.f;
    for (int i = 0; i < n; ++i) {
        float a = std::abs(buf[i]);
        if (a > m) m = a;
    }
    return m;
}

// test_ir_reverb_default_nonzero:
// Default IR, impulse input → output should have energy (absMax > 0.01).
static void test_ir_reverb_default_nonzero() {
    ReverbConfig cfg;
    cfg.type       = ReverbType::IRConvolution;
    cfg.sampleRate = 48000.0;
    cfg.blockSize  = 512;
    cfg.wetMix     = 1.f;

    IRConvReverb rev(cfg);
    rev.prepareToPlay(cfg.sampleRate, cfg.blockSize);

    std::vector<float> buf(static_cast<size_t>(cfg.blockSize), 0.f);
    buf[0] = 1.0f; // impulse

    rev.process(buf.data(), cfg.blockSize);

    float peak = absMax(buf.data(), cfg.blockSize);
    assert(peak > 0.01f && "IRConvReverb default IR: output should be non-zero");
    (void)peak;
    std::puts("  PASS test_ir_reverb_default_nonzero");
}

// test_ir_reverb_load_custom_ir:
// Custom IR [0.5, 0, 0, ...], impulse input → first output sample ≈ 0.5 (±0.01).
static void test_ir_reverb_load_custom_ir() {
    ReverbConfig cfg;
    cfg.type       = ReverbType::IRConvolution;
    cfg.sampleRate = 48000.0;
    cfg.blockSize  = 512;
    cfg.wetMix     = 1.f;

    IRConvReverb rev(cfg);
    rev.prepareToPlay(cfg.sampleRate, cfg.blockSize);

    // Custom IR: ir[0] = 0.5, rest 0.
    std::vector<float> ir(64, 0.f);
    ir[0] = 0.5f;
    rev.loadIR(ir.data(), static_cast<int>(ir.size()));

    std::vector<float> buf(static_cast<size_t>(cfg.blockSize), 0.f);
    buf[0] = 1.0f; // impulse

    rev.process(buf.data(), cfg.blockSize);

    float peak = absMax(buf.data(), cfg.blockSize);
    assert(std::abs(peak - 0.5f) < 0.01f && "IRConvReverb loadIR: peak should be ≈0.5");
    (void)peak;
    std::puts("  PASS test_ir_reverb_load_custom_ir");
}

// test_ir_reverb_factory_select:
// createReverbEngine(IRConvolution) → name() == "IRConvReverb".
static void test_ir_reverb_factory_select() {
    ReverbConfig cfg;
    cfg.type       = ReverbType::IRConvolution;
    cfg.sampleRate = 48000.0;
    cfg.blockSize  = 512;

    auto eng = createReverbEngine(cfg);
    assert(eng != nullptr);
    assert(std::string(eng->name()) == "IRConvReverb");
    std::puts("  PASS test_ir_reverb_factory_select");
}

int main() {
    test_ir_reverb_default_nonzero();
    test_ir_reverb_load_custom_ir();
    test_ir_reverb_factory_select();
    std::puts("PASS test_p_ir_reverb");
    return 0;
}

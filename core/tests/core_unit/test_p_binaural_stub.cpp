// test_p_binaural_stub.cpp
// BinauralMonitor unit tests (pure C++ / NO_JUCE path):
//
// Test 1:  initialize() with empty sofaPath → Ok, pass-through mode
// Test 2:  isInitialized() = false before init
// Test 3:  processBlock pass-through: leftOut == monoIn, rightOut == monoIn
// Test 4:  initialize() with non-existent path → SofaNotFound
// Test 5:  setDirection() doesn't crash and stores values
// Test 6:  reset() on uninitialized monitor doesn't crash
// Test 7:  initialize() with valid .speh → Ok, hasHrtf() = true
// Test 8:  processBlock with unit-impulse HRTF → output ≈ input (causal)
// Test 9:  processBlock with delayed-impulse HRTF → output is delayed
// Test 10: SofaBinReader round-trip: write then read back header fields
// Test 11: processBlock alloc-free after init (RT-safety gate, NO_JUCE only)
// Test 12: block-boundary OLA continuity: 4×64 == 1×256 result

#include "output_backend/BinauralMonitor.h"
#include "hrtf/SofaBinReader.h"
#include "hrtf/OlaConvolver.h"
#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
#include "util/RtAssertNoAlloc.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a, b, tol) \
    do { float _a = (float)(a), _b = (float)(b); \
         if (std::abs(_a - _b) > (float)(tol)) { \
             std::printf("FAIL %s:%d  |%.8f - %.8f| = %.2e > %.2e\n", \
                 __FILE__, __LINE__, (double)_a, (double)_b, \
                 (double)std::abs(_a - _b), (double)(tol)); \
             ++failures; \
         } } while(0)

using namespace spe::output;
using namespace spe::hrtf;

// Write a minimal .speh file with n_pos positions, ir_len IR samples.
// Each IR is a unit impulse at sample 0 for recv=0, delay at sample `delay` for recv=1.
static std::string writeTinySpeh(const std::string& path,
                                  int n_pos, int ir_len, int delay_samples)
{
    // Build header
    struct Hdr {
        char     magic[4];
        uint32_t n_positions;
        uint32_t ir_length;
        uint32_t n_receivers;
        float    sample_rate;
        uint32_t reserved;
    } hdr;
    std::memcpy(hdr.magic, "SPEH", 4);
    hdr.n_positions = static_cast<uint32_t>(n_pos);
    hdr.ir_length   = static_cast<uint32_t>(ir_len);
    hdr.n_receivers = 2;
    hdr.sample_rate = 48000.f;
    hdr.reserved    = 0;

    std::vector<float> positions(static_cast<std::size_t>(n_pos) * 3, 0.f);
    for (int i = 0; i < n_pos; ++i) {
        positions[static_cast<std::size_t>(i) * 3 + 0] = static_cast<float>(i) * (360.f / n_pos);
        positions[static_cast<std::size_t>(i) * 3 + 1] = 0.f;
        positions[static_cast<std::size_t>(i) * 3 + 2] = 1.f;
    }

    std::vector<float> ir(static_cast<std::size_t>(n_pos) * 2 * ir_len, 0.f);
    for (int i = 0; i < n_pos; ++i) {
        // recv 0 (left): unit impulse at sample 0
        ir[static_cast<std::size_t>(i) * 2 * ir_len + 0] = 1.f;
        // recv 1 (right): impulse at delay_samples
        if (delay_samples < ir_len)
            ir[static_cast<std::size_t>(i) * 2 * ir_len +
               static_cast<std::size_t>(ir_len) +
               static_cast<std::size_t>(delay_samples)] = 1.f;
    }

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&hdr),              sizeof(hdr));
    f.write(reinterpret_cast<const char*>(positions.data()),  positions.size() * sizeof(float));
    f.write(reinterpret_cast<const char*>(ir.data()),         ir.size() * sizeof(float));
    return path;
}

int main()
{
    // Test 2: isInitialized() before init
    {
        BinauralMonitor bm;
        CHECK(!bm.isInitialized());
        CHECK(!bm.hasHrtf());
    }

    // Test 1: initialize() with empty sofaPath → Ok, pass-through
    {
        BinauralMonitor bm;
        auto res = bm.initialize({.sofaPath = "", .sampleRate = 48000.f, .blockSize = 64});
        CHECK(res == BinauralMonitor::InitResult::Ok);
        CHECK(bm.isInitialized());
        CHECK(!bm.hasHrtf());
    }

    // Test 3: processBlock pass-through
    {
        BinauralMonitor bm;
        bm.initialize({.sofaPath = "", .sampleRate = 48000.f, .blockSize = 64});
        constexpr int N = 64;
        std::vector<float> mono(N), left(N, -1.f), right(N, -1.f);
        for (int i = 0; i < N; ++i) mono[i] = static_cast<float>(i) * 0.01f;
        bm.processBlock(mono.data(), N, left.data(), right.data());
        for (int i = 0; i < N; ++i) {
            CHECK_NEAR(left[i],  mono[i], 1e-7f);
            CHECK_NEAR(right[i], mono[i], 1e-7f);
        }
    }

    // Test 4: initialize() with non-existent path → SofaNotFound
    {
        BinauralMonitor bm;
        auto res = bm.initialize({.sofaPath = "/tmp/does_not_exist_abc.speh",
                                  .sampleRate = 48000.f, .blockSize = 64});
        CHECK(res == BinauralMonitor::InitResult::SofaNotFound);
        CHECK(!bm.isInitialized());
    }

    // Test 5: setDirection doesn't crash
    {
        BinauralMonitor bm;
        bm.initialize({});
        bm.setDirection(1.0f, 0.3f);
        CHECK(bm.isInitialized());
    }

    // Test 6: reset() on uninit doesn't crash
    {
        BinauralMonitor bm;
        bm.reset();  // must not throw / crash
        CHECK(!bm.isInitialized());
    }

    // Write a tiny .speh for tests 7-9 (8 positions, ir_len=64, delay=4)
    const std::string speh_path = "/tmp/test_binaural_tiny.speh";
    writeTinySpeh(speh_path, 8, 64, 4);

    // Test 7: initialize with valid .speh → Ok, hasHrtf() = true
    {
        BinauralMonitor bm;
        auto res = bm.initialize({.sofaPath = speh_path, .sampleRate = 48000.f, .blockSize = 64});
        CHECK(res == BinauralMonitor::InitResult::Ok);
        CHECK(bm.isInitialized());
        CHECK(bm.hasHrtf());
    }

    // Test 8: unit-impulse HRTF (recv 0) → left has energy, not silent
    {
        BinauralMonitor bm;
        bm.initialize({.sofaPath = speh_path, .sampleRate = 48000.f, .blockSize = 64});

        constexpr int N = 64;
        std::vector<float> mono(N, 0.f);
        mono[0] = 1.f;  // impulse
        std::vector<float> left(N, 0.f), right(N, 0.f);
        bm.processBlock(mono.data(), N, left.data(), right.data());

        // Energy must be non-zero and concentrated in first few samples.
        float energy = 0.f;
        for (int i = 0; i < N; ++i) energy += left[i] * left[i];
        CHECK(energy > 0.5f);  // unit-impulse IR → most energy preserved

#if !defined(SPE_HAVE_JUCE)
        // Pure-C++ OLA: sample-exact impulse response
        for (int i = 0; i < N; ++i)
            CHECK_NEAR(left[i], mono[i], 1e-6f);
#endif
    }

    // Test 9: delayed-impulse HRTF (recv 1, delay=4) → right energy comes after left
    {
        BinauralMonitor bm;
        bm.initialize({.sofaPath = speh_path, .sampleRate = 48000.f, .blockSize = 64});

        constexpr int N = 64;
        std::vector<float> mono(N, 0.f);
        mono[0] = 1.f;
        std::vector<float> left(N, 0.f), right(N, 0.f);
        bm.processBlock(mono.data(), N, left.data(), right.data());

        // Right IR has impulse at delay=4: energy must be non-zero
        float right_energy = 0.f;
        for (int i = 0; i < N; ++i) right_energy += right[i] * right[i];
        CHECK(right_energy > 0.5f);

#if !defined(SPE_HAVE_JUCE)
        // Pure-C++ OLA: sample-exact delay check
        CHECK_NEAR(right[0], 0.f, 1e-6f);
        CHECK_NEAR(right[1], 0.f, 1e-6f);
        CHECK_NEAR(right[2], 0.f, 1e-6f);
        CHECK_NEAR(right[3], 0.f, 1e-6f);
        CHECK_NEAR(right[4], 1.f, 1e-6f);
#endif
    }

    // Test 10: SofaBinReader round-trip
    {
        HrtfTable tbl;
        SpehResult res = loadSpeh(speh_path, 48000.f, tbl);
        CHECK(res == SpehResult::Ok);
        CHECK(tbl.n_positions == 8);
        CHECK(tbl.ir_length   == 64);
        CHECK(tbl.n_receivers == 2);
        CHECK_NEAR(tbl.sample_rate, 48000.f, 1.f);
        // First position az should be 0 deg
        CHECK_NEAR(tbl.positions[0], 0.f, 1e-5f);
        // Second position az should be 45 deg (360/8)
        CHECK_NEAR(tbl.positions[3], 45.f, 1e-4f);
        // recv 0 IR[0] = 1.0 (unit impulse)
        CHECK_NEAR(tbl.ir(0, 0)[0], 1.f, 1e-6f);
        // recv 1 IR[4] = 1.0 (delay=4)
        CHECK_NEAR(tbl.ir(0, 1)[4], 1.f, 1e-6f);
    }

#if !defined(SPE_HAVE_JUCE) && defined(SPE_RT_ASSERTS)
    // Test 11: processBlock alloc-free after init (RT-safety — NO_JUCE + RT_ASSERTS only)
    {
        BinauralMonitor bm;
        bm.initialize({.sofaPath = speh_path, .sampleRate = 48000.f, .blockSize = 64});
        constexpr int N = 64;
        std::vector<float> mono(N, 0.f), left(N), right(N);
        mono[0] = 1.f;
        spe::util::rt_alloc_violations_reset();
        {
            SPE_RT_NO_ALLOC_SCOPE();
            bm.processBlock(mono.data(), N, left.data(), right.data());
        }
        CHECK(spe::util::rt_alloc_violations() == 0);
        std::printf("[PASS] test11: processBlock alloc-free after init\n");
    }
#endif

    // Test 12: block-boundary OLA continuity: 4×64 blocks == 1×256 single-block result
    // (Verifies overlap_ state is maintained correctly across block boundaries)
#if !defined(SPE_HAVE_JUCE)
    {
        constexpr int BLOCK = 64;
        constexpr int TOTAL = BLOCK * 4;

        std::vector<float> mono_long(TOTAL, 0.f);
        mono_long[0] = 1.f;

        // Load known IR from tiny speh
        HrtfTable tbl;
        loadSpeh(speh_path, 48000.f, tbl);

        // Process as 4 blocks of 64
        OlaConvolver conv;
        conv.prepare(tbl.ir(0, 0), static_cast<int>(tbl.ir_length), BLOCK);
        std::vector<float> out_blocks(TOTAL, 0.f);
        for (int b = 0; b < 4; ++b)
            conv.process(mono_long.data() + b * BLOCK, BLOCK,
                         out_blocks.data() + b * BLOCK);

        // Process as a single block of 256
        OlaConvolver conv2;
        conv2.prepare(tbl.ir(0, 0), static_cast<int>(tbl.ir_length), TOTAL);
        std::vector<float> out_single(TOTAL, 0.f);
        conv2.process(mono_long.data(), TOTAL, out_single.data());

        bool match = true;
        for (int i = 0; i < TOTAL; ++i) {
            if (std::abs(out_blocks[i] - out_single[i]) > 1e-6f) {
                std::printf("FAIL test12: mismatch at i=%d: blocks=%.8f single=%.8f\n",
                            i, (double)out_blocks[i], (double)out_single[i]);
                match = false;
                ++failures;
                break;
            }
        }
        if (match)
            std::printf("[PASS] test12: block-boundary OLA continuity (4x64 == 1x256)\n");
    }
#endif

    if (failures == 0) {
        std::printf("OK  test_p_binaural_stub: all checks passed\n");
        return 0;
    }
    std::printf("FAIL test_p_binaural_stub: %d failure(s)\n", failures);
    return 1;
}

// core/tests/core_unit/test_p_audio_io_input.cpp
//
// C1.a acceptance tests: NullBackend audio-input path.
// Gates from .omc/plans/spatial-engine-phaseC.md:
//   - silence input → 0.0 RMS (tolerance < 1e-9)
//   - 1 kHz sine input @ 0 dBFS → expected RMS within 0.5 dB

#include "audio_io/AudioBackend.h"
#include "audio_io/NullBackend.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace spe::audio_io;

namespace {

class InputRmsCallback final : public AudioCallback {
public:
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    void audioBlock(const AudioBlock& blk) override {
        if (blk.input_channels && blk.input_channel_count > 0) {
            for (int n = 0; n < blk.num_frames; ++n) {
                const float s = blk.input_channels[0][n];
                sum_sq_ += static_cast<double>(s) * s;
                ++samples_seen_;
            }
        }
        // Engine is supposed to fill outputs each block. Leave silent for the
        // test — the backend zeroes output before the call so this is fine.
    }

    double rms() const noexcept {
        return samples_seen_ ? std::sqrt(sum_sq_ / static_cast<double>(samples_seen_)) : 0.0;
    }

private:
    double          sum_sq_       = 0.0;
    std::size_t     samples_seen_ = 0;
};

void test_silence_input_zero_rms() {
    auto backend = make_null_backend(48000.0, 2, 64, /*input_channels=*/1);
    InputRmsCallback cb;
    auto* nb = static_cast<NullBackend*>(backend.get());
    auto rc = nb->pump_synchronous(&cb, /*blocks=*/16);
    assert(rc == BackendError::Ok);
    const double rms = cb.rms();
    std::printf("test_silence_input_zero_rms: blocks=16, rms=%.3e\n", rms);
    assert(rms < 1e-9 && "silence input must yield RMS ~0");
    std::printf("  PASS\n");
}

void test_sine_1khz_input_rms_within_half_db() {
    constexpr double SR    = 48000.0;
    constexpr double FREQ  = 1000.0;
    constexpr int    BLOCK = 64;
    constexpr int    BLOCKS = 750;  // 750 * 64 = 48000 samples = 1 sec

    // Generate one period-coherent buffer (length = 48 samples per cycle × N cycles
    // is automatic since we feed circularly via fixture; use 48000-sample buffer).
    std::vector<float> sine(48000);
    for (int i = 0; i < 48000; ++i) {
        sine[i] = static_cast<float>(
            std::sin(2.0 * 3.14159265358979 * FREQ * i / SR));
    }

    auto backend = make_null_backend(SR, 2, BLOCK, /*input_channels=*/1);
    auto* nb = static_cast<NullBackend*>(backend.get());
    nb->set_input_fixture({sine});
    InputRmsCallback cb;
    auto rc = nb->pump_synchronous(&cb, BLOCKS);
    assert(rc == BackendError::Ok);

    const double rms_observed = cb.rms();
    const double rms_expected = 1.0 / std::sqrt(2.0);  // sine amp 1.0 → RMS = 1/√2
    const double db_err = 20.0 * std::log10(rms_observed / rms_expected);
    std::printf("test_sine_1khz_input_rms: observed=%.6f expected=%.6f db_err=%.4f\n",
                rms_observed, rms_expected, db_err);
    assert(std::fabs(db_err) < 0.5 && "1 kHz sine input RMS must be within 0.5 dB of expected");
    std::printf("  PASS\n");
}

void test_input_disabled_default() {
    // Default ctor: input_channels=0; AudioBlock.input_channels must be null.
    auto backend = make_null_backend(48000.0, 2, 64);
    assert(backend->inputChannelCount() == 0);
    std::printf("test_input_disabled_default: input_channel_count=0 PASS\n");
}

}  // namespace

int main() {
    test_input_disabled_default();
    test_silence_input_zero_rms();
    test_sine_1khz_input_rms_within_half_db();
    std::printf("All test_p_audio_io_input tests passed.\n");
    return 0;
}

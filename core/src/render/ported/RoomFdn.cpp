// core/src/render/ported/RoomFdn.cpp
// See RoomFdn.h for provenance (immersive-audio-engine RoomEngine @ f2cb796).

#include "render/ported/RoomFdn.h"

#include <algorithm>
#include <cmath>

namespace iae {

namespace {
// Allpass step (RoomEngine.cpp:42-49): y = -c*x + line[rp]; line[rp] = x + c*y.
[[nodiscard]] inline float allpassStep(float* line, int len, int& pos,
                                       float coeff, float x) noexcept {
    auto wrap = [](int v, int m) { v %= m; if (v < 0) v += m; return v; };
    const int rp = wrap(pos, len);
    const float y = -coeff * x + line[rp];
    line[rp] = x + coeff * y;
    pos = wrap(pos + 1, len);
    return y;
}
} // namespace

void RoomFdn::fastHadamard8(float* a) noexcept {
    // RoomEngine.cpp:81-92 — Sylvester order-8 (unnormalised).
    for (int hs = 1; hs < 8; hs *= 2)
        for (int i = 0; i < 8; i += 2 * hs)
            for (int j = i; j < i + hs; ++j) {
                const float u = a[j];
                const float v = a[j + hs];
                a[j]      = u + v;
                a[j + hs] = u - v;
            }
}

void RoomFdn::prepare(double sampleRateHz, int maxBlockSamples) {
    // RoomEngine.cpp:199-217 (FDN delay-line setup).
    sampleRate_ = std::max(4000.0, sampleRateHz);
    maxBlock_   = std::max(1, maxBlockSamples);

    static constexpr int kBaseDelays48000[kOrder] =
        { 601, 733, 877, 997, 1091, 1187, 1321, 1487 };
    const double srScale = sampleRate_ / 48000.0;
    for (int i = 0; i < kOrder; ++i) {
        const int L = std::clamp(
            (int) std::lround((double) kBaseDelays48000[i] * srScale), 64, 8192);
        delaySamples_[(size_t) i] = L;
        delayLines_[(size_t) i].assign((size_t) L + (size_t) maxBlock_ + 8, 0.f);
        writePos_[(size_t) i] = 0;
    }
    lpState_.fill(0.f);
    ap1_.fill(0.f);
    ap2_.fill(0.f);
    ap1Pos_ = ap2Pos_ = 0;
    ready_ = true;
}

void RoomFdn::reset() noexcept {
    for (auto& line : delayLines_) std::fill(line.begin(), line.end(), 0.f);
    writePos_.fill(0);
    lpState_.fill(0.f);
    ap1_.fill(0.f);
    ap2_.fill(0.f);
    ap1Pos_ = ap2Pos_ = 0;
}

void RoomFdn::process(const float* in, int n, float* outLines) noexcept {
    if (!ready_ || n <= 0) return;

    // Per-block loop gain / damping coefficients (RoomEngine.cpp:673-691).
    double sumDelay = 0.0;
    for (int k = 0; k < kOrder; ++k) sumDelay += (double) delaySamples_[(size_t) k];
    const float tMean = (float) (sumDelay / ((double) kOrder * sampleRate_));
    const float t60   = std::clamp(params_.t60Seconds, 0.2f, 6.f);
    float gLoop = std::exp(-6.90775527898114f * tMean / std::max(0.2f, t60));
    gLoop = std::clamp(gLoop, 0.02f, 0.985f);
    gLoop *= 0.918f; // metal/comb mitigation (reference)
    const float gH  = gLoop * (1.0f / std::sqrt(8.f));
    const float inj = (1.0f / std::sqrt(8.f)) * 0.88f;

    const float fc    = std::clamp(params_.hfDecayCornerHz, 800.f, 16000.f);
    const float ratio = std::clamp(params_.hfDecayRatio01, 0.05f, 1.f);
    const float a         = std::exp(-6.28318530717959f * fc / (float) sampleRate_);
    const float oneMinusA = 1.f - a;
    const float bright    = 0.14f + 0.86f * ratio;

    for (int i = 0; i < n; ++i) {
        float x = in[i];
        x = allpassStep(ap1_.data(), (int) ap1_.size(), ap1Pos_, ap1Coeff_, x);
        x = allpassStep(ap2_.data(), (int) ap2_.size(), ap2Pos_, ap2Coeff_, x);

        float d[kOrder];
        for (int k = 0; k < kOrder; ++k) {
            auto& line = delayLines_[(size_t) k];
            const int len = (int) line.size();
            const int L   = delaySamples_[(size_t) k];
            const int ri  = wrapIndex(writePos_[(size_t) k] - L, len);
            const float raw = line[(size_t) ri];
            float& lp = lpState_[(size_t) k];
            lp = a * lp + oneMinusA * raw;
            d[k] = raw * bright + lp * (1.f - bright);
        }

        float m[kOrder];
        for (int k = 0; k < kOrder; ++k) m[k] = d[k];
        fastHadamard8(m);
        for (int k = 0; k < kOrder; ++k) m[k] *= gH;

        for (int k = 0; k < kOrder; ++k) {
            auto& line = delayLines_[(size_t) k];
            const int len = (int) line.size();
            int& wp = writePos_[(size_t) k];
            line[(size_t) wp] = inj * x + m[k];
            wp = wrapIndex(wp + 1, len);
        }

        // Emit the per-line damped taps (the signal the spatialiser pans).
        for (int k = 0; k < kOrder; ++k)
            outLines[(size_t) k * (size_t) n + (size_t) i] = d[k];
    }
}

} // namespace iae

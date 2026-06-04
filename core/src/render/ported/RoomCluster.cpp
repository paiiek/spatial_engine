// core/src/render/ported/RoomCluster.cpp
// See RoomCluster.h for provenance (immersive-audio-engine RoomEngine @ f2cb796).

#include "render/ported/RoomCluster.h"

#include <algorithm>
#include <cmath>

namespace iae {

void RoomCluster::prepare(double sampleRateHz) {
    // RoomEngine.cpp:204-206 (cluster bus + delay-line setup).
    sampleRate_ = std::max(4000.0, sampleRateHz);
    line_.assign(static_cast<size_t>(kLineLen), 0.f);
    writePos_ = 0;
    ready_ = true;
}

void RoomCluster::reset() noexcept {
    // RoomEngine.cpp:260-262.
    std::fill(line_.begin(), line_.end(), 0.f);
    writePos_ = 0;
}

void RoomCluster::process(const float* in, int n, float* out) noexcept {
    if (!ready_ || n <= 0) return;

    const int clLen = static_cast<int>(line_.size());
    // RoomEngine.cpp:608 — line is fixed at 16384 (>= 512); guard for safety.
    if (clLen < 512) {
        for (int i = 0; i < n; ++i) out[i] = 0.f;
        return;
    }

    // Per-block tap offsets / gain (RoomEngine.cpp:610-625).
    const float diff  = std::clamp(params_.diffusion01, 0.f, 1.f);
    const float V     = std::max(50.f, params_.virtualVolumeM3);
    // ref :612 — 0.11f * cbrt((double)V) evaluated in double, narrowed on store.
    // Cast the whole product (not cbrt alone) to keep bit-exact parity with the
    // reference while staying -Wconversion-clean.
    const float charM = static_cast<float>(0.11f * std::cbrt(static_cast<double>(V)));
    int d0 = static_cast<int>(std::lround(
        (static_cast<double>(charM) / 343.0) * sampleRate_ * 0.2));
    d0 = std::clamp(d0, 8, 1200);
    const int step = std::max(2, d0 / 10);
    std::array<int, kFeedforwardTaps> off {};
    for (int k = 0; k < kFeedforwardTaps; ++k) {
        int ok = d0 + k * step;
        ok = std::min(ok, clLen - 3);
        off[static_cast<size_t>(k)] = std::max(1, ok);
    }
    static constexpr float kClTri6[6]  = { 1.f, 2.f, 3.f, 3.f, 2.f, 1.f };
    static constexpr float kClTri6Inv  = 1.f / 12.f;
    const float clGain = (0.08f + 0.42f * diff) * kClTri6Inv * 0.5f;

    float* cLine = line_.data();
    for (int i = 0; i < n; ++i) {
        const float x = in[i];
        const int wi = wrapIndex(writePos_, clLen);
        cLine[static_cast<size_t>(wi)] = x;
        float sum = 0.f;
        for (int k = 0; k < kFeedforwardTaps; ++k) {
            const int ri = wrapIndex(wi - off[static_cast<size_t>(k)], clLen);
            sum += kClTri6[static_cast<size_t>(k)] * cLine[static_cast<size_t>(ri)];
        }
        writePos_ = wrapIndex(writePos_ + 1, clLen);
        out[i] = sum * clGain;
    }
}

} // namespace iae

// core/src/render/ported/SpeakerDecorrelation.cpp
// See SpeakerDecorrelation.h for provenance. Byte-faithful to the reference
// Source/SpeakerDecorrelation.cpp (delay store array→vector adaptation only).

#include "render/ported/SpeakerDecorrelation.h"

#include <algorithm>
#include <cmath>

namespace iae {

void SpeakerDecorrelationBank::prepare(double sampleRateHz, int maxSamplesPerBlock) noexcept {
    sr = sampleRateHz > 1.0 ? sampleRateHz : 48000.0;
    maxBlock = maxSamplesPerBlock > 0 ? maxSamplesPerBlock : 512;
    // Heap-allocate the per-channel delay rings once (mmhoa adaptation).
    for (auto& ch : channels)
        ch.delay.assign(static_cast<size_t>(kDelayCapacity), 0.f);
    reset();
}

void SpeakerDecorrelationBank::reset() noexcept {
    for (auto& ch : channels) {
        std::fill(ch.delay.begin(), ch.delay.end(), 0.f);
        ch.writePos = 0;
        ch.apZm1.fill(0.f);
        ch.delaySamples = 1;
        ch.stagesUsed = 1;
        ch.cfgHash = 0;
    }
}

void SpeakerDecorrelationBank::ensureConfigured(ChannelState& ch,
                                                int speakerIndex,
                                                float delaySpreadMs,
                                                int stages,
                                                float apCoeff01,
                                                std::uint32_t seed) noexcept {
    const int   st     = std::max(1, std::min(kMaxStages, stages));
    const float g      = std::max(0.02f, std::min(0.92f, apCoeff01));
    const float spread = std::max(0.f, std::min(24.f, delaySpreadMs));

    const float frac =
        static_cast<float>(hashSpeaker(static_cast<unsigned>(speakerIndex),
                                       seed ^ (static_cast<std::uint32_t>(st) * 2166136261u))
                           & 0xffffu)
        / 65535.f;
    const float delayMs = spread * frac;
    int dSamples = static_cast<int>(std::round(static_cast<double>(delayMs) * 0.001 * sr));
    dSamples = std::max(1, std::min(kDelayCapacity - 2, dSamples));

    const std::uint32_t h =
        static_cast<std::uint32_t>(dSamples)
        ^ (static_cast<std::uint32_t>(st) << 8)
        ^ (static_cast<std::uint32_t>(std::lround(g * 1000.f)) << 16);

    if (ch.cfgHash != h) {
        ch.cfgHash = h;
        ch.delaySamples = dSamples;
        ch.stagesUsed = st;
        ch.apG = g;
        std::fill(ch.delay.begin(), ch.delay.end(), 0.f);
        ch.writePos = 0;
        ch.apZm1.fill(0.f);
    }
}

void SpeakerDecorrelationBank::processChannel(int speakerIndex,
                                              float* ioBuffer,
                                              int numSamples,
                                              bool enabled,
                                              float mix01,
                                              int stages,
                                              float delaySpreadMs,
                                              float apCoeff01,
                                              std::uint32_t seed) noexcept {
    if (ioBuffer == nullptr || numSamples <= 0 || speakerIndex < 0
        || speakerIndex >= kPrototypeChannels)
        return;

    if (!enabled || mix01 <= 1.0e-5f)
        return;

    auto& ch = channels[static_cast<size_t>(speakerIndex)];
    ensureConfigured(ch, speakerIndex, delaySpreadMs, stages, apCoeff01, seed);

    const float wetAmt = std::max(0.f, std::min(1.f, mix01));
    const float dryAmt = std::max(0.f, std::min(1.f, std::sqrt(1.f - wetAmt * wetAmt)));

    const int cap = kDelayCapacity;
    const int ds  = ch.delaySamples;

    for (int i = 0; i < numSamples; ++i) {
        const float xIn = ioBuffer[static_cast<size_t>(i)];
        const int wp = ch.writePos;
        ch.delay[static_cast<size_t>(wp)] = xIn;
        const int rp = wp - ds;
        const int ri = rp >= 0 ? rp : rp + cap;
        float w = ch.delay[static_cast<size_t>(ri)];

        for (int s = 0; s < ch.stagesUsed; ++s)
            w = schroederAllpass(w, ch.apG, ch.apZm1[static_cast<size_t>(s)]);

        ioBuffer[static_cast<size_t>(i)] = dryAmt * xIn + wetAmt * w;
        ch.writePos = (wp + 1 >= cap) ? 0 : wp + 1;
    }

    (void) maxBlock;
}

} // namespace iae

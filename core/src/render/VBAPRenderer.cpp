// core/src/render/VBAPRenderer.cpp

#include "render/VBAPRenderer.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <utility>

namespace spe::render {

void VBAPRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                  double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());

    // v0.8 P1.3 (DSP-3) — bound the audio-thread scratch + ramp cap. ramps_
    // is std::array<…, 64> already; before P1.3 there was no >64 guard, so
    // a layout with 65+ speakers would have written OOB into ramps_. The
    // assert fires at prepareToPlay (control thread) so the audio thread
    // never sees an oversized N.
    assert(num_speakers_ <= 64
           && "VBAPRenderer: layout exceeds 64-speaker cap "
              "(ramps_/scratch fixed)");
    gain_scratch_.assign(static_cast<size_t>(num_speakers_), 0.f);

    // Reset all gain ramps to 0
    for (auto& obj_ramps : ramps_)
        for (int s = 0; s < num_speakers_; ++s)
            obj_ramps[s].reset(0.f);

    // Reset cache — pre-size gains vectors so processBlock never reallocates
    for (auto& slot : cache_slots_) {
        slot.key = UINT64_MAX;
        slot.gains.assign(static_cast<size_t>(layout.speakers.size()), 0.f);
    }
    cache_ring_.fill(0);
    ring_head_  = 0;
    cache_size_ = 0;
    cache_hits_ = cache_misses_ = 0;
}

void VBAPRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    const int N = static_cast<int>(objects.size());
    const int S = num_speakers_;

    // Clear output
    std::memset(out, 0, sizeof(float) * static_cast<size_t>(num_samples * S));

    for (int obj = 0; obj < N; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[obj];
        if (!src) continue;

        // Quantise (az,el,width) to 0.5 deg bins with offsets so UINT64_MAX is unreachable
        const int az_bin = static_cast<int>(std::lround(
            objects[obj].az_rad * (180.0f / 3.14159265f) / AZ_BIN_DEG)) + AZ_OFFSET;
        const int el_bin = static_cast<int>(std::lround(
            objects[obj].el_rad * (180.0f / 3.14159265f) / EL_BIN_DEG)) + EL_OFFSET;
        // Pack width into upper bits of el_bin word (width binned to 1-deg steps, offset 512)
        const int w_bin  = static_cast<int>(std::lround(
            objects[obj].width_rad * (180.0f / 3.14159265f))) + 512;
        const uint64_t key =
            (static_cast<uint64_t>(static_cast<uint32_t>(az_bin)) << 32) |
            (static_cast<uint64_t>(static_cast<uint16_t>(el_bin)) << 16) |
             static_cast<uint64_t>(static_cast<uint16_t>(w_bin));

        const std::vector<float>* gains_ptr = nullptr;
        {
            int probe = static_cast<int>(key & static_cast<uint64_t>(MAX_CACHE - 1));
            for (int i = 0; i < MAX_CACHE; ++i, probe = (probe + 1) & (MAX_CACHE - 1)) {
                if (cache_slots_[probe].key == key) {
                    ++cache_hits_;
                    gains_ptr = &cache_slots_[probe].gains;
                    break;
                }
                if (cache_slots_[probe].key == UINT64_MAX) {
                    ++cache_misses_;
                    // Evict FIFO head if at capacity (keeps load <= 50%)
                    if (cache_size_ >= RING_CAP) {
                        int evict = cache_ring_[ring_head_];
                        cache_slots_[evict].key = UINT64_MAX;
                        ring_head_ = (ring_head_ + 1) % RING_CAP;
                        --cache_size_;
                    }
                    // v0.8 P1.3 (DSP-3) — RT-safe cold-miss compute. Pre-P1.3
                    // this called vbap_gain() which returned a std::vector
                    // and allocated for internal temporaries (azs/idx/ux/uy/
                    // uz/ang/cands) on the audio thread inside the
                    // SPE_RT_NO_ALLOC_SCOPE(). The new vbap_gain_into()
                    // writes into the caller-provided member scratch and
                    // uses stack arrays internally (capped at 64 speakers).
                    // Source width → MDAP (Multiple-Direction Amplitude
                    // Panning): width_rad is the spread angle (radians),
                    // converted to degrees and clamped to 40° inside the
                    // helper. width≈0 degenerates to the point-source VBAP.
                    // MDAP samples K=8 directions around (az,el) and sums their
                    // (elevation-masked) VBAP gains, energy-normalised — a real
                    // spatial spread, replacing the old uniform-blend approximation.
                    const float w_rad = objects[obj].width_rad;
                    const int written = (w_rad > 1e-4f)
                        ? AlgorithmAnalyticReference::vbap_mdap_gain_into(
                              layout_, objects[obj].az_rad, objects[obj].el_rad,
                              w_rad * (180.0f / 3.14159265f),
                              gain_scratch_.data(),
                              static_cast<int>(gain_scratch_.size()))
                        : AlgorithmAnalyticReference::vbap_gain_into(
                              layout_, objects[obj].az_rad, objects[obj].el_rad,
                              gain_scratch_.data(),
                              static_cast<int>(gain_scratch_.size()));
                    cache_slots_[probe].key = key;
                    // Copy from scratch into the cached slot (slot.gains
                    // already sized at prepareToPlay — no realloc).
                    auto& slot_gains = cache_slots_[probe].gains;
                    const int n_copy = std::min(written, static_cast<int>(slot_gains.size()));
                    for (int g_i = 0; g_i < n_copy; ++g_i)
                        slot_gains[static_cast<size_t>(g_i)] =
                            gain_scratch_[static_cast<size_t>(g_i)];
                    cache_ring_[(ring_head_ + cache_size_) % RING_CAP] = probe;
                    ++cache_size_;
                    gains_ptr = &cache_slots_[probe].gains;
                    break;
                }
            }
            if (!gains_ptr) {
                // v0.8 P1.3 — Table 100% full path was the thread_local
                // std::vector fallback. With the cache at <=50% load by
                // construction this branch is unreachable; if it ever did
                // fire pre-P1.3 it allocated on the audio thread. Now we
                // fall through to the gain_scratch_ member buffer (already
                // populated by the linear probe above when no key matched
                // and no empty slot was found is impossible — but defend
                // by computing into scratch and pointing gains_ptr there).
                ++cache_misses_;
                const float w_rad2 = objects[obj].width_rad;
                if (w_rad2 > 1e-4f) {
                    (void)AlgorithmAnalyticReference::vbap_mdap_gain_into(
                        layout_, objects[obj].az_rad, objects[obj].el_rad,
                        w_rad2 * (180.0f / 3.14159265f),
                        gain_scratch_.data(),
                        static_cast<int>(gain_scratch_.size()));
                } else {
                    (void)AlgorithmAnalyticReference::vbap_gain_into(
                        layout_, objects[obj].az_rad, objects[obj].el_rad,
                        gain_scratch_.data(),
                        static_cast<int>(gain_scratch_.size()));
                }
                gains_ptr = &gain_scratch_;
            }
        }
        const auto& gains = *gains_ptr;

        // Update ramp targets
        for (int s = 0; s < S; ++s)
            ramps_[obj][s].setTarget(gains[s], num_samples);

        // Mix per sample
        for (int n = 0; n < num_samples; ++n) {
            float x = src[n];
            for (int s = 0; s < S; ++s)
                out[n * S + s] += x * ramps_[obj][s].next();
        }
    }
}

} // namespace spe::render

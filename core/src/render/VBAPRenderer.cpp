// core/src/render/VBAPRenderer.cpp

#include "render/VBAPRenderer.h"
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
                    // NOTE: vbap_gain allocates internally (std::vector temporaries).
                    // Cold-miss path allocates; warm-hit path (above) is alloc-free.
                    auto computed = AlgorithmAnalyticReference::vbap_gain(
                        layout_, objects[obj].az_rad, objects[obj].el_rad);
                    // Width fan-out: blend point-source gains with uniform spread.
                    // width=0 → pure VBAP; width=π → half uniform / half VBAP.
                    // Blend factor w ∈ [0,1]: w = width_rad / π.
                    // g_out[i] = (1-w)*g[i] + w*(1/sqrt(N))
                    // This guarantees ≥3 nonzero gains when w>0 and N≥3.
                    const float w_rad = objects[obj].width_rad;
                    if (w_rad > 1e-4f) {
                        const float w = w_rad / 3.14159265f;  // [0,1]
                        const float uniform = 1.0f / std::sqrt(static_cast<float>(computed.size()));
                        float energy = 0.f;
                        for (auto& g : computed) {
                            g = (1.f - w) * g + w * uniform;
                            energy += g * g;
                        }
                        // Re-normalise to energy = 1
                        if (energy > 1e-8f) {
                            const float inv_rms = 1.0f / std::sqrt(energy);
                            for (auto& g : computed) g *= inv_rms;
                        }
                    }
                    cache_slots_[probe].key = key;
                    cache_slots_[probe].gains = std::move(computed); // same capacity: no realloc
                    cache_ring_[(ring_head_ + cache_size_) % RING_CAP] = probe;
                    ++cache_size_;
                    gains_ptr = &cache_slots_[probe].gains;
                    break;
                }
            }
            if (!gains_ptr) {
                // Table 100% full (unreachable at <=50% load) — compute without caching
                ++cache_misses_;
                static thread_local std::vector<float> tmp;
                tmp = AlgorithmAnalyticReference::vbap_gain(
                    layout_, objects[obj].az_rad, objects[obj].el_rad);
                const float w_rad2 = objects[obj].width_rad;
                if (w_rad2 > 1e-4f) {
                    const float w2 = w_rad2 / 3.14159265f;
                    const float uniform2 = 1.0f / std::sqrt(static_cast<float>(tmp.size()));
                    float energy2 = 0.f;
                    for (auto& g : tmp) {
                        g = (1.f - w2) * g + w2 * uniform2;
                        energy2 += g * g;
                    }
                    if (energy2 > 1e-8f) {
                        const float inv2 = 1.0f / std::sqrt(energy2);
                        for (auto& g : tmp) g *= inv2;
                    }
                }
                gains_ptr = &tmp;
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

// core/src/render/WFSRenderer.cpp

#include "render/WFSRenderer.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace spe::render {

// PRECONDITION (thread-safety, load-bearing): the caller guarantees no concurrent
// ensureAllocated() while prepareToPlay runs. In SpatialEngine this holds because
// ensureAllocated()'s only off-thread caller is the OSC sink on osc_backend_'s
// udp_thread_, whose entire lifetime is strictly nested inside the prepared window
// (prepareToPlay calls osc_backend_.start() LAST; releaseResources calls
// osc_backend_.stop() — which JOINS udp_thread_ — FIRST). So this clear()/resize()
// can never race ensureAllocated(). Do not break that nesting without revisiting
// the ready_ handshake (it guards processBlock, NOT prepareToPlay vs ensureAllocated).
void WFSRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                 double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());

    // Compute spatial aliasing fade factor
    float spacing = layout.max_spacing_m();
    if (spacing < 1e-6f) spacing = 1e-6f;
    float alias_limit = spe::SOUND_C / (2.f * F_MAX); // ~21.4 mm
    alias_fade_gain_ = std::min(1.f, alias_limit / spacing);

    // F5-M3b (Option C): DO NOT allocate delays_/ramps_ here — that is the
    // dominant footprint term and a deployment that never uses WFS should not
    // pay it. Re-gate: any prior allocation is discarded and ready_ is reset so
    // the next WFS activation re-allocates against the (possibly new) layout.
    // delays_/ramps_ are only (re)sized here while audio is stopped (prepare is
    // the device-open path); the live algorithm-switch path goes through
    // ensureAllocated()'s release store.
    ready_.store(false, std::memory_order_release);
    delays_.clear();
    ramps_.clear();
}

void WFSRenderer::ensureAllocated()
{
    // Control thread (non-RT). Idempotent: allocate exactly once.
    if (ready_.load(std::memory_order_acquire)) return;

    const int total = spe::MAX_OBJECTS * num_speakers_;
    delays_.resize(total);
    ramps_.resize(total);
    for (auto& d : delays_) d.prepareToPlay(sr_);
    for (int obj = 0; obj < spe::MAX_OBJECTS; ++obj)
        for (int s = 0; s < num_speakers_; ++s)
            ramps_[flat_idx(obj, s)].reset(0.f);

    // Publish AFTER the storage is fully built. The audio thread's acquire-load
    // of ready_ in processBlock pairs with this release store, so a thread that
    // observes ready_==true is guaranteed to see the completed delays_/ramps_.
    ready_.store(true, std::memory_order_release);
}

void WFSRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    const int N = static_cast<int>(objects.size());
    const int S = num_speakers_;

    std::memset(out, 0, sizeof(float) * static_cast<size_t>(num_samples * S));

    // F5-M3b: render silent until the control thread has allocated & published
    // delays_/ramps_ (acquire pairs with ensureAllocated's release). This branch
    // is the only audio-path cost of Option C: one atomic acquire-load + an early
    // return. delays_/ramps_ are guaranteed non-empty & fully built once true.
    if (!ready_.load(std::memory_order_acquire)) return;

    for (int obj = 0; obj < N && obj < spe::MAX_OBJECTS; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[obj];
        if (!src) continue;

        float az  = objects[obj].az_rad;
        float el  = objects[obj].el_rad;
        float d   = std::max(objects[obj].dist_m, 0.01f);

        float sx = d * std::sin(az) * std::cos(el);
        float sy = d * std::sin(el);
        float sz = d * std::cos(az) * std::cos(el);

        // Width: scale gain of neighbouring speakers.
        // width=0 → pure distance-based gain; width>0 → add spread boost to neighbours.
        // We compute a per-speaker width_boost multiplier (1.0 for point source).
        const float w_rad = objects[obj].width_rad;

        // Find nearest speaker index for width neighbour logic
        int nearest_s = 0;
        float nearest_dist = 1e9f;
        for (int s = 0; s < S; ++s) {
            const auto& spk = layout_.speakers[s];
            float dx = spk.x - sx, dy = spk.y - sy, dz = spk.z - sz;
            float r = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (r < nearest_dist) { nearest_dist = r; nearest_s = s; }
        }

        for (int s = 0; s < S; ++s) {
            const auto& spk = layout_.speakers[s];
            float dx = spk.x - sx;
            float dy = spk.y - sy;
            float dz = spk.z - sz;
            float r  = std::sqrt(dx*dx + dy*dy + dz*dz);
            r = std::max(r, 0.01f);

            float delay_samples = r / spe::SOUND_C * static_cast<float>(sr_);
            float gain = alias_fade_gain_ / std::sqrt(r);

            // Width spread: neighbours within ±2 of nearest speaker get extra gain
            if (w_rad > 1e-4f) {
                int dist_spk = std::abs(s - nearest_s);
                // Wrap-around distance on ring
                dist_spk = std::min(dist_spk, S - dist_spk);
                // Spread radius: width=π/2 → 2 neighbours, width=π → 3 neighbours
                const float spread_w = w_rad / 3.14159265f;  // [0,1]
                const float boost = spread_w * std::exp(-static_cast<float>(dist_spk) * 1.5f);
                gain *= (1.f + boost);
            }

            auto& ramp  = ramps_[flat_idx(obj, s)];
            auto& delay = delays_[flat_idx(obj, s)];
            ramp.setTarget(gain, num_samples);

            for (int n = 0; n < num_samples; ++n) {
                float g       = ramp.next();
                float delayed = delay.processSample(src[n], delay_samples);
                out[n * S + s] += delayed * g;
            }
        }
    }
}

} // namespace spe::render

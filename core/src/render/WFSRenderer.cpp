// core/src/render/WFSRenderer.cpp

#include "render/WFSRenderer.h"
#include "render/AlgorithmAnalyticReference.h"
#include "render/ported/Wfs.h"
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

    // Precompute ported-kernel speaker geometry in mmhoa-native coordinates
    // (x=right, y=up, z=front). The WFS kernel is frame-agnostic, so no Y<->Z
    // swap is needed. Speaker "forward" is the unit vector toward the listener
    // (origin) — matching the reference's "orientation stored toward listener"
    // convention; the kernel negates it internally to get the radiating face.
    const int S = std::min(num_speakers_, MAX_SPEAKERS);
    for (int s = 0; s < S; ++s) {
        const auto& spk = layout.speakers[s];
        spk_pos_[s]  = iae::Vec3{ spk.x, spk.y, spk.z };
        spk_fwd_[s]  = iae::normalized(iae::Vec3{ -spk.x, -spk.y, -spk.z });
        spk_kind_[s] = iae::SpeakerKind::Frontal;
    }

    // F5-M3b (Option C): DO NOT allocate delays_/ramps_ here — that is the
    // dominant footprint term and a deployment that never uses WFS should not
    // pay it. Re-gate: any prior allocation is discarded and ready_ is reset so
    // the next WFS activation re-allocates against the (possibly new) layout.
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
    const int S = std::min(num_speakers_, MAX_SPEAKERS);

    std::memset(out, 0, sizeof(float) * static_cast<size_t>(num_samples * num_speakers_));

    // F5-M3b: render silent until the control thread has allocated & published
    // delays_/ramps_ (acquire pairs with ensureAllocated's release).
    if (!ready_.load(std::memory_order_acquire)) return;
    if (S <= 0) return;

    const float blend = std::clamp(params_.vbapGainBlend, 0.f, 1.f);

    for (int obj = 0; obj < N && obj < spe::MAX_OBJECTS; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[obj];
        if (!src) continue;

        const float az = objects[obj].az_rad;
        const float el = objects[obj].el_rad;
        const float d  = std::max(objects[obj].dist_m, 0.01f);

        // Virtual source position in mmhoa-native frame (same as speaker frame).
        const iae::Vec3 vsrc{ d * std::sin(az) * std::cos(el),
                              d * std::sin(el),
                              d * std::cos(az) * std::cos(el) };

        // Per-speaker gains + relative delays from the reference WFS kernel:
        // plane-wave / curvature / obliquity / shaping are all encoded here.
        std::array<float, MAX_SPEAKERS> g{};
        std::array<float, MAX_SPEAKERS> dly{};
        iae::computeWavefieldSynthesisDriving(
            spk_pos_.data(), spk_fwd_.data(), spk_kind_.data(),
            static_cast<size_t>(S), vsrc, static_cast<float>(sr_),
            delay_ref_, params_, plane_wave_,
            /*layerMask*/ nullptr, g.data(), dly.data());

        // Optional VBAP gain blend (full-mode axis). Reuse the native analytic
        // VBAP (shares the ②/③ elevation + 2D/3D-aware core). Delays are scaled
        // by (1-blend): blend toward pure VBAP collapses the wavefront delays.
        if (blend > 1.0e-6f) {
            std::array<float, MAX_SPEAKERS> vg{};
            const int nv = AlgorithmAnalyticReference::vbap_gain_into(
                layout_, az, el, vg.data(), S);
            if (nv == S) {
                const float om = 1.f - blend;
                float ss = 0.f;
                for (int s = 0; s < S; ++s) {
                    g[s] = om * g[s] + blend * vg[s];
                    dly[s] *= om;
                    ss += g[s] * g[s];
                }
                if (ss > 1.0e-8f) {
                    const float k = std::sqrt(1.f / ss);
                    for (int s = 0; s < S; ++s) g[s] *= k;
                }
            }
        }

        for (int s = 0; s < S; ++s) {
            auto& ramp  = ramps_[flat_idx(obj, s)];
            auto& delay = delays_[flat_idx(obj, s)];
            ramp.setTarget(g[s], num_samples);
            const float delay_samples = dly[s];

            for (int n = 0; n < num_samples; ++n) {
                const float gg      = ramp.next();
                const float delayed = delay.processSample(src[n], delay_samples);
                out[n * num_speakers_ + s] += delayed * gg;
            }
        }
    }
}

} // namespace spe::render

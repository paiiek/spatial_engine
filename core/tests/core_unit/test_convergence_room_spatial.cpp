// test_convergence_room_spatial.cpp
// Dreamscape Convergence ⑥b — the spatial fan-out used by the room engine:
// the 8 FDN line taps are panned to the 8 cube-corner directions via the native
// analytic VBAP. This test verifies, deterministically, that those 8 directions
// route to a SPREAD of distinct speakers on a 3D layout (not collapsed onto one),
// which is the property that makes the late reverb spatial rather than mono.

#include "render/AlgorithmAnalyticReference.h"
#include "geometry/SpeakerLayout.h"
#include "core/Constants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>

using namespace spe;
using namespace spe::geometry;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

static constexpr float kPi = 3.14159265358979323846f;

// Two-ring dome (lower el=-20°, upper el=+30°), per_ring speakers each.
static SpeakerLayout make_dome(int per_ring) {
    SpeakerLayout l; l.name = "room_dome"; l.regularity = Regularity::IRREGULAR;
    int ch = 1;
    for (float el_deg : {-20.f, 30.f}) {
        const float el = el_deg * kPi / 180.f;
        for (int i = 0; i < per_ring; ++i) {
            const float az = (-kPi) + 2.f * kPi * (float) i / (float) per_ring;
            const float ce = std::cos(el);
            Speaker s; s.channel = ch++;
            s.x = ce * std::sin(az); s.y = std::sin(el); s.z = ce * std::cos(az);
            l.speakers.push_back(s);
        }
    }
    return l;
}

int main() {
    const auto layout = make_dome(8);          // 16-speaker 3D dome
    const int N = (int) layout.speakers.size();

    // The 8 cube corners (mmhoa frame: x=right, y=up, z=front), matching the
    // engine's fdn_line_gains_ precompute.
    static constexpr float kCorner[8][3] = {
        { 1, 1, 1}, { 1, 1,-1}, { 1,-1, 1}, { 1,-1,-1},
        {-1, 1, 1}, {-1, 1,-1}, {-1,-1, 1}, {-1,-1,-1},
    };
    const float inv = 1.f / std::sqrt(3.f);

    std::set<int> argmaxSpeakers;
    int upperHits = 0, lowerHits = 0;
    for (int k = 0; k < 8; ++k) {
        const float x = kCorner[k][0] * inv;
        const float y = kCorner[k][1] * inv;
        const float z = kCorner[k][2] * inv;
        const float az = std::atan2(x, z);
        const float el = std::asin(std::clamp(y, -1.f, 1.f));

        std::array<float, spe::MAX_SPEAKERS> g{};
        const int got = render::AlgorithmAnalyticReference::vbap_gain_into(
            layout, az, el, g.data(), spe::MAX_SPEAKERS);
        CHECK(got == N);

        // gains must be a valid non-empty partition (Σg² ≈ 1)
        float sumsq = 0.f; int amax = 0;
        for (int s = 0; s < N; ++s) { sumsq += g[s] * g[s]; if (g[s] > g[amax]) amax = s; }
        CHECK(std::abs(sumsq - 1.f) < 1.0e-3f);
        CHECK(g[amax] > 0.f);
        argmaxSpeakers.insert(amax);
        if (amax >= N / 2) ++upperHits; else ++lowerHits;   // upper ring = idx >= 8
        std::printf("[corner %d] az=%+.0f el=%+.0f -> spk %d (g=%.2f)\n",
                    k, az * 180.f / kPi, el * 180.f / kPi, amax, g[amax]);
    }

    // The 8 corners must spread across many distinct speakers — this is the
    // property a stale/collapsed gain table would violate.
    std::printf("[spread] distinct argmax speakers = %zu/8\n", argmaxSpeakers.size());
    CHECK((int) argmaxSpeakers.size() >= 6);
    // +y corners reach the upper ring, -y corners the lower ring (elevation works).
    CHECK(upperHits >= 3);
    CHECK(lowerHits >= 3);

    if (failures == 0) std::printf("test_convergence_room_spatial: ALL PASS\n");
    return failures;
}

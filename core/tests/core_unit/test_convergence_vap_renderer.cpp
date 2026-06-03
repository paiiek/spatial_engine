// Phase 1 (Dreamscape Convergence): VAPRenderer integration test.
// Drives the live RenderingAlgorithm wrapper (prepareToPlay + processBlock)
// with an octahedron layout in the mmhoa frame, an object on the RIGHT, and a
// constant mono source. Asserts the renderer routes energy to the right
// speaker (L/R invariant through the full ported path) and produces output.

#include "render/VAPRenderer.h"
#include "geometry/SpeakerLayout.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace spe;

static int failures = 0;
#define CHECK(cond, msg)                              \
    do {                                              \
        if (!(cond)) {                                \
            std::fprintf(stderr, "FAIL: %s\n", msg);  \
            ++failures;                               \
        }                                             \
    } while (0)

static geometry::SpeakerLayout octahedron() {
    geometry::SpeakerLayout L;
    // mmhoa frame: x=right, y=up, z=front. Radius 2 m.
    L.speakers = {
        {1,  2.f, 0.f, 0.f},   // 0 RIGHT
        {2, -2.f, 0.f, 0.f},   // 1 LEFT
        {3,  0.f, 0.f, 2.f},   // 2 FRONT
        {4,  0.f, 0.f,-2.f},   // 3 BACK
        {5,  0.f, 2.f, 0.f},   // 4 UP
        {6,  0.f,-2.f, 0.f},   // 5 DOWN
    };
    return L;
}

int main() {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int RIGHT = 0, LEFT = 1;
    const int S = 6;
    const int NS = 64;  // samples/block

    render::VAPRenderer vap;
    vap.prepareToPlay(octahedron(), 48000.0);
    CHECK(vap.numSpeakers() == S, "VAPRenderer numSpeakers == 6");

    // One active object on the RIGHT (mmhoa az=+90deg), near the wall.
    std::array<render::ObjectState, MAX_OBJECTS> objs{};
    objs[0] = {/*az*/ +kPi / 2.f, /*el*/ 0.f, /*dist*/ 1.9f, /*active*/ true, /*width*/ 0.f};

    std::vector<float> mono(static_cast<size_t>(NS), 1.0f);  // constant source
    std::array<const float*, MAX_OBJECTS> dry{};
    dry[0] = mono.data();

    std::vector<float> out(static_cast<size_t>(NS * S), 0.f);

    // Run several blocks so GainRamp converges, then measure last-block energy.
    double energy[6] = {};
    for (int blk = 0; blk < 6; ++blk) {
        vap.processBlock(
            std::span<const render::ObjectState>(objs.data(), MAX_OBJECTS),
            std::span<const float* const>(dry.data(), MAX_OBJECTS),
            out.data(), NS);
        if (blk == 5) {
            for (int n = 0; n < NS; ++n)
                for (int s = 0; s < S; ++s) {
                    const float v = out[static_cast<size_t>(n * S + s)];
                    energy[s] += static_cast<double>(v) * v;
                }
        }
    }

    double total = 0.0;
    for (int s = 0; s < S; ++s) total += energy[s];
    CHECK(total > 1e-6, "VAPRenderer produces non-zero output");

    // L/R invariant through the full live path: right speaker dominates left.
    CHECK(energy[RIGHT] > energy[LEFT],
          "VAPRenderer: RIGHT object -> right speaker energy > left (L/R lock)");
    double maxE = 0.0; int argmax = -1;
    for (int s = 0; s < S; ++s) if (energy[s] > maxE) { maxE = energy[s]; argmax = s; }
    CHECK(argmax == RIGHT, "VAPRenderer: right speaker is the dominant output");

    if (failures == 0) {
        std::printf("convergence_vap_renderer OK\n");
        return 0;
    }
    std::fprintf(stderr, "convergence_vap_renderer FAILED: %d assertion(s)\n", failures);
    return 1;
}

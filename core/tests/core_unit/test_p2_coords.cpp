// P2: Coordinate convention unit test.
// Every (frame, sign) pair gets >= 3 hand-computed cases per CoordsTests.h.

#include "coords/Coords.h"
#include "coords/CoordsTests.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static constexpr float kTol = 1e-5f;

static bool near(float a, float b) { return std::fabs(a - b) < kTol; }

static int failures = 0;

#define CHECK(cond, msg)                                           \
    do {                                                           \
        if (!(cond)) {                                             \
            std::fprintf(stderr, "FAIL: %s\n", msg);              \
            ++failures;                                            \
        }                                                          \
    } while (0)

int main() {
    using namespace spe::coords;
    using namespace spe::coords::tests;

    // --- pipeline_to_ambix ---
    for (const auto& c : kPipeToAmbix) {
        auto [a, b] = pipeline_to_ambix(c.in_a, c.in_b);
        CHECK(near(a, c.exp_a), "pipeline_to_ambix az");
        CHECK(near(b, c.exp_b), "pipeline_to_ambix el");
    }

    // --- ambix_to_pipeline ---
    for (const auto& c : kAmbixToPipe) {
        auto [a, b] = ambix_to_pipeline(c.in_a, c.in_b);
        CHECK(near(a, c.exp_a), "ambix_to_pipeline az");
        CHECK(near(b, c.exp_b), "ambix_to_pipeline el");
    }

    // --- cartesian_to_pipeline (hand-computed spec cases + table) ---
    {
        // Spec-mandated cases.
        auto [az, el, d] = cartesian_to_pipeline(1.0f, 0.0f, 1.0f);
        CHECK(near(az, kPi4),            "cart(1,0,1) az == pi/4");
        CHECK(near(el, 0.0f),            "cart(1,0,1) el == 0");
        CHECK(near(d,  1.41421356f),     "cart(1,0,1) dist == sqrt(2)");
    }
    {
        auto [az, el, d] = cartesian_to_pipeline(-1.0f, 0.0f, 1.0f);
        CHECK(near(az, -kPi4),           "cart(-1,0,1) az == -pi/4");
        CHECK(near(el, 0.0f),            "cart(-1,0,1) el == 0");
        CHECK(near(d,  1.41421356f),     "cart(-1,0,1) dist == sqrt(2)");
    }
    {
        auto [az, el, d] = cartesian_to_pipeline(0.0f, 1.0f, 0.0f);
        CHECK(near(az, 0.0f),            "cart(0,1,0) az == 0");
        CHECK(near(el, kPi2),            "cart(0,1,0) el == pi/2");
        CHECK(near(d,  1.0f),            "cart(0,1,0) dist == 1");
    }
    {
        auto [az, el, d] = cartesian_to_pipeline(0.0f, -1.0f, 0.0f);
        CHECK(near(az, 0.0f),            "cart(0,-1,0) az == 0");
        CHECK(near(el, -kPi2),           "cart(0,-1,0) el == -pi/2");
        CHECK(near(d,  1.0f),            "cart(0,-1,0) dist == 1");
    }
    // Extra cases from table.
    for (const auto& c : kCartToPipe) {
        auto [az, el, d] = cartesian_to_pipeline(c.in_x, c.in_y, c.in_z);
        CHECK(near(az, c.exp_az),   "cartesian_to_pipeline az");
        CHECK(near(el, c.exp_el),   "cartesian_to_pipeline el");
        CHECK(near(d,  c.exp_dist), "cartesian_to_pipeline dist");
    }

    // --- image_y_to_listener_el ---
    for (const auto& c : kImageYToEl) {
        float el = image_y_to_listener_el(c.in);
        CHECK(near(el, c.exp), "image_y_to_listener_el");
    }
    // Spec-mandated cases.
    CHECK(near(image_y_to_listener_el( 0.5f), std::asin(-0.5f)), "img_y +0.5 == -pi/6");
    CHECK(near(image_y_to_listener_el( 0.0f), 0.0f),             "img_y 0 == 0");
    CHECK(near(image_y_to_listener_el(-1.0f), kPi2),             "img_y -1 == +pi/2");

    // --- yaml_speaker_to_cartesian ---
    for (const auto& c : kYamlSpeakerToCart) {
        auto xyz = yaml_speaker_to_cartesian(c.az_deg, c.el_deg, c.dist_m);
        CHECK(near(xyz[0], c.exp_x), "yaml_speaker x");
        CHECK(near(xyz[1], c.exp_y), "yaml_speaker y");
        CHECK(near(xyz[2], c.exp_z), "yaml_speaker z");
    }
    // Spec-mandated cases.
    {
        auto xyz = yaml_speaker_to_cartesian(90.0f, 0.0f, 1.0f);
        CHECK(near(xyz[0],  1.0f), "yaml(90,0,1) x==1");
        CHECK(near(xyz[1],  0.0f), "yaml(90,0,1) y==0");
        CHECK(near(xyz[2],  0.0f), "yaml(90,0,1) z==0");
    }
    {
        auto xyz = yaml_speaker_to_cartesian(0.0f, 0.0f, 1.0f);
        CHECK(near(xyz[0],  0.0f), "yaml(0,0,1) x==0");
        CHECK(near(xyz[1],  0.0f), "yaml(0,0,1) y==0");
        CHECK(near(xyz[2],  1.0f), "yaml(0,0,1) z==1");
    }
    {
        auto xyz = yaml_speaker_to_cartesian(180.0f, 0.0f, 1.0f);
        CHECK(near(xyz[0],  0.0f), "yaml(180,0,1) x==0");
        CHECK(near(xyz[1],  0.0f), "yaml(180,0,1) y==0");
        CHECK(near(xyz[2], -1.0f), "yaml(180,0,1) z==-1");
    }
    {
        auto xyz = yaml_speaker_to_cartesian(0.0f, 90.0f, 1.0f);
        CHECK(near(xyz[0],  0.0f), "yaml(0,90,1) x==0");
        CHECK(near(xyz[1],  1.0f), "yaml(0,90,1) y==1");
        CHECK(near(xyz[2],  0.0f), "yaml(0,90,1) z==0");
    }

    // --- stereo_pan_from_pipeline_az ---
    CHECK(stereo_pan_from_pipeline_az( kPi2) > 0.0f, "pan(+pi/2) > 0 (RIGHT louder)");
    CHECK(stereo_pan_from_pipeline_az(-kPi2) < 0.0f, "pan(-pi/2) < 0 (LEFT louder)");
    // Anti-regression: locks 2026-03-01 baseline_pan inversion bug.
    CHECK(stereo_pan_from_pipeline_az( kPi2) > stereo_pan_from_pipeline_az(0.0f),
          "pan ANTI-TEST: +pi/2 > 0");
    CHECK(stereo_pan_from_pipeline_az(0.0f)  > stereo_pan_from_pipeline_az(-kPi2),
          "pan ANTI-TEST: 0 > -pi/2");
    // Verify sin(az), not sin(-az).
    CHECK(near(stereo_pan_from_pipeline_az(kPi2), 1.0f),  "pan(pi/2) == sin(pi/2) == 1");
    CHECK(near(stereo_pan_from_pipeline_az(0.0f), 0.0f),  "pan(0) == 0");

    if (failures == 0) {
        std::printf("p2_coords OK\n");
        return 0;
    }
    std::fprintf(stderr, "p2_coords FAILED: %d assertion(s)\n", failures);
    return 1;
}

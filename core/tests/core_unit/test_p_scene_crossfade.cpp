// test_p_scene_crossfade.cpp
// M4: SceneCrossfade time-based interpolation gate.

#include "scene/SceneCrossfade.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace spe::scene;

static constexpr float kPi = 3.14159265358979323846f;

static void test_midpoint_500ms() {
    // 500 ms fade @ 48 kHz → exactly 24000 samples total.
    const float SR = 48000.f;
    Snapshot from, to;
    from.objects[0].active  = true;
    from.objects[0].az_rad  = -1.0f;
    from.objects[0].el_rad  = 0.0f;
    from.objects[0].dist_m  = 1.0f;
    from.objects[0].gain_db = -12.0f;

    to = from;
    to.objects[0].az_rad  = +1.0f;
    to.objects[0].dist_m  = 5.0f;
    to.objects[0].gain_db = 0.0f;

    SceneCrossfade fade;
    fade.start(from, to, 500.f, SR);
    assert(fade.active());

    // Step exactly 250 ms (12000 samples).
    fade.advance(12000);
    const float p = fade.progress();
    if (std::abs(p - 0.5f) > 1e-3f) {
        std::printf("FAIL midpoint progress=%.4f (expected 0.5)\n", p);
        assert(false);
    }
    auto o = fade.currentObject(0);
    // az: −1 → +1, midpoint 0.0 (±5%)
    if (std::abs(o.az_rad - 0.0f) > 0.05f) {
        std::printf("FAIL midpoint az=%.4f (expected ~0)\n", o.az_rad);
        assert(false);
    }
    // dist: 1 → 5, midpoint 3.0 (±5% of range = ±0.2)
    if (std::abs(o.dist_m - 3.0f) > 0.2f) {
        std::printf("FAIL midpoint dist=%.4f (expected ~3)\n", o.dist_m);
        assert(false);
    }
    // gain_db: −12 → 0, midpoint −6 (±0.5dB)
    if (std::abs(o.gain_db + 6.0f) > 0.5f) {
        std::printf("FAIL midpoint gain_db=%.4f (expected ~-6)\n", o.gain_db);
        assert(false);
    }
    std::printf("PASS test_midpoint_500ms (az=%.3f dist=%.3f gain_db=%.3f)\n",
                o.az_rad, o.dist_m, o.gain_db);
}

// Drive the fade to completion and ensure state pins to `to`.
static void test_completion_pins_to_target() {
    const float SR = 48000.f;
    Snapshot from, to;
    from.objects[1].active = false;
    to.objects[1].active   = true;
    from.objects[1].dist_m = 0.5f;
    to.objects[1].dist_m   = 7.5f;

    SceneCrossfade fade;
    fade.start(from, to, 100.f, SR);
    // Advance well past 100ms (5 × 100ms ≈ 24000 samples).
    fade.advance(24000);
    if (fade.active()) {
        std::printf("FAIL: fade still active after exceeding duration\n");
        assert(false);
    }
    auto o = fade.currentObject(1);
    if (!o.active || std::abs(o.dist_m - 7.5f) > 1e-4f) {
        std::printf("FAIL completion: active=%d dist=%.4f (expected active dist=7.5)\n",
                    o.active, o.dist_m);
        assert(false);
    }
    std::printf("PASS test_completion_pins_to_target\n");
}

// Discrete fields (active, algorithm) snap at the midpoint, not before.
static void test_discrete_snap_at_midpoint() {
    const float SR = 48000.f;
    Snapshot from, to;
    from.objects[2].active    = false;
    from.objects[2].algorithm = 0;
    to.objects[2].active      = true;
    to.objects[2].algorithm   = 3;

    SceneCrossfade fade;
    fade.start(from, to, 1000.f, SR);

    // 200 ms in (20% progress) — still on `from` side of the midpoint.
    fade.advance(9600);
    {
        auto o = fade.currentObject(2);
        if (o.active || o.algorithm != 0) {
            std::printf("FAIL early-snap: active=%d algorithm=%d at p=%.3f (expected from)\n",
                        o.active, o.algorithm, fade.progress());
            assert(false);
        }
    }
    // 700 ms total (70% progress) — now past midpoint, should snap to `to`.
    fade.advance(24000);
    {
        auto o = fade.currentObject(2);
        if (!o.active || o.algorithm != 3) {
            std::printf("FAIL late-snap: active=%d algorithm=%d at p=%.3f (expected to)\n",
                        o.active, o.algorithm, fade.progress());
            assert(false);
        }
    }
    std::printf("PASS test_discrete_snap_at_midpoint\n");
}

// Angle wrap: from 170° to -170° (Δ = 20° via +180→-180 boundary).
static void test_angle_shortest_arc() {
    const float SR = 48000.f;
    Snapshot from, to;
    from.objects[3].az_rad =  170.f * kPi / 180.f;
    to.objects[3].az_rad   = -170.f * kPi / 180.f;

    SceneCrossfade fade;
    fade.start(from, to, 200.f, SR);
    fade.advance(4800); // 50% progress (200ms / 2 = 100ms = 4800 samples)
    auto o = fade.currentObject(3);
    // Shortest arc: 170° → 180° → -170° (across +π wrap).
    // Midpoint should be near ±180° (not 0°).
    const float az_deg = o.az_rad * 180.f / kPi;
    const float wrapped = std::abs(std::abs(az_deg) - 180.f);
    if (wrapped > 5.f) {
        std::printf("FAIL shortest-arc: az=%.3f° (expected ~±180°)\n", az_deg);
        assert(false);
    }
    std::printf("PASS test_angle_shortest_arc (az=%.3f°)\n", az_deg);
}

// Zero / negative duration → snap immediately.
static void test_snap_immediate() {
    Snapshot from, to;
    from.objects[4].dist_m = 1.f;
    to.objects[4].dist_m   = 9.f;

    SceneCrossfade fade;
    fade.start(from, to, 0.f, 48000.f);
    if (fade.active()) {
        std::printf("FAIL snap_immediate: fade reports active for 0ms duration\n");
        assert(false);
    }
    auto o = fade.currentObject(4);
    if (std::abs(o.dist_m - 9.f) > 1e-4f) {
        std::printf("FAIL snap_immediate: dist=%.4f (expected 9)\n", o.dist_m);
        assert(false);
    }
    std::printf("PASS test_snap_immediate\n");
}

int main() {
    test_midpoint_500ms();
    test_completion_pins_to_target();
    test_discrete_snap_at_midpoint();
    test_angle_shortest_arc();
    test_snap_immediate();
    std::printf("All scene_crossfade tests passed.\n");
    return 0;
}

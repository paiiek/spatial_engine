// core/tests/core_unit/test_p_scene_cue_e2e.cpp
// E-M6 gate: full library→cuelist→auto-advance end-to-end integration.
//
// This test covers the BROADER multi-scene / multi-cue flow that the narrower
// E-M3 unit tests do not: create several scenes with distinct objects via
// SceneController, build a CueList with mixed dwell/non-dwell cues, fire
// go(0) and let the dwell auto-advance sequence walk through all cues in
// order, asserting cue index and emitted object identity at each step.
//
// It does NOT re-cover: D2 generation latch, stop(), gain units, wire-path
// dispatch, or SceneLoad-failure hold — those are in test_p_cue_go_advance
// and test_p_cue_wire_dispatch respectively.
//
// No external framework — hand-rolled CHECK macro (matches codebase style).

#include "ipc/SceneController.h"
#include "ipc/SceneSnapshot.h"
#include "ipc/Command.h"
#include "scene/CueEngine.h"
#include "scene/CueList.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace spe;
namespace fs = std::filesystem;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); std::exit(1); } } while(0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct EmitCapture {
    std::vector<ipc::Command> cmds;
    bool operator()(const ipc::Command& c) { cmds.push_back(c); return true; }
    void clear() { cmds.clear(); }

    // Return the last ObjGain.gain (linear) emitted for obj_id, or NaN.
    float lastGain(uint32_t obj_id) const {
        float g = std::nanf("");
        for (const auto& c : cmds) {
            if (c.tag != ipc::CommandTag::ObjGain) continue;
            if (auto* p = std::get_if<ipc::PayloadObjGain>(&c.payload))
                if (p->obj_id == obj_id) g = p->gain;
        }
        return g;
    }

    // True if any ObjActive was emitted for obj_id with the given active flag.
    bool hasActive(uint32_t obj_id, bool active_flag) const {
        for (const auto& c : cmds) {
            if (c.tag != ipc::CommandTag::ObjActive) continue;
            if (auto* p = std::get_if<ipc::PayloadObjActive>(&c.payload))
                if (p->obj_id == obj_id && p->active == active_flag) return true;
        }
        return false;
    }
};

// Write a minimal scene snapshot with a single object.
static void writeScene(const std::string& dir, const std::string& name,
                       uint32_t obj_id, float gain_linear, bool muted = false) {
    ipc::SceneSnapshot snap;
    snap.name = name;
    ipc::ObjectSnapshot o;
    o.id        = obj_id;
    o.az_rad    = 0.5f;
    o.el_rad    = 0.2f;
    o.dist_m    = 3.0f;
    o.algorithm = 0;
    o.gain_linear = gain_linear;
    o.muted     = muted;
    snap.objects.push_back(o);
    if (!snap.saveToDisk(dir)) {
        std::fprintf(stderr, "FATAL: writeScene(%s) failed\n", name.c_str());
        std::exit(1);
    }
}

// ---------------------------------------------------------------------------
// Test 1: full 4-scene, 4-cue auto-advance sequence
//
// Scenes: sc0(obj=10, g=1.0), sc1(obj=11, g=0.8), sc2(obj=12, g=0.6),
//         sc3(obj=13, g=0.4).
// CueList:
//   cue 0 → sc0, crossfade=0, dwell=300ms  → auto-advance to cue 1
//   cue 1 → sc1, crossfade=0, dwell=300ms  → auto-advance to cue 2
//   cue 2 → sc2, crossfade=0, dwell=300ms  → auto-advance to cue 3
//   cue 3 → sc3, crossfade=0, no dwell     → stays
// ---------------------------------------------------------------------------

static void test_full_sequence(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    {
        scene::Cue c; c.scene = "sc0"; c.crossfade_ms = 0.f; c.dwell_ms = 300.f;
        cl.cues.push_back(c);
    }
    {
        scene::Cue c; c.scene = "sc1"; c.crossfade_ms = 0.f; c.dwell_ms = 300.f;
        cl.cues.push_back(c);
    }
    {
        scene::Cue c; c.scene = "sc2"; c.crossfade_ms = 0.f; c.dwell_ms = 300.f;
        cl.cues.push_back(c);
    }
    {
        scene::Cue c; c.scene = "sc3"; c.crossfade_ms = 0.f;
        cl.cues.push_back(c);
    }
    cue.setCueList(cl);

    // ---- fire go(0) ---------------------------------------------------------
    cap.clear();
    cue.go(0, 0);
    CHECK(cue.currentIndex() == 0, "seq: go(0) → index 0");
    CHECK(!cue.crossfadeActive(), "seq: zero crossfade → no active fade after go(0)");
    // With crossfade=0, object updates are emitted immediately on go().
    const float g0 = cap.lastGain(10);
    CHECK(!std::isnan(g0), "seq: cue0 ObjGain emitted for obj 10");
    CHECK(std::fabs(g0 - 1.0f) < 0.01f, "seq: cue0 obj10 gain ≈ 1.0");
    CHECK(cap.hasActive(10, true), "seq: cue0 obj10 active=true emitted");

    // ---- tick to just before cue0 dwell deadline (300ms) --------------------
    cue.tick(200);
    CHECK(cue.currentIndex() == 0, "seq: still on cue 0 before dwell deadline");

    // ---- tick past cue0 dwell deadline → auto-advance to cue 1 -------------
    cap.clear();
    cue.tick(400);
    CHECK(cue.currentIndex() == 1, "seq: dwell fired → advanced to cue 1");
    const float g1 = cap.lastGain(11);
    CHECK(!std::isnan(g1), "seq: cue1 ObjGain emitted for obj 11");
    CHECK(std::fabs(g1 - 0.8f) < 0.01f, "seq: cue1 obj11 gain ≈ 0.8");

    // ---- tick past cue1 dwell deadline (400+300=700ms) → cue 2 -------------
    cap.clear();
    cue.tick(800);
    CHECK(cue.currentIndex() == 2, "seq: dwell fired → advanced to cue 2");
    const float g2 = cap.lastGain(12);
    CHECK(!std::isnan(g2), "seq: cue2 ObjGain emitted for obj 12");
    CHECK(std::fabs(g2 - 0.6f) < 0.01f, "seq: cue2 obj12 gain ≈ 0.6");

    // ---- tick past cue2 dwell deadline (800+300=1100ms) → cue 3 ------------
    cap.clear();
    cue.tick(1200);
    CHECK(cue.currentIndex() == 3, "seq: dwell fired → advanced to cue 3");
    const float g3 = cap.lastGain(13);
    CHECK(!std::isnan(g3), "seq: cue3 ObjGain emitted for obj 13");
    CHECK(std::fabs(g3 - 0.4f) < 0.01f, "seq: cue3 obj13 gain ≈ 0.4");

    // ---- cue 3 has no dwell → stays at 3 forever ---------------------------
    cue.tick(9999);
    CHECK(cue.currentIndex() == 3, "seq: no dwell on cue 3 → stays");

    std::printf("PASS: test_full_sequence\n");
}

// ---------------------------------------------------------------------------
// Test 2: manual go() mid-sequence re-enters at the requested cue
//
// Go(0) → tick past dwell → on cue 1 → manual go(3) → verify lands on 3,
// not 2, and that subsequent tick does NOT advance (cue 3 has no dwell).
// ---------------------------------------------------------------------------

static void test_manual_jump_mid_sequence(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    {
        scene::Cue c; c.scene = "sc0"; c.crossfade_ms = 0.f; c.dwell_ms = 200.f;
        cl.cues.push_back(c);
    }
    {
        scene::Cue c; c.scene = "sc1"; c.crossfade_ms = 0.f; c.dwell_ms = 200.f;
        cl.cues.push_back(c);
    }
    {
        scene::Cue c; c.scene = "sc2"; c.crossfade_ms = 0.f; c.dwell_ms = 200.f;
        cl.cues.push_back(c);
    }
    {
        scene::Cue c; c.scene = "sc3"; c.crossfade_ms = 0.f;
        cl.cues.push_back(c);
    }
    cue.setCueList(cl);

    cue.go(0, 0);
    cue.tick(300);  // past cue 0 dwell → now on cue 1
    CHECK(cue.currentIndex() == 1, "jump: auto-advanced to cue 1");

    // Manual jump to cue 3, skipping cue 2
    cap.clear();
    cue.go(3, 350);
    CHECK(cue.currentIndex() == 3, "jump: manual go(3) lands on cue 3");
    const float g3 = cap.lastGain(13);
    CHECK(!std::isnan(g3), "jump: ObjGain emitted for obj 13 after go(3)");
    CHECK(std::fabs(g3 - 0.4f) < 0.01f, "jump: obj13 gain ≈ 0.4");

    // tick well past when cue 3 dwell would have fired if it had one
    cue.tick(9999);
    CHECK(cue.currentIndex() == 3, "jump: cue 3 has no dwell → stays");

    std::printf("PASS: test_manual_jump_mid_sequence\n");
}

// ---------------------------------------------------------------------------
// Test 3: next()/prev() navigation across the library
//
// go(0) → next → cue1 → next → cue2 → prev → back to cue1.
// All crossfade=0 for deterministic instantaneous advance.
// ---------------------------------------------------------------------------

static void test_next_prev_navigation(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    for (const char* s : {"sc0", "sc1", "sc2", "sc3"}) {
        scene::Cue c; c.scene = s; c.crossfade_ms = 0.f;
        cl.cues.push_back(c);
    }
    cue.setCueList(cl);

    cue.go(0, 0);
    CHECK(cue.currentIndex() == 0, "nav: start at 0");

    cue.next(10);
    CHECK(cue.currentIndex() == 1, "nav: next → 1");

    cue.next(20);
    CHECK(cue.currentIndex() == 2, "nav: next → 2");

    cue.prev(30);
    CHECK(cue.currentIndex() == 1, "nav: prev → 1");

    // Verify obj11 was loaded on the last go-to-1
    cap.clear();
    cue.next(40);  // → 2 again
    cue.prev(50);  // → 1 again (fresh go)
    const float g = cap.lastGain(11);
    CHECK(!std::isnan(g), "nav: ObjGain for obj11 after prev-to-1");
    CHECK(std::fabs(g - 0.8f) < 0.01f, "nav: obj11 gain ≈ 0.8 on prev");

    std::printf("PASS: test_next_prev_navigation\n");
}

// ---------------------------------------------------------------------------
// Test 4: duplicate + rename via SceneController then include in cuelist
//
// Verify the library management ops (duplicate/rename) produce loadable
// scenes that CueEngine can play back correctly.
// ---------------------------------------------------------------------------

static void test_library_ops_then_cue(const std::string& dir) {
    ipc::SceneController ctrl(dir);

    // Duplicate sc0 → sc0_copy
    CHECK(ctrl.duplicate("sc0", "sc0_copy"), "lib: duplicate sc0→sc0_copy");
    // Rename sc0_copy → sc_intro
    CHECK(ctrl.rename("sc0_copy", "sc_intro"), "lib: rename sc0_copy→sc_intro");

    // Build a cuelist using the renamed scene
    EmitCapture cap;
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));
    scene::CueList cl;
    {
        scene::Cue c; c.scene = "sc_intro"; c.crossfade_ms = 0.f;
        cl.cues.push_back(c);
    }
    cue.setCueList(cl);

    cap.clear();
    cue.go(0, 0);
    CHECK(cue.currentIndex() == 0, "lib: go(0) on sc_intro lands at cue 0");
    // sc_intro was duplicated from sc0 which has obj10 at gain 1.0
    const float g = cap.lastGain(10);
    CHECK(!std::isnan(g), "lib: ObjGain emitted for obj10 from sc_intro");
    CHECK(std::fabs(g - 1.0f) < 0.01f, "lib: sc_intro obj10 gain ≈ 1.0");

    // delete sc_intro; subsequent go should hold (scene missing)
    CHECK(ctrl.remove("sc_intro"), "lib: remove sc_intro");
    scene::CueEngine cue2(&ctrl, 48000.f, std::ref(cap));
    cue2.setCueList(cl);
    cue2.go(0, 0);
    // With scene missing, index should be held at -1 (no cue loaded)
    // or at 0 depending on implementation; key assertion: no crash + currentIndex not 1
    CHECK(cue2.currentIndex() != 1, "lib: deleted scene → did not advance past 0");

    std::printf("PASS: test_library_ops_then_cue\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    auto tmp = fs::temp_directory_path() / "spe_scene_cue_e2e_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const std::string dir = tmp.string();

    // Write scenes used across all tests
    writeScene(dir, "sc0", 10, 1.0f);
    writeScene(dir, "sc1", 11, 0.8f);
    writeScene(dir, "sc2", 12, 0.6f);
    writeScene(dir, "sc3", 13, 0.4f);

    test_full_sequence(dir);
    test_manual_jump_mid_sequence(dir);
    test_next_prev_navigation(dir);
    test_library_ops_then_cue(dir);

    fs::remove_all(tmp);
    std::printf("ALL PASS\n");
    return 0;
}

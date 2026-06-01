// core/tests/core_unit/test_p_cue_go_advance.cpp
// E-M3 gate: CueEngine go/advance + dwell auto-advance + D2 generation latch +
// units (gain dB intermediate AND emitted linear) + SceneLoad-failure hold.
// No external framework — hand-rolled CHECK macro (test_p_scene.cpp style).

#include "scene/CueEngine.h"
#include "scene/CueList.h"
#include "ipc/SceneController.h"
#include "ipc/SceneSnapshot.h"
#include "ipc/Command.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

using namespace spe;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); } } while(0)

namespace fs = std::filesystem;

// Capture every emitted Command so the test can assert on the wire payloads.
struct EmitCapture {
    std::vector<ipc::Command> cmds;
    bool operator()(const ipc::Command& c) {
        cmds.push_back(c);
        return true;
    }
    void clear() { cmds.clear(); }
    // Last ObjGain.gain (linear) for obj_id, or NaN if none emitted.
    float lastGain(uint32_t obj_id) const {
        float g = std::nanf("");
        for (const auto& c : cmds) {
            if (c.tag != ipc::CommandTag::ObjGain) continue;
            if (auto* p = std::get_if<ipc::PayloadObjGain>(&c.payload)) {
                if (p->obj_id == obj_id) g = p->gain;
            }
        }
        return g;
    }
};

static void writeScene(const std::string& dir, const std::string& name,
                       int obj_id, float gain_linear, bool muted) {
    ipc::SceneSnapshot snap;
    snap.name = name;
    ipc::ObjectSnapshot o;
    o.id = obj_id;
    o.az_rad = 0.3f;
    o.el_rad = 0.1f;
    o.dist_m = 2.0f;
    o.algorithm = 0;
    o.gain_linear = gain_linear;
    o.muted = muted;
    snap.objects.push_back(o);
    CHECK(snap.saveToDisk(dir), "writeScene: saveToDisk");
}

// ---- Test 1: go(0) loads target + arms crossfade ----------------------------

static void test_go_loads_and_arms(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    { scene::Cue c; c.scene = "s0"; c.crossfade_ms = 1000.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "s1"; c.crossfade_ms = 1000.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    cue.go(0, /*now_ms=*/0);
    CHECK(cue.currentIndex() == 0, "go0: index == 0");
    CHECK(cue.crossfadeActive(), "go0: crossfade armed");
    CHECK(ctrl.lastLoaded().has_value(), "go0: target scene loaded");
    CHECK(ctrl.lastLoaded()->name == "s0", "go0: loaded name s0");
    std::printf("PASS: test_go_loads_and_arms\n");
}

// ---- Test 2: dwell auto-advance fires in order ------------------------------

static void test_dwell_autoadvance(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    { scene::Cue c; c.scene = "s0"; c.crossfade_ms = 100.f; c.dwell_ms = 500.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "s1"; c.crossfade_ms = 100.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    cue.go(0, 0);
    CHECK(cue.currentIndex() == 0, "dwell: start at 0");
    // crossfade ends at 100ms; dwell deadline = 100 + 500 = 600ms.
    cue.tick(120);   // crossfade completes
    CHECK(!cue.crossfadeActive(), "dwell: crossfade done by 120ms");
    cue.tick(400);   // before dwell deadline
    CHECK(cue.currentIndex() == 0, "dwell: not advanced before deadline");
    cue.tick(700);   // past dwell deadline → auto-advance to cue 1
    CHECK(cue.currentIndex() == 1, "dwell: auto-advanced to cue 1");
    std::printf("PASS: test_dwell_autoadvance\n");
}

// ---- Test 3: D2 race — manual go(2) before dwell fires ----------------------

static void test_d2_generation_latch(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    { scene::Cue c; c.scene = "s0"; c.crossfade_ms = 100.f; c.dwell_ms = 500.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "s1"; c.crossfade_ms = 100.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "s2"; c.crossfade_ms = 100.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    cue.go(0, 0);
    cue.tick(120);                 // cue 0 crossfade done; dwell armed @ gen for cue0
    const uint64_t gen_after_go0 = cue.generation();
    cue.go(2, 200);                // manual jump BEFORE dwell deadline (600ms)
    CHECK(cue.currentIndex() == 2, "d2: jumped to cue 2");
    CHECK(cue.generation() != gen_after_go0, "d2: generation bumped by manual go");
    cue.tick(250);                 // cue 2 crossfade completes
    cue.tick(700);                 // PAST the OLD dwell deadline (600ms)
    // Stale dwell tag mismatches current generation → must NOT fire.
    CHECK(cue.currentIndex() == 2, "d2: stale dwell did NOT fire, still on cue 2");
    std::printf("PASS: test_d2_generation_latch\n");
}

// ---- Test 4: stop() cancels pending dwell ----------------------------------

static void test_stop_cancels_dwell(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    { scene::Cue c; c.scene = "s0"; c.crossfade_ms = 100.f; c.dwell_ms = 500.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "s1"; c.crossfade_ms = 100.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    cue.go(0, 0);
    cue.tick(120);
    cue.stop(150);                 // cancels pending dwell
    cue.tick(700);                 // past old deadline
    CHECK(cue.currentIndex() == 0, "stop: dwell cancelled, stays on cue 0");
    std::printf("PASS: test_stop_cancels_dwell\n");
}

// ---- Test 5: units — intermediate dB AND emitted linear gain ----------------

static void test_gain_units(const std::string& dir) {
    // Scene object with gain_linear = 0.5 → gain_db ≈ -6.02 → emitted linear ≈ 0.5
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    // 0 crossfade → instant snap → target frame emitted immediately at go().
    { scene::Cue c; c.scene = "s_half"; c.crossfade_ms = 0.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    cap.clear();
    cue.go(0, 0);

    // Intermediate ObjectFrame.gain_db ≈ -6.02 in the engine's baseline.
    const float gain_db = cue.currentFrame()[3].gain_db;
    CHECK(std::fabs(gain_db - (-6.0206f)) < 0.05f, "units: intermediate gain_db ≈ -6.02");

    // EMITTED PayloadObjGain.gain must be LINEAR ≈ 0.5 (fix #7 reverse conv).
    const float emitted = cap.lastGain(3);
    CHECK(!std::isnan(emitted), "units: ObjGain emitted for obj 3");
    CHECK(std::fabs(emitted - 0.5f) < 0.01f, "units: EMITTED gain is LINEAR ≈ 0.5");

    // width/reverb_send default to 0.
    CHECK(cue.currentFrame()[3].width_rad == 0.f, "units: width default 0");
    CHECK(cue.currentFrame()[3].reverb_send == 0.f, "units: reverb_send default 0");
    std::printf("PASS: test_gain_units\n");
}

// ---- Test 6: active = !muted -----------------------------------------------

static void test_active_from_muted(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    { scene::Cue c; c.scene = "s_muted"; c.crossfade_ms = 0.f; cl.cues.push_back(c); }
    cue.setCueList(cl);
    cue.go(0, 0);
    // s_muted has obj 5 muted=true → active=false.
    CHECK(cue.currentFrame()[5].active == false, "active: listed-but-muted → active=false");
    // a slot with no matching object stays default false.
    CHECK(cue.currentFrame()[10].active == false, "active: unlisted slot → false");
    std::printf("PASS: test_active_from_muted\n");
}

// ---- Test 7: SceneLoad-failure holds current scene -------------------------

static void test_load_failure_holds(const std::string& dir) {
    EmitCapture cap;
    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f, std::ref(cap));

    scene::CueList cl;
    { scene::Cue c; c.scene = "s0"; c.crossfade_ms = 100.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "does_not_exist"; c.crossfade_ms = 100.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    cue.go(0, 0);
    CHECK(cue.currentIndex() == 0, "fail: on cue 0");
    cue.go(1, 100);  // target scene file missing → hold
    CHECK(cue.currentIndex() == 0, "fail: index UNCHANGED after failed load (held cue 0)");
    std::printf("PASS: test_load_failure_holds\n");
}

// ---- main -------------------------------------------------------------------

int main() {
    auto tmp = fs::temp_directory_path() / "spe_cue_go_advance_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const std::string dir = tmp.string();

    writeScene(dir, "s0",      0, 1.0f, false);
    writeScene(dir, "s1",      1, 1.0f, false);
    writeScene(dir, "s2",      2, 1.0f, false);
    writeScene(dir, "s_half",  3, 0.5f, false);
    writeScene(dir, "s_muted", 5, 1.0f, true);

    test_go_loads_and_arms(dir);
    test_dwell_autoadvance(dir);
    test_d2_generation_latch(dir);
    test_stop_cancels_dwell(dir);
    test_gain_units(dir);
    test_active_from_muted(dir);
    test_load_failure_holds(dir);

    fs::remove_all(tmp);
    std::printf("ALL PASS\n");
    return 0;
}

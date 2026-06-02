// core/tests/core_unit/test_p_scene_obj_state_e2e.cpp
// v0.9 Lane F4 (F4b) — real save-side object-state capture round-trip.
//
// Drives objects into the engine over the production command path
// (dispatchCommand → cmd_fifo_ → audioBlock drain → obj_cache_ → post-drain
// publish), then:
//   AC4  — snapshotObjects → /scene/save → reload from disk → assert non-zero
//          width/reverb (+ az/el/dist/gain) → fire cue → assert engine receives
//          non-zero (re-driven). Also the non-empty objects regression guard.
//   AC4b — empty-objects regression baseline: a fresh engine with NO driven
//          objects yields an EMPTY objects vector (and snapshotObjects with
//          nothing published returns empty).
//   AC6  — touched heuristic skips pristine slots; ids >= MAX_OBJECTS dropped.
//   AC7  — mute persistence: a muted-but-positioned object round-trips with
//          position/width/reverb preserved and muted==true.
//
// No external framework — hand-rolled CHECK macro (matches codebase style).

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "ipc/Command.h"
#include "ipc/SceneController.h"
#include "ipc/SceneSnapshot.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

using namespace spe;
namespace fs = std::filesystem;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); std::exit(1); } } while(0)

// Drive one minimal audio block so the command FIFO drains into obj_cache_ and
// (if dirty) publishes the snapshot.
static void driveBlock(core::SpatialEngine& engine, int n_spk = 8) {
    constexpr int kFrames = 64;
    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s)
        ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    audio_io::AudioBlock block;
    block.output_channels      = ptrs.data();
    block.output_channel_count = n_spk;
    block.num_frames           = kFrames;
    block.sample_rate          = 48000.0;
    engine.audioBlock(block);
}

static void dispatchMove(core::SpatialEngine& e, uint32_t id, float az, float el, float dist) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjMove;
    ipc::PayloadObjMove p; p.obj_id = id; p.az_rad = az; p.el_rad = el; p.dist_m = dist;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchGain(core::SpatialEngine& e, uint32_t id, float gain_lin) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjGain;
    ipc::PayloadObjGain p; p.obj_id = id; p.gain = gain_lin;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchWidth(core::SpatialEngine& e, uint32_t id, float w) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjWidth;
    ipc::PayloadObjWidth p; p.obj_id = id; p.width_rad = w;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchReverb(core::SpatialEngine& e, uint32_t id, float r) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjDsp;
    ipc::PayloadObjDsp p; p.obj_id = id; p.param = ipc::PayloadObjDsp::Param::ReverbSend; p.value = r;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchMute(core::SpatialEngine& e, uint32_t id, bool muted) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjMute;
    ipc::PayloadObjMute p; p.obj_id = id; p.muted = muted;
    c.payload = p; e.dispatchCommand(c);
}

static bool fclose_(float a, float b, float tol = 1e-3f) {
    float d = a - b; return (d < 0 ? -d : d) < tol;
}

// ---------------------------------------------------------------------------
// AC4b: empty-objects regression baseline.
// ---------------------------------------------------------------------------
static void test_empty_objects_baseline() {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, 64);

    // Nothing published yet → empty.
    std::vector<ipc::ObjectSnapshot> out;
    engine.snapshotObjects(out);
    CHECK(out.empty(), "AC4b: no publish → empty objects");

    // Drive a block with NO commands → nothing dirty → still nothing published.
    driveBlock(engine);
    engine.snapshotObjects(out);
    CHECK(out.empty(), "AC4b: idle block → still empty");

    std::printf("PASS: test_empty_objects_baseline\n");
}

// ---------------------------------------------------------------------------
// AC4: full OSC→save→reload→cue round-trip preserving width/reverb.
// ---------------------------------------------------------------------------
static void test_objstate_roundtrip(const std::string& dir) {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, 64);

    spe::ipc::SceneController ctrl(dir);
    ctrl.setObjectStateProvider(
        [&engine](std::vector<ipc::ObjectSnapshot>& o) { engine.snapshotObjects(o); });

    // Drive object 7: move + gain + width + reverb.
    const uint32_t kId = 7;
    const float kAz = 0.6f, kEl = -0.2f, kDist = 2.5f, kGain = 0.7f;
    const float kWidth = 1.1f, kReverb = 0.55f;
    dispatchMove(engine, kId, kAz, kEl, kDist);
    dispatchGain(engine, kId, kGain);
    dispatchWidth(engine, kId, kWidth);
    dispatchReverb(engine, kId, kReverb);

    // Spin until a block has drained + published (AC4 determinism device).
    for (int n = 0; n < 8; ++n) driveBlock(engine);

    // Save via the provider seam.
    ipc::Command save; save.tag = ipc::CommandTag::SceneSave;
    ipc::PayloadSceneSave sp{}; std::snprintf(sp.name, sizeof(sp.name), "%s", "objstate");
    save.payload = sp;
    CHECK(ctrl.handleCommand(save), "AC4: SceneSave handled");

    // Reload from disk.
    auto loaded = ipc::SceneSnapshot::loadFromDisk(dir, "objstate");
    CHECK(loaded.has_value(), "AC4: scene reloaded");
    CHECK(!loaded->objects.empty(), "AC4 regression: objects NOT empty (guards fact #11)");

    // Find object 7.
    const ipc::ObjectSnapshot* found = nullptr;
    for (const auto& o : loaded->objects)
        if (o.id == static_cast<int>(kId)) { found = &o; break; }
    CHECK(found != nullptr, "AC4: object 7 present in saved scene");
    CHECK(fclose_(found->width_rad,   kWidth),  "AC4: saved width_rad non-zero/matches");
    CHECK(fclose_(found->reverb_send, kReverb), "AC4: saved reverb_send non-zero/matches");
    CHECK(found->width_rad   > 0.f, "AC4: width_rad strictly non-zero");
    CHECK(found->reverb_send > 0.f, "AC4: reverb_send strictly non-zero");
    CHECK(fclose_(found->az_rad, kAz),   "AC4: saved az matches");
    CHECK(fclose_(found->el_rad, kEl),   "AC4: saved el matches");
    CHECK(fclose_(found->dist_m, kDist), "AC4: saved dist matches");
    CHECK(fclose_(found->gain_linear, kGain), "AC4: saved gain matches");
    CHECK(found->muted == false, "AC4: object active → muted==false");

    // Fire cue: load this scene into a fresh engine via the emit path and assert
    // the engine receives non-zero width/reverb. We re-drive a clean engine with
    // the snapshot→frame→emit equivalent: dispatch the same commands the cue
    // would emit and confirm obj_cache_ ends up populated (non-zero) on it.
    // (CueEngine emit assertion proper is AC3 in test_p_scene_cue_e2e.)
    core::SpatialEngine engine2(0);
    engine2.prepareToPlay(48000.0, 64);
    dispatchWidth(engine2, kId, found->width_rad);
    dispatchReverb(engine2, kId, found->reverb_send);
    dispatchMove(engine2, kId, found->az_rad, found->el_rad, found->dist_m);
    for (int n = 0; n < 4; ++n) driveBlock(engine2);
    std::vector<ipc::ObjectSnapshot> out2;
    engine2.snapshotObjects(out2);
    const ipc::ObjectSnapshot* re = nullptr;
    for (const auto& o : out2) if (o.id == static_cast<int>(kId)) { re = &o; break; }
    CHECK(re != nullptr, "AC4: cue-re-driven object present on engine2");
    CHECK(re->width_rad   > 0.f, "AC4: engine2 received non-zero width");
    CHECK(re->reverb_send > 0.f, "AC4: engine2 received non-zero reverb");

    std::printf("PASS: test_objstate_roundtrip\n");
}

// ---------------------------------------------------------------------------
// AC6: touched heuristic + MAX_OBJECTS bound.
// ---------------------------------------------------------------------------
static void test_touched_and_bounds() {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, 64);

    // Drive exactly one object (id 3). All other slots stay pristine.
    dispatchMove(engine, 3, 0.4f, 0.f, 1.7f);
    for (int n = 0; n < 4; ++n) driveBlock(engine);

    std::vector<ipc::ObjectSnapshot> out;
    engine.snapshotObjects(out);
    CHECK(out.size() == 1, "AC6: only the one touched object emitted");
    CHECK(out[0].id == 3, "AC6: touched id is 3");

    // Out-of-range id must be dropped at the FIFO drain (no crash, no emit).
    dispatchMove(engine, static_cast<uint32_t>(MAX_OBJECTS) + 5, 0.1f, 0.f, 1.f);
    for (int n = 0; n < 2; ++n) driveBlock(engine);
    engine.snapshotObjects(out);
    CHECK(out.size() == 1, "AC6: OOB id did not add an object");

    std::printf("PASS: test_touched_and_bounds\n");
}

// ---------------------------------------------------------------------------
// AC7: mute persistence — muted-but-positioned object round-trips.
// ---------------------------------------------------------------------------
static void test_mute_persistence(const std::string& dir) {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, 64);
    spe::ipc::SceneController ctrl(dir);
    ctrl.setObjectStateProvider(
        [&engine](std::vector<ipc::ObjectSnapshot>& o) { engine.snapshotObjects(o); });

    const uint32_t kId = 9;
    // Position + width + reverb, THEN mute.
    dispatchMove(engine, kId, -0.8f, 0.15f, 3.0f);
    dispatchWidth(engine, kId, 0.9f);
    dispatchReverb(engine, kId, 0.3f);
    dispatchMute(engine, kId, true);    // active=false but fields are non-default
    for (int n = 0; n < 4; ++n) driveBlock(engine);

    ipc::Command save; save.tag = ipc::CommandTag::SceneSave;
    ipc::PayloadSceneSave sp{}; std::snprintf(sp.name, sizeof(sp.name), "%s", "muted");
    save.payload = sp;
    CHECK(ctrl.handleCommand(save), "AC7: SceneSave handled");

    auto loaded = ipc::SceneSnapshot::loadFromDisk(dir, "muted");
    CHECK(loaded.has_value(), "AC7: scene reloaded");
    const ipc::ObjectSnapshot* found = nullptr;
    for (const auto& o : loaded->objects)
        if (o.id == static_cast<int>(kId)) { found = &o; break; }
    CHECK(found != nullptr, "AC7: muted object still emitted (touched via non-default fields)");
    CHECK(found->muted == true, "AC7: muted==true preserved");
    CHECK(fclose_(found->width_rad, 0.9f),  "AC7: width preserved through mute");
    CHECK(fclose_(found->reverb_send, 0.3f),"AC7: reverb preserved through mute");
    CHECK(fclose_(found->dist_m, 3.0f),     "AC7: position preserved through mute");

    std::printf("PASS: test_mute_persistence\n");
}

int main() {
    auto tmp = fs::temp_directory_path() / "spe_scene_objstate_e2e";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const std::string dir = tmp.string();

    test_empty_objects_baseline();
    test_objstate_roundtrip(dir);
    test_touched_and_bounds();
    test_mute_persistence(dir);

    fs::remove_all(tmp);
    std::printf("ALL PASS\n");
    return 0;
}

// test_p_object_cap_render.cpp — v0.9 Lane C (C-M2 / D2 audio-plane test)
//
// Proves the object at index MAX_OBJECTS-1 actually RENDERS at the configured
// cap. Using MAX_OBJECTS-1 makes the test cap-relative: it exercises obj 63 at
// the 64-build and obj 127 at the 128-build — i.e. it proves objects 64..127
// produce sound once the cap is bumped, without a #if guard.
//
// Path under test (per plan §0.2): an ObjMove command drains via the FIFO
// directly into obj_cache_[id] (SpatialEngine.cpp:473-478), the engine
// generates the per-object sine tone for the now-active object, and the
// per-algorithm renderer writes non-silent samples to the speaker bus.
//
// Assertions:
//   1. obj_cache_[MAX_OBJECTS-1].active == true after the drain.
//   2. The rendered speaker output is NON-SILENT (RMS > 0).

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/LayoutLoader.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

static int failures = 0;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL: %s\n", msg);                      \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// Drive one audio block, summing |output| across all speakers/frames so the
// caller can verify non-silence. Returns the accumulated absolute energy.
static double drive_one_block_energy(spe::core::SpatialEngine& engine, int n_spk) {
    constexpr int kFrames = 64;
    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s)
        ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    spe::audio_io::AudioBlock block;
    block.output_channels      = ptrs.data();
    block.output_channel_count = n_spk;
    block.num_frames           = kFrames;
    block.sample_rate          = 48000.0;
    engine.audioBlock(block);

    double energy = 0.0;
    for (int s = 0; s < n_spk; ++s)
        for (int n = 0; n < kFrames; ++n)
            energy += std::fabs(static_cast<double>(bufs[static_cast<size_t>(s)][static_cast<size_t>(n)]));
    return energy;
}

int main(int argc, char** argv) {
    std::string fixtures_dir;
    if (argc >= 2) fixtures_dir = std::string(argv[1]) + "/";
    else           fixtures_dir = std::string(SPE_FIXTURES_DIR) + "/";

    auto result = spe::geometry::load_layout(fixtures_dir + "lab_irregular_reordered.yaml");
    CHECK(spe::geometry::is_ok(result), "layout fixture loads");
    if (!spe::geometry::is_ok(result)) return 1;
    const auto& layout = std::get<spe::geometry::SpeakerLayout>(result);

    spe::core::SpatialEngine engine(0);
    engine.setLayout(layout);
    engine.prepareToPlay(48000.0, 64);

    // The boundary object: 63 at the 64-build, 127 at the 128-build.
    const uint32_t obj = static_cast<uint32_t>(spe::MAX_OBJECTS - 1);

    // Sanity: the cap is exactly what cmake configured.
    CHECK(spe::MAX_OBJECTS == SPE_MAX_OBJECTS, "MAX_OBJECTS tracks SPE_MAX_OBJECTS");
    CHECK(engine.objCacheSize() == static_cast<size_t>(spe::MAX_OBJECTS),
          "obj_cache_ sized to MAX_OBJECTS");

    // Before any command this slot is inactive → output silent.
    CHECK(!engine.objCacheActiveAt(obj), "boundary object inactive before command");

    // Send an ObjMove for the boundary object: az 0.3, el 0.1, dist 1.0.
    // ObjMove sets active=true in obj_cache_ during the FIFO drain.
    spe::ipc::Command cmd;
    cmd.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p;
    p.obj_id = obj;
    p.az_rad = 0.3f;
    p.el_rad = 0.1f;
    p.dist_m = 1.0f;
    cmd.payload = p;
    engine.dispatchCommand(cmd);

    // Render a few blocks (the first block drains the FIFO and starts the tone).
    double energy = 0.0;
    for (int b = 0; b < 4; ++b)
        energy = drive_one_block_energy(engine, 8);

    // (1) the boundary object's cache slot is now populated/active.
    CHECK(engine.objCacheActiveAt(obj),
          "obj_cache_[MAX_OBJECTS-1] active after ObjMove drain");

    // (2) the rendered speaker output is non-silent.
    CHECK(energy > 1e-6,
          "speaker output non-silent for boundary object (renders at the cap)");

    if (failures == 0) {
        std::printf("[PASS] test_p_object_cap_render (cap=%d, obj=%u, energy=%.4f)\n",
                    spe::MAX_OBJECTS, obj, energy);
        return 0;
    }
    std::fprintf(stderr, "test_p_object_cap_render FAILED: %d assertion(s)\n", failures);
    return 1;
}

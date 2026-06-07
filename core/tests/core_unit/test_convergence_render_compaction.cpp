// test_convergence_render_compaction.cpp
// Dreamscape Convergence v1.0 — Phase 1.2: per-algorithm active-object compaction.
//
// audioBlock() now skips fill/processBlock/scratch-sum for any renderer with 0
// active objects, and sums only the renderers that ran. The claim is that this
// is BIT-EXACT vs. always running all five renderers, because each renderer
// produces all-zero output with 0 active objects and advances no per-object
// state for inactive objects.
//
// The dangerous failure mode is a SKIPPED renderer leaking its stale scratch
// into the mix, or the skip changing a later block's output. This test catches
// both with a byte-exact comparison:
//
//   Engine A: a VBAP object active for the whole run, PLUS a WFS object that is
//             active for the first blocks and then DEACTIVATED. Once the WFS
//             object is inactive, WFS has 0 active objects and is skipped.
//   Engine B: the SAME VBAP object, and NO WFS object, ever.
//
// After the WFS object is deactivated, every measured output block of A must be
// bit-identical to B. If the skipped WFS renderer leaked stale scratch (it
// rendered audibly in earlier blocks), or the skip perturbed the VBAP path, the
// byte comparison fails.
//
// A second check drives all five algorithms at once and asserts the output is
// finite and non-silent (the compacted multi-renderer sum still works).

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static constexpr float kPi = 3.14159265358979323846f;

// 16-speaker dome: lower ring (el=-20°) 0..7, upper ring (el=+30°) 8..15.
static spe::geometry::SpeakerLayout make_dome() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "compaction_dome"; l.regularity = Regularity::IRREGULAR;
    int ch = 1;
    for (float el_deg : {-20.f, 30.f}) {
        const float el = el_deg * kPi / 180.f;
        for (int i = 0; i < 8; ++i) {
            const float az = (-kPi) + 2.f * kPi * (float) i / 8.f;
            const float ce = std::cos(el);
            Speaker s; s.channel = ch++;
            s.x = ce * std::sin(az); s.y = std::sin(el); s.z = ce * std::cos(az);
            l.speakers.push_back(s);
        }
    }
    return l;
}

namespace {

constexpr int kFrames      = 256;
constexpr int kTotalBlocks = 24;
constexpr int kDeactBlock  = 10;  // WFS object deactivated at the START of this block
constexpr int kMeasureFrom = 10;  // measure from the first all-WFS-inactive block

void move(spe::core::SpatialEngine& e, uint32_t id, float az, float el, float dist) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p; p.obj_id = id; p.az_rad = az; p.el_rad = el; p.dist_m = dist;
    c.payload = p; e.dispatchCommand(c);
}
void setAlgo(spe::core::SpatialEngine& e, uint32_t id, spe::ipc::Algorithm a) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjAlgo;
    spe::ipc::PayloadObjAlgo p; p.obj_id = id; p.algo = a;
    c.payload = p; e.dispatchCommand(c);
}
void setActive(spe::core::SpatialEngine& e, uint32_t id, bool on) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjActive;
    spe::ipc::PayloadObjActive p; p.obj_id = id; p.active = on;
    c.payload = p; e.dispatchCommand(c);
}

// Run the scene; return the concatenated output samples of blocks [kMeasureFrom,
// kTotalBlocks). transient_algo (>=0) adds an object on that algorithm which is
// deactivated at kDeactBlock (so its renderer is then skipped); -1 = none.
std::vector<float> run_scene(int transient_algo, int n_spk) {
    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_dome());
    engine.prepareToPlay(48000.0, kFrames);

    // The VBAP object that is present in EVERY variant, unchanged throughout.
    setAlgo(engine, 0, spe::ipc::Algorithm::VBAP);
    move(engine, 0, 0.6f, 0.2f, 2.0f);

    if (transient_algo >= 0) {
        setAlgo(engine, 1, static_cast<spe::ipc::Algorithm>(transient_algo));  // WFS triggers ensureAllocated
        move(engine, 1, -0.8f, -0.3f, 3.0f);
    }

    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    std::vector<float> tail;
    tail.reserve(static_cast<size_t>((kTotalBlocks - kMeasureFrom) * kFrames * n_spk));

    for (int b = 0; b < kTotalBlocks; ++b) {
        if (transient_algo >= 0 && b == kDeactBlock) setActive(engine, 1, false);

        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = n_spk;
        blk.num_frames = kFrames;
        blk.sample_rate = 48000.0;
        engine.audioBlock(blk);

        if (b >= kMeasureFrom)
            for (int s = 0; s < n_spk; ++s)
                for (int n = 0; n < kFrames; ++n)
                    tail.push_back(bufs[static_cast<size_t>(s)][static_cast<size_t>(n)]);
    }
    return tail;
}

}  // namespace

int main() {
    const int N = 16;

    // --- Bit-exact equivalence: a transient renderer (then skipped) must yield
    // output bit-identical to the never-had-it baseline. Tested for both WFS
    // (memset+skip path) and Ambisonic (the subtle path: no memset, always
    // decodes — equivalence rests on the stateless-matmul argument). ---
    const std::vector<float> baseline = run_scene(/*transient_algo=*/-1, N);

    // Sanity: the VBAP object actually produces sound in the measured window
    // (otherwise a byte-identical pair of silences would pass vacuously).
    double e_base = 0.0;
    for (float v : baseline) e_base += static_cast<double>(v) * v;
    CHECK(e_base > 1.0e-6, "measured window is non-silent (VBAP object renders)");

    struct Variant { const char* name; spe::ipc::Algorithm algo; };
    const Variant variants[] = {
        {"WFS",       spe::ipc::Algorithm::WFS},
        {"Ambisonic", spe::ipc::Algorithm::Ambisonic},
    };
    for (const auto& var : variants) {
        const std::vector<float> withT =
            run_scene(static_cast<int>(var.algo), N);
        CHECK(withT.size() == baseline.size(), "measured tail sizes match");
        std::size_t mism = 0;
        const std::size_t n = std::min(withT.size(), baseline.size());
        for (std::size_t i = 0; i < n; ++i)
            if (std::memcmp(&withT[i], &baseline[i], sizeof(float)) != 0) ++mism;
        std::printf("[compaction] transient=%s  samples=%zu  bit-mismatches=%zu\n",
                    var.name, n, mism);
        CHECK(mism == 0,
              "skipping a 0-active renderer is bit-exact "
              "(no stale-scratch leak, VBAP path unperturbed)");
    }
    std::printf("[compaction] baseline energy=%.6g\n", e_base);

    // --- All five algorithms active at once: compacted sum stays finite/loud. ---
    {
        spe::core::SpatialEngine engine(0);
        engine.setLayout(make_dome());
        engine.prepareToPlay(48000.0, kFrames);
        const spe::ipc::Algorithm algos[5] = {
            spe::ipc::Algorithm::VBAP, spe::ipc::Algorithm::WFS,
            spe::ipc::Algorithm::DBAP, spe::ipc::Algorithm::Ambisonic,
            spe::ipc::Algorithm::VAP };
        for (uint32_t i = 0; i < 5; ++i) {
            setAlgo(engine, i, algos[i]);
            move(engine, i, -1.0f + 0.4f * static_cast<float>(i), 0.1f, 2.0f);
        }
        std::vector<std::vector<float>> bufs(static_cast<size_t>(N),
                                             std::vector<float>(kFrames, 0.f));
        std::vector<float*> ptrs(static_cast<size_t>(N));
        for (int s = 0; s < N; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

        double energy = 0.0; bool finite = true;
        for (int b = 0; b < 12; ++b) {
            spe::audio_io::AudioBlock blk;
            blk.output_channels = ptrs.data();
            blk.output_channel_count = N;
            blk.num_frames = kFrames;
            blk.sample_rate = 48000.0;
            engine.audioBlock(blk);
            for (int s = 0; s < N; ++s)
                for (int nn = 0; nn < kFrames; ++nn) {
                    const float v = bufs[static_cast<size_t>(s)][static_cast<size_t>(nn)];
                    if (!std::isfinite(v)) finite = false;
                    energy += static_cast<double>(v) * v;
                }
        }
        std::printf("[compaction] all-5-algos energy=%.6g finite=%d\n", energy, finite ? 1 : 0);
        CHECK(finite, "all-5-algorithm output is finite");
        CHECK(energy > 1.0e-6, "all-5-algorithm output is non-silent");
    }

    if (failures == 0) { std::printf("test_convergence_render_compaction: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_render_compaction: %d FAILURE(S)\n", failures);
    return 1;
}

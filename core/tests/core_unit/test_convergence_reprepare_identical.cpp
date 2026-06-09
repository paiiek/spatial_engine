// test_convergence_reprepare_identical.cpp
// Phase 4.3 Inc 3 (Dreamscape convergence) — reprepareForLayout() is a
// byte-identical code-motion extraction of prepareToPlay's has_layout_ branch.
//
// Two proofs:
//   (A) Extraction is byte-identical. A freshly prepared engine and an engine
//       that additionally calls reprepareForLayout() once (idempotent re-prep on
//       fresh, no-pending state) must render the SAME scene BIT-FOR-BIT. If the
//       extraction dropped, reordered, or altered any layout-dependent init
//       (renderer prepare / scratch sizing / per-speaker delay+gain / limiters /
//       render_ready), the 24-block output would diverge.
//   (B) reprepareForLayout() is safe to call standalone (the Inc 4 use case):
//       after rendering several blocks it re-prepares without crashing and the
//       engine keeps producing finite output at the same speaker count.
//
// Harness mirrors test_convergence_render_compaction.cpp (same dome + driver).

#include "core/SpatialEngine.h"
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

// 16-speaker dome (same as the compaction test): lower ring el=-20°, upper +30°.
static spe::geometry::SpeakerLayout make_dome() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "reprepare_dome"; l.regularity = Regularity::IRREGULAR;
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
constexpr int kReprepMid   = 8;   // (B) block at which a mid-stream reprep fires

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

// Render the full scene; if reprep_once, call reprepareForLayout() right after
// prepareToPlay (before any block) — an idempotent re-prep of fresh state.
std::vector<float> run_scene(bool reprep_once, int n_spk) {
    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_dome());
    engine.prepareToPlay(48000.0, kFrames);
    if (reprep_once) engine.reprepareForLayout();

    // A VBAP object (always present) + a WFS object (exercises ensureAllocated,
    // per-speaker delay lines) so the measured output depends on the full
    // layout-dependent state that reprepareForLayout rebuilds.
    setAlgo(engine, 0, spe::ipc::Algorithm::VBAP);
    move(engine, 0, 0.6f, 0.2f, 2.0f);
    setAlgo(engine, 1, spe::ipc::Algorithm::WFS);
    move(engine, 1, -0.8f, -0.3f, 3.0f);

    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    std::vector<float> all;
    all.reserve(static_cast<size_t>(kTotalBlocks * kFrames * n_spk));
    for (int b = 0; b < kTotalBlocks; ++b) {
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = n_spk;
        blk.num_frames = kFrames;
        blk.sample_rate = 48000.0;
        engine.audioBlock(blk);
        for (int s = 0; s < n_spk; ++s)
            for (int n = 0; n < kFrames; ++n)
                all.push_back(bufs[static_cast<size_t>(s)][static_cast<size_t>(n)]);
    }
    return all;
}

}  // namespace

int main() {
    const int N = 16;

    // --- (A) byte-identical extraction --------------------------------------
    const std::vector<float> base   = run_scene(/*reprep_once=*/false, N);
    const std::vector<float> reprep = run_scene(/*reprep_once=*/true,  N);

    double e_base = 0.0;
    for (float v : base) e_base += static_cast<double>(v) * v;
    CHECK(e_base > 1.0e-6, "measured scene is non-silent (would-be vacuous otherwise)");

    CHECK(reprep.size() == base.size(), "output sizes match");
    std::size_t mism = 0;
    const std::size_t n = std::min(reprep.size(), base.size());
    for (std::size_t i = 0; i < n; ++i)
        if (std::memcmp(&reprep[i], &base[i], sizeof(float)) != 0) ++mism;
    std::printf("[reprepare] samples=%zu  bit-mismatches=%zu\n", n, mism);
    CHECK(mism == 0,
          "prepare vs prepare+reprepareForLayout is BIT-IDENTICAL "
          "(extraction preserves all layout-dependent init)");

    // --- (B) mid-stream reprep is safe + keeps rendering finite -------------
    {
        spe::core::SpatialEngine engine(0);
        engine.setLayout(make_dome());
        engine.prepareToPlay(48000.0, kFrames);
        setAlgo(engine, 0, spe::ipc::Algorithm::VBAP);
        move(engine, 0, 0.3f, 0.1f, 2.0f);

        std::vector<std::vector<float>> bufs(static_cast<size_t>(N),
                                             std::vector<float>(kFrames, 0.f));
        std::vector<float*> ptrs(static_cast<size_t>(N));
        for (int s = 0; s < N; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

        bool all_finite = true;
        for (int b = 0; b < kTotalBlocks; ++b) {
            if (b == kReprepMid) engine.reprepareForLayout();  // standalone re-prep
            spe::audio_io::AudioBlock blk;
            blk.output_channels = ptrs.data();
            blk.output_channel_count = N;
            blk.num_frames = kFrames;
            blk.sample_rate = 48000.0;
            engine.audioBlock(blk);
            for (int s = 0; s < N; ++s)
                for (int nn = 0; nn < kFrames; ++nn)
                    if (!std::isfinite(bufs[static_cast<size_t>(s)][static_cast<size_t>(nn)]))
                        all_finite = false;
        }
        CHECK(all_finite, "mid-stream reprepareForLayout keeps render output finite (no crash/NaN)");
    }

    if (failures == 0) { std::printf("test_convergence_reprepare_identical: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_reprepare_identical: %d FAILURE(S)\n", failures);
    return 1;
}

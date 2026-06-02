// core/tests/perf/perf_obj_block_time.cpp
//
// v0.9 Lane C (C-M3 + C-M4) — RT-budget + memory-footprint measurement harness.
//
// Drives SpatialEngine through its real audioBlock() callback at the HEAVIEST
// configuration and measures per-block processing time across a sweep of active
// object counts, plus the process max-RSS at the compiled cap.
//
// Heaviest configuration (per plan §C-M3):
//   • 8-channel speaker output (circular layout, built in-process so the harness
//     is CWD-independent).
//   • VBAP algorithm on every object (ObjCache default = VBAP).
//   • B2 binaural side-output ENABLED with a real SOFA (synthetic_min.speh) so
//     primeAllSlots() primes all MAX_OBJECTS OlaConvolver quads, and the per-block
//     Direct (B1) path runs setDirection() + processBlockForObject() — a dual-slot
//     OlaConvolver convolution — for EVERY active object (linear in object count,
//     the dominant per-object binaural cost).
//
// Object counts above the compiled cap (spe::MAX_OBJECTS) are SKIPPED: at the
// 64-config only counts <= 64 run; the 96/128 rows require the 128 build.
//
// Timing integrity (per plan §"How to run"):
//   • Raw std::chrono::steady_clock timestamps are captured around each
//     audioBlock() into a per-scenario vector; median/p99/max are computed
//     EXACTLY from the sorted vector (NOT from CpuMeter's P² estimate).
//   • CpuMeter peakPct()/p99Us()/cpuPct() are ALSO read for cross-check.
//   • Build Release (-DCMAKE_BUILD_TYPE=Release) for accurate optimized timing.
//
// GATE (hard, per plan §C-M3 / Critic condition 3 — authoritative):
//   At the COMPILED cap in the heaviest scenario:
//     peak per-block time <= 50% of RT budget (~667 µs) AND engineOverrunCount()==0.
//   p99 is informational. C-M4: max-RSS at the cap < 100 MB.
// Exits non-zero if the gate fails (do NOT fake a pass).
//
// RT budget = block_size / sample_rate = 64 / 48000 ≈ 1333.3 µs.

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioBackend.h"

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 64;
// RT budget for one block, in microseconds.
constexpr double kBudgetUs   = static_cast<double>(kBlock) / kSampleRate * 1e6; // ≈1333.3
// Hard gate: peak per-block must stay <= 50% of budget.
constexpr double kGatePeakUs = 0.50 * kBudgetUs;                                // ≈666.7
// C-M4 memory ceiling.
constexpr long   kRssCeilingKB = 100 * 1024;  // 100 MB

constexpr int kWarmupBlocks = 2000;
constexpr int kTimedBlocks  = 8000;

// 8-channel circular speaker layout (built in-process → CWD-independent).
spe::geometry::SpeakerLayout make8chLayout() {
    spe::geometry::SpeakerLayout l;
    l.name       = "perf_obj_8ch_circular";
    l.regularity = spe::geometry::Regularity::CIRCULAR;
    for (int i = 0; i < 8; ++i) {
        const float az =
            static_cast<float>(i) * 45.f * static_cast<float>(M_PI) / 180.f;
        spe::geometry::Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = 0.f;
        s.z = std::cos(az);
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] =
            static_cast<int16_t>(i);
    }
    return l;
}

// Activate object obj_id at a distinct direction with unit gain (default VBAP).
void activateObject(spe::core::SpatialEngine& engine, int obj_id) {
    spe::ipc::Command move{};
    move.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p{};
    p.obj_id = obj_id;
    // Spread directions around the sphere so VBAP/HRTF do real per-object work.
    p.az_rad = static_cast<float>(obj_id) * 0.196349541f; // ~11.25° steps
    p.el_rad = static_cast<float>((obj_id % 5) - 2) * 0.2f;
    p.dist_m = 1.0f;
    move.payload = p;
    engine.dispatchCommand(move);

    spe::ipc::Command gain{};
    gain.tag = spe::ipc::CommandTag::ObjGain;
    spe::ipc::PayloadObjGain pg{};
    pg.obj_id = obj_id;
    pg.gain   = 1.0f;
    gain.payload = pg;
    engine.dispatchCommand(gain);
}

struct ScenarioResult {
    int    n_obj      = 0;
    double median_us  = 0.0;
    double peak_us    = 0.0;   // exact true max
    double p99_us     = 0.0;   // exact p99
    std::uint64_t xruns = 0;
    // CpuMeter cross-check (% of budget / µs).
    std::uint32_t meter_cpu_pct  = 0;
    std::uint32_t meter_peak_pct = 0;
    std::uint32_t meter_p99_us   = 0;
};

long currentMaxRssKB() {
    struct rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss; // KB on Linux
}

// Run one scenario: n_obj active objects, heaviest config. Returns timing stats.
ScenarioResult runScenario(int n_obj, long* out_rss_kb) {
    ScenarioResult r;
    r.n_obj = n_obj;

    spe::core::SpatialEngine engine(/*listen_port=*/0);
    engine.setLayout(make8chLayout());
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) +
                               "/synthetic_min.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(kSampleRate, kBlock);
    // Default mode is B1 Direct: per-object dual-slot OlaConvolver convolution.

    for (int i = 0; i < n_obj; ++i) activateObject(engine, i);

    // Output buffers (8 channels).
    constexpr int kOutCh = 8;
    std::vector<std::vector<float>> outs(
        kOutCh, std::vector<float>(static_cast<std::size_t>(kBlock), 0.f));
    std::vector<float*> out_ptrs(kOutCh);
    for (int c = 0; c < kOutCh; ++c)
        out_ptrs[static_cast<std::size_t>(c)] =
            outs[static_cast<std::size_t>(c)].data();

    spe::audio_io::AudioBlock block{};
    block.input_channels       = nullptr;
    block.input_channel_count  = 0;
    block.output_channels      = out_ptrs.data();
    block.output_channel_count = kOutCh;
    block.num_frames           = kBlock;
    block.hw_timestamp_ns      = 0;

    // Warmup: drain the cmd FIFO (activations), prime OlaConvolver crossfades,
    // settle EWMA/peak. Not timed.
    for (int b = 0; b < kWarmupBlocks; ++b) engine.audioBlock(block);

    // Snapshot max-RSS after the heaviest config is fully primed + warmed.
    if (out_rss_kb) *out_rss_kb = currentMaxRssKB();

    // Timed loop: raw steady_clock around each audioBlock.
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(kTimedBlocks));
    using Clock = std::chrono::steady_clock;
    for (int b = 0; b < kTimedBlocks; ++b) {
        const auto t0 = Clock::now();
        engine.audioBlock(block);
        const auto t1 = Clock::now();
        const double us =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                .count() * 1e-3;
        samples.push_back(us);
    }

    std::sort(samples.begin(), samples.end());
    const std::size_t n = samples.size();
    r.median_us = samples[n / 2];
    std::size_t p99_idx = static_cast<std::size_t>(static_cast<double>(n) * 0.99);
    if (p99_idx >= n) p99_idx = n - 1;
    r.p99_us  = samples[p99_idx];
    r.peak_us = samples[n - 1];

    r.xruns          = engine.engineOverrunCount();
    r.meter_cpu_pct  = engine.cpuMeter().cpuPct();
    r.meter_peak_pct = engine.cpuMeter().peakPct();
    r.meter_p99_us   = engine.cpuMeter().p99Us();

    engine.releaseResources();
    return r;
}

double pctBudget(double us) { return us / kBudgetUs * 100.0; }

} // namespace

int main() {
    std::printf("perf_obj_block_time: RT-budget + RSS sweep "
                "(heaviest path: 8ch + VBAP + B2 binaural Direct per-object)\n");
    std::printf("  compiled cap MAX_OBJECTS = %d\n", spe::MAX_OBJECTS);
    std::printf("  block=%d  sample_rate=%.0f  RT budget=%.1f us  "
                "50%% gate=%.1f us\n",
                kBlock, kSampleRate, kBudgetUs, kGatePeakUs);
    std::printf("  warmup=%d timed=%d blocks/scenario\n\n",
                kWarmupBlocks, kTimedBlocks);

    const int sweep[] = {8, 16, 32, 64, 96, 128};

    // RSS at the SMALLEST count (baseline) and at the compiled cap.
    long rss_at_cap_kb = 0;
    long rss_baseline_kb = 0;

    std::printf("  %4s | %10s | %10s | %10s | %8s | %8s | %8s | %6s | %5s\n",
                "obj", "median_us", "p99_us", "peak_us",
                "med%bud", "p99%bud", "peak%bud", "xruns", "skip");
    std::printf("  -----+------------+------------+------------+"
                "----------+----------+----------+--------+------\n");

    std::vector<ScenarioResult> results;
    bool cap_measured = false;
    ScenarioResult cap_result;

    for (int n_obj : sweep) {
        if (n_obj > spe::MAX_OBJECTS) {
            std::printf("  %4d | %10s | %10s | %10s | %8s | %8s | %8s | %6s | "
                        "SKIP (> cap %d)\n",
                        n_obj, "-", "-", "-", "-", "-", "-", "-",
                        spe::MAX_OBJECTS);
            continue;
        }

        long rss_kb = 0;
        ScenarioResult r = runScenario(n_obj, &rss_kb);
        results.push_back(r);

        if (n_obj == sweep[0]) rss_baseline_kb = rss_kb;
        if (n_obj == spe::MAX_OBJECTS) {
            rss_at_cap_kb = rss_kb;
            cap_measured  = true;
            cap_result    = r;
        }

        std::printf("  %4d | %10.2f | %10.2f | %10.2f | %7.1f%% | %7.1f%% | "
                    "%7.1f%% | %6llu |\n",
                    r.n_obj, r.median_us, r.p99_us, r.peak_us,
                    pctBudget(r.median_us), pctBudget(r.p99_us),
                    pctBudget(r.peak_us),
                    static_cast<unsigned long long>(r.xruns));
    }

    std::printf("\n  CpuMeter cross-check (%% of budget; p99 us est.):\n");
    std::printf("  %4s | %10s | %10s | %10s\n",
                "obj", "cpu%(med)", "peak%", "p99_us(est)");
    for (const auto& r : results) {
        std::printf("  %4d | %9u%% | %9u%% | %10u\n",
                    r.n_obj, r.meter_cpu_pct, r.meter_peak_pct, r.meter_p99_us);
    }

    // ── C-M4: memory footprint ──────────────────────────────────────────────
    std::printf("\n  Memory (getrusage ru_maxrss, process-wide high-water):\n");
    std::printf("    baseline (%d obj): %ld KB (%.1f MB)\n",
                sweep[0], rss_baseline_kb,
                static_cast<double>(rss_baseline_kb) / 1024.0);
    if (cap_measured) {
        std::printf("    at cap   (%d obj): %ld KB (%.1f MB)  [ceiling %d MB]\n",
                    spe::MAX_OBJECTS, rss_at_cap_kb,
                    static_cast<double>(rss_at_cap_kb) / 1024.0,
                    static_cast<int>(kRssCeilingKB / 1024));
    }

    // ── Gate verdict ────────────────────────────────────────────────────────
    if (!cap_measured) {
        std::printf("\n  ERROR: compiled cap (%d) not in sweep — cannot gate.\n",
                    spe::MAX_OBJECTS);
        return 2;
    }

    bool pass = true;

    std::printf("\n  GATE (authoritative: peak <= 50%% budget AND xruns == 0 "
                "at cap=%d):\n", spe::MAX_OBJECTS);
    const bool peak_ok = cap_result.peak_us <= kGatePeakUs;
    const bool xrun_ok = cap_result.xruns == 0;
    std::printf("    peak = %.2f us (%.1f%% budget) %s %.1f us  -> %s\n",
                cap_result.peak_us, pctBudget(cap_result.peak_us),
                peak_ok ? "<=" : ">", kGatePeakUs,
                peak_ok ? "PASS" : "FAIL");
    std::printf("    xruns = %llu -> %s\n",
                static_cast<unsigned long long>(cap_result.xruns),
                xrun_ok ? "PASS" : "FAIL");
    std::printf("    p99 (informational) = %.2f us (%.1f%% budget)\n",
                cap_result.p99_us, pctBudget(cap_result.p99_us));
    if (!peak_ok || !xrun_ok) pass = false;

    const bool rss_ok = rss_at_cap_kb < kRssCeilingKB;
    std::printf("\n  C-M4 GATE (max-RSS at cap < 100 MB):\n");
    std::printf("    RSS = %.1f MB %s 100 MB -> %s\n",
                static_cast<double>(rss_at_cap_kb) / 1024.0,
                rss_ok ? "<" : ">=", rss_ok ? "PASS" : "FAIL");
    if (!rss_ok) pass = false;

    // F3 (lazy-prime) promotion trigger: RSS within ~20% of the 100 MB ceiling.
    // NOTE: the dominant memory term measured here is WFSRenderer::delays_
    // (std::vector<DelayLine> sized MAX_OBJECTS * num_speakers; each DelayLine
    // = 192 KB), NOT the binaural OlaConvolvers. At 128 obj / 8 spk that is
    // ~192 MB alone — see docs/RT_BUDGET_MAX_OBJECTS.md. F3 (binaural
    // lazy-prime) targets only ~4 MB of OlaConvolver state, so F3 ALONE does
    // NOT bring RSS under 100 MB; the WFS (and PerObjectChain) per-object
    // delay-line allocation is the real driver and must be addressed.
    if (static_cast<double>(rss_at_cap_kb) / 1024.0 >= 80.0) {
        std::printf("    [F3 TRIGGER] RSS >= 80 MB — lazy-prime (F3) flagged "
                    "for promotion, BUT note the dominant term is WFS delays_ "
                    "(see doc); F3 alone is insufficient.\n");
    } else {
        std::printf("    [F3] RSS comfortably < 80 MB — F3 stays a follow-up.\n");
    }

    std::printf("\n  VERDICT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

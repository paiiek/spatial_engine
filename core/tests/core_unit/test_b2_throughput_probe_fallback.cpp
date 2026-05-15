// test_b2_throughput_probe_fallback.cpp
//
// v0.5 P4 (A6): under-spec CPU forces B2→B1 fallback. The plug-in must:
//   * Clamp effective_mode_ to Direct.
//   * Preserve requested_mode_ (so a faster system can later restore B2).
//   * Surface probeWarningCode() == "ambivs_disabled_cpu" with the measured
//     throughput in probeThroughput() for telemetry.

#include "output_backend/BinauralMonitor.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

#define REQUIRE(cond)                                                  \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr,                                        \
                         "FAIL: %s (line %d)\n", #cond, __LINE__);      \
            return 1;                                                   \
        }                                                               \
    } while (0)

int main()
{
    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = 48000.f;
    cfg.blockSize  = 64;
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    REQUIRE(mon.hasHrtf());

    // User requests AmbiVS. Before any probe, effective_mode_ should track
    // the request (probe_warning_set_ defaults to false).
    mon.setRequestedMode(spe::output::BinauralMode::AmbiVS);
    REQUIRE(mon.requestedMode() == spe::output::BinauralMode::AmbiVS);
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);
    REQUIRE(std::string(mon.probeWarningCode()).empty());

    // Inject a slow-CPU probe result: 0.5x RT (below kMinB2Throughput = 1.5).
    mon.injectProbeThroughputForTest(0.5f);

    // Effective mode must clamp to Direct; requested intent is preserved.
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::Direct);
    REQUIRE(mon.requestedMode() == spe::output::BinauralMode::AmbiVS);
    REQUIRE(std::strcmp(mon.probeWarningCode(), "ambivs_disabled_cpu") == 0);
    REQUIRE(mon.probeThroughput() == 0.5f);

    // Later, on a faster system or after a sample-rate change, the probe
    // measures >= 1.5x → fallback is cleared and the request is honoured.
    mon.injectProbeThroughputForTest(2.0f);
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);
    REQUIRE(std::string(mon.probeWarningCode()).empty());
    REQUIRE(mon.probeThroughput() == 2.0f);

    // Real probe (will be very fast on CI HW): expect >= 1.5x and no warning.
    const float throughput = mon.runThroughputProbe();
    REQUIRE(throughput > 0.f);
    // On any modern CI runner this will be far above 1.5x; we don't assert
    // the exact value to avoid flakes, but we DO assert that on a healthy
    // run the warning code stays empty and effective_mode tracks the request.
    if (throughput >= spe::output::kMinB2Throughput) {
        REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);
        REQUIRE(std::string(mon.probeWarningCode()).empty());
    } else {
        // Slow runner — still verify the fallback path triggered.
        REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::Direct);
        REQUIRE(std::strcmp(mon.probeWarningCode(), "ambivs_disabled_cpu") == 0);
    }

    std::puts("PASS test_b2_throughput_probe_fallback");
    return 0;
}

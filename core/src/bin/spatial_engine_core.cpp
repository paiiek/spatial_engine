// core/src/bin/spatial_engine_core.cpp
//
// P1: console host that wires SpatialEngine to a chosen audio backend
// (null on CI / dev machines; dante on the lab box). Runs until SIGINT or
// the requested duration elapses.

#include "audio_io/AudioBackend.h"
#include "audio_io/NullBackend.h"
#include "core/Constants.h"
#include "core/SpatialEngine.h"
#include "util/RtAssertNoAlloc.h"

#if defined(SPE_HAVE_JUCE)
#include "audio_io/DanteBackend.h"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_quit{false};

void handle_sigint(int) { g_quit.store(true); }

void print_banner(const std::string& backend_name) {
    std::printf("spatial_engine_core v0.1.0 (P1 host)\n");
    std::printf("  MAX_OBJECTS=%d  MAX_BLOCK=%d  ALGO_SWAP_K=%d\n",
                spe::MAX_OBJECTS, spe::MAX_BLOCK, spe::ALGO_SWAP_K);
    std::printf("  schema_version=%u  heartbeat=%d Hz (miss>=%d)\n",
                spe::SCHEMA_VERSION, spe::HEARTBEAT_HZ, spe::HEARTBEAT_MISS_THRESHOLD);
#ifdef SPE_HAVE_JUCE
    std::printf("  JUCE: linked\n");
#else
    std::printf("  JUCE: not linked\n");
#endif
    std::printf("  backend=%s\n", backend_name.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    std::string backend_name = "null";
    int run_seconds = 5;
    int block_size  = 64;
    int channels    = 8;
    double sr       = 48000.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](int defv) -> int {
            return (i + 1 < argc) ? std::atoi(argv[++i]) : defv;
        };
        if      (a == "--backend"      && i + 1 < argc) backend_name = argv[++i];
        else if (a == "--seconds")                       run_seconds = next(run_seconds);
        else if (a == "--block")                          block_size = next(block_size);
        else if (a == "--channels")                       channels   = next(channels);
        else if (a == "--rate")                           sr         = next(static_cast<int>(sr));
        else if (a == "--help" || a == "-h") {
            std::printf("Usage: spatial_engine_core [--backend null|dante] "
                        "[--seconds N] [--block 64] [--channels 8] [--rate 48000]\n");
            return 0;
        }
    }

    print_banner(backend_name);

    std::signal(SIGINT, handle_sigint);

    std::unique_ptr<spe::audio_io::AudioBackend> backend;
    if (backend_name == "dante") {
#if defined(SPE_HAVE_JUCE)
        backend = spe::audio_io::make_dante_backend(sr, block_size);
#else
        std::fprintf(stderr, "[spatial_engine_core] dante backend requires JUCE; "
                             "falling back to null.\n");
        backend = spe::audio_io::make_null_backend(sr, channels, block_size);
        backend_name = "null";
#endif
    } else {
        backend = spe::audio_io::make_null_backend(sr, channels, block_size);
    }

    spe::core::SpatialEngine engine;
    auto err = backend->start(&engine);
    if (err != spe::audio_io::BackendError::Ok) {
        std::fprintf(stderr, "[spatial_engine_core] backend start failed: %s\n",
                     spe::audio_io::describe(err));
        return 1;
    }
    std::printf("  backend started: %s\n", backend->description().c_str());
    std::printf("  running for %d s (Ctrl-C to stop)...\n", run_seconds);

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(run_seconds);
    while (!g_quit.load() && clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    backend->stop();
    std::printf("  stopped. blocks_processed=%llu xruns=%llu rt_alloc_violations=%llu\n",
                static_cast<unsigned long long>(engine.blocksProcessed()),
                static_cast<unsigned long long>(backend->xrunCount()),
                static_cast<unsigned long long>(spe::util::rt_alloc_violations()));
    return 0;
}

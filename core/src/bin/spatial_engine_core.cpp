// core/src/bin/spatial_engine_core.cpp
#include "audio_io/AudioBackend.h"
#include "audio_io/NullBackend.h"
#include "audio_io/AudioCallback.h"
#include "core/Constants.h"
#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "ipc/CommandDecoder.h"
#include "util/RtAssertNoAlloc.h"
#include "WavWriter.h"

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

void print_banner(const std::string& backend_name, int osc_port) {
    std::printf("spatial_engine_core v0.2.0 (full render chain)\n");
    std::printf("  MAX_OBJECTS=%d  MAX_BLOCK=%d\n", spe::MAX_OBJECTS, spe::MAX_BLOCK);
#ifdef SPE_HAVE_JUCE
    std::printf("  JUCE: linked\n");
#else
    std::printf("  JUCE: not linked  OSC-UDP: port %d\n", osc_port);
#endif
    std::printf("  backend=%s\n", backend_name.c_str());
}

// Thin wrapper: captures engine output to WAV while forwarding to engine.
class WavCapture final : public spe::audio_io::AudioCallback {
public:
    WavCapture(spe::core::SpatialEngine& engine,
               int channels, double sr, const std::string& wav_path)
        : engine_(engine), writer_(channels, sr, wav_path) {}

    void prepareToPlay(double sr, int max_block) override {
        engine_.prepareToPlay(sr, max_block);
    }
    void audioBlock(const spe::audio_io::AudioBlock& block) override {
        engine_.audioBlock(block);
        // Capture what the engine wrote into output_channels
        writer_.append(const_cast<float**>(block.output_channels),
                       block.output_channel_count, block.num_frames);
    }
    void releaseResources() override {
        engine_.releaseResources();
        if (writer_.flush()) {
            std::printf("  WAV written: %s\n", writer_.path().c_str());
        }
    }

private:
    spe::core::SpatialEngine& engine_;
    spe::bin::WavWriter       writer_;
};

} // namespace

int main(int argc, char** argv) {
    std::string backend_name = "null";
    int         run_seconds  = 10;
    int         block_size   = 64;
    int         channels     = 8;
    double      sr           = 48000.0;
    int         osc_port     = 9100;
    std::string wav_path;
    std::string layout_path  = "../configs/lab_8ch.yaml";
    std::string osc_dialect  = "legacy"; // "legacy" or "adm"

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nexts = [&](const char* def) -> std::string {
            return (i + 1 < argc) ? argv[++i] : def;
        };
        auto nexti = [&](int def) -> int {
            return (i + 1 < argc) ? std::atoi(argv[++i]) : def;
        };
        if      (a == "--backend")     backend_name = nexts("null");
        else if (a == "--seconds")     run_seconds  = nexti(run_seconds);
        else if (a == "--block")       block_size   = nexti(block_size);
        else if (a == "--channels")    channels     = nexti(channels);
        else if (a == "--rate")        sr           = nexti(static_cast<int>(sr));
        else if (a == "--osc-port")    osc_port     = nexti(osc_port);
        else if (a == "--wav")         wav_path     = nexts("");
        else if (a == "--layout")      layout_path  = nexts(layout_path.c_str());
        else if (a == "--osc-dialect") osc_dialect  = nexts("legacy");
        else if (a == "--help" || a == "-h") {
            std::printf("Usage: spatial_engine_core [--backend null|dante] "
                        "[--seconds N] [--block 64] [--channels 8] [--rate 48000] "
                        "[--osc-port 9100] [--wav OUTPUT.wav] [--layout PATH.yaml] "
                        "[--osc-dialect legacy|adm]\n");
            return 0;
        }
    }

    print_banner(backend_name, osc_port);
    std::signal(SIGINT, handle_sigint);

    // Engine
    spe::core::SpatialEngine engine(osc_port);

    // Apply OSC dialect (--osc-dialect legacy|adm)
    if (osc_dialect == "adm") {
        engine.oscBackend().setDialect(spe::ipc::WireDialect::AdmV1);
        std::printf("  osc-dialect: adm (ADM-OSC v1.0 encode)\n");
    } else {
        std::printf("  osc-dialect: legacy\n");
    }

    // Load speaker layout
    auto layout_result = spe::geometry::load_layout(layout_path);
    if (spe::geometry::is_ok(layout_result)) {
        engine.setLayout(std::get<spe::geometry::SpeakerLayout>(layout_result));
        std::printf("  layout: %s (%s)\n", layout_path.c_str(),
                    std::get<spe::geometry::SpeakerLayout>(layout_result).name.c_str());
    } else {
        std::fprintf(stderr, "  [warn] layout load failed: %s -- using fallback\n",
                     std::get<std::string>(layout_result).c_str());
    }

    // Backend
    std::unique_ptr<spe::audio_io::AudioBackend> backend;
    if (backend_name == "dante") {
#if defined(SPE_HAVE_JUCE)
        backend = spe::audio_io::make_dante_backend(sr, block_size);
#else
        std::fprintf(stderr, "[warn] dante requires JUCE; using null.\n");
        backend = spe::audio_io::make_null_backend(sr, channels, block_size);
        backend_name = "null";
#endif
    } else {
        backend = spe::audio_io::make_null_backend(sr, channels, block_size);
    }

    // WAV capture wrapper (optional)
    std::unique_ptr<WavCapture> wav_cap;
    spe::audio_io::AudioCallback* callback = &engine;
    if (!wav_path.empty()) {
        wav_cap = std::make_unique<WavCapture>(engine, channels, sr, wav_path);
        callback = wav_cap.get();
        std::printf("  WAV capture: %s\n", wav_path.c_str());
    }

    auto err = backend->start(callback);
    if (err != spe::audio_io::BackendError::Ok) {
        std::fprintf(stderr, "[spatial_engine_core] backend start failed: %s\n",
                     spe::audio_io::describe(err));
        return 1;
    }
    std::printf("  backend: %s\n", backend->description().c_str());
    std::printf("  OSC listener: 0.0.0.0:%d (ADM-OSC /adm/obj/N/aed)\n", osc_port);
    std::printf("  running %d s — Ctrl-C to stop...\n", run_seconds);

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(run_seconds);
    while (!g_quit.load() && clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    backend->stop();
    std::printf("  stopped. blocks=%llu xruns=%llu\n",
                static_cast<unsigned long long>(engine.blocksProcessed()),
                static_cast<unsigned long long>(backend->xrunCount()));
    return 0;
}

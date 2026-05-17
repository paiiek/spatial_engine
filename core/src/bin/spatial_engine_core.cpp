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
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// Registry-based per-instance forwarding (ADR 0011 §4 reader-side).
// Compiled unconditionally in the standalone; the registry file is
// only populated when plugin instances run with SPATIAL_ENGINE_VST3_OSC=ON.
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// ---------------------------------------------------------------------------
// Minimal registry reader for the standalone forwarder.
// Mirrors the ADR 0011 schema; deliberately JUCE-free and dependency-free.
// ---------------------------------------------------------------------------

struct ForwardEntry {
    uint32_t instance_id;
    uint16_t port;
};

// Return $XDG_CONFIG_HOME/spatial_engine/instances.json (or ~/.config/...).
static std::string forwarder_registry_path()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && xdg[0] != '\0') {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home ? home : "/tmp") + "/.config";
    }
    return base + "/spatial_engine/instances.json";
}

// Current boot_id (cached on first call; empty string on error).
static std::string read_boot_id()
{
    std::ifstream f("/proc/sys/kernel/random/boot_id");
    if (!f.is_open()) return {};
    std::string s;
    std::getline(f, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

static constexpr long long kSupportedSchemaVersion = 1;

// Parse the instances array from the registry JSON.
// Validates schema_version, port range, and boot_id before accepting entries.
// Tolerates parse errors (returns empty on any fault) per ADR 0011 §4.
static std::vector<ForwardEntry> read_registry()
{
    std::ifstream f(forwarder_registry_path());
    if (!f.is_open()) return {};

    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    if (json.empty()) return {};

    std::vector<ForwardEntry> result;
    int registry_entry_rejected_count = 0;

    // Validate top-level schema_version before processing instances.
    auto extractTopInt = [&](const std::string& key) -> long long {
        size_t k = json.find('"' + key + '"');
        if (k == std::string::npos) return -1LL;
        size_t colon = json.find(':', k);
        if (colon == std::string::npos) return -1LL;
        size_t num_start = colon + 1;
        while (num_start < json.size() &&
               (json[num_start] == ' ' || json[num_start] == '\t' ||
                json[num_start] == '\n' || json[num_start] == '\r'))
            ++num_start;
        char* ep = nullptr;
        long long v = std::strtoll(json.c_str() + num_start, &ep, 10);
        return (ep != json.c_str() + num_start) ? v : -1LL;
    };

    long long top_schema = extractTopInt("schema_version");
    if (top_schema != kSupportedSchemaVersion) {
        std::fprintf(stderr,
            "[forwarder] registry schema_version=%lld != supported=%lld, ignoring file\n",
            top_schema, kSupportedSchemaVersion);
        return {};
    }

    // Read current boot_id for staleness check.
    const std::string current_boot_id = read_boot_id();

    // Extract per-entry fields by scanning for object boundaries inside "instances".
    size_t pos = json.find("\"instances\"");
    if (pos == std::string::npos) return {};

    size_t cursor = pos;
    while ((cursor = json.find('{', cursor)) != std::string::npos) {
        ++cursor;
        ForwardEntry e{};

        // Scan fields within this object.
        size_t obj_end = json.find('}', cursor);
        if (obj_end == std::string::npos) break;
        std::string obj = json.substr(cursor, obj_end - cursor);

        auto extractInt = [&](const std::string& key) -> long long {
            size_t k = obj.find('"' + key + '"');
            if (k == std::string::npos) return -1;
            size_t colon = obj.find(':', k);
            if (colon == std::string::npos) return -1;
            size_t num_start = colon + 1;
            while (num_start < obj.size() && (obj[num_start] == ' ' || obj[num_start] == '\t'))
                ++num_start;
            char* end_ptr = nullptr;
            long long v = std::strtoll(obj.c_str() + num_start, &end_ptr, 10);
            return (end_ptr != obj.c_str() + num_start) ? v : -1LL;
        };

        auto extractStr = [&](const std::string& key) -> std::string {
            std::string needle = '"' + key + '"';
            size_t k = obj.find(needle);
            if (k == std::string::npos) return {};
            size_t colon = obj.find(':', k + needle.size());
            if (colon == std::string::npos) return {};
            size_t q1 = obj.find('"', colon + 1);
            if (q1 == std::string::npos) return {};
            size_t q2 = obj.find('"', q1 + 1);
            if (q2 == std::string::npos) return {};
            return obj.substr(q1 + 1, q2 - q1 - 1);
        };

        long long iid          = extractInt("instance_id");
        long long port         = extractInt("port");
        long long entry_schema = extractInt("schema_version");
        std::string boot_id    = extractStr("boot_id");

        bool reject = false;

        // Validate per-entry schema_version.
        if (entry_schema != kSupportedSchemaVersion) {
            std::fprintf(stderr,
                "[forwarder] entry schema_version=%lld rejected\n", entry_schema);
            reject = true;
        }

        // Validate port is in [1024, 65535] strictly (reject 0 and ephemeral-leak 0).
        if (!reject && (port < 1024 || port > 65535)) {
            std::fprintf(stderr,
                "[forwarder] entry port=%lld out of range [1024,65535], rejected\n", port);
            reject = true;
        }

        // Validate boot_id matches current boot.
        if (!reject && !current_boot_id.empty() && boot_id != current_boot_id) {
            std::fprintf(stderr,
                "[forwarder] entry boot_id mismatch (stale), rejected\n");
            reject = true;
        }

        if (!reject && iid >= 0) {
            e.instance_id = static_cast<uint32_t>(iid);
            e.port        = static_cast<uint16_t>(port);
            result.push_back(e);
        } else if (reject) {
            ++registry_entry_rejected_count;
        }

        cursor = obj_end + 1;
    }

    if (registry_entry_rejected_count > 0) {
        std::fprintf(stderr,
            "[forwarder] tag=registry_entry_rejected_count value=%d\n",
            registry_entry_rejected_count);
    }

    return result;
}

// Forward a raw UDP datagram to each entry's port on 127.0.0.1.
// Logs WARN on ECONNREFUSED (once per call — no per-pair rate limit needed for S2).
// Tags: registry_active_instances=N forwarded_to_count=M dropped_due_unknown_obj_id=K
static void forward_to_instances(int send_fd,
                                  const std::vector<ForwardEntry>& entries,
                                  const uint8_t* buf, size_t len)
{
    int forwarded = 0;
    int dropped   = 0;

    for (const auto& e : entries) {
        struct sockaddr_in dst{};
        dst.sin_family      = AF_INET;
        dst.sin_port        = htons(e.port);
        dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        ssize_t sent = ::sendto(send_fd, buf, len, 0,
                                reinterpret_cast<struct sockaddr*>(&dst),
                                sizeof(dst));
        if (sent < 0) {
            if (errno == ECONNREFUSED || errno == EPIPE) {
                std::fprintf(stderr,
                    "[forwarder] WARN port=%d unreachable (instance_id=%u): %s\n",
                    e.port, e.instance_id, std::strerror(errno));
                ++dropped;
            }
        } else {
            ++forwarded;
        }
    }

    std::fprintf(stderr,
        "[forwarder] registry_active_instances=%zu forwarded_to_count=%d dropped_due_unknown_obj_id=%d\n",
        entries.size(), forwarded, dropped);
}

} // anonymous namespace (forwarder helpers)

#define SPE_STRINGIFY_(x) #x
#define SPE_STRINGIFY(x)  SPE_STRINGIFY_(x)
#define SPE_VERSION_STR   SPE_STRINGIFY(SPE_VERSION_MAJOR) "." \
                          SPE_STRINGIFY(SPE_VERSION_MINOR) "." \
                          SPE_STRINGIFY(SPE_VERSION_PATCH)

namespace {

std::atomic<bool> g_quit{false};
void handle_sigint(int) { g_quit.store(true); }

void print_banner(const std::string& backend_name, int osc_port) {
    std::printf("spatial_engine_core v" SPE_VERSION_STR " (full render chain)\n");
    std::printf("  MAX_OBJECTS=%d  MAX_BLOCK=%d\n", spe::MAX_OBJECTS, spe::MAX_BLOCK);
#ifdef SPE_HAVE_JUCE
    std::printf("  JUCE: linked\n");
#else
    std::printf("  JUCE: not linked  OSC-UDP: port %d\n", osc_port);
#endif
    std::printf("  backend=%s\n", backend_name.c_str());
}

// Resolve default layout path by probing common CWDs. Returns the first
// readable candidate, or empty if none found (caller falls back to fixture).
std::string resolve_default_layout(const char* basename) {
    const char* candidates[] = {
        "configs/",       // CWD == project root
        "../configs/",    // CWD == build/
        "../../configs/", // CWD == core/build/  (canonical per project CLAUDE.md)
    };
    for (const char* prefix : candidates) {
        std::string p = std::string(prefix) + basename;
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return {};
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
    // v0.5.1 Sec H2 — OSC inbound socket bind address. Default 127.0.0.1
    // (loopback only) so the unauthenticated OSC command surface is NOT
    // exposed on the LAN by default. Operators opting into cross-machine
    // deployments must pass --osc-bind 0.0.0.0 (or a specific NIC IP) and
    // acknowledge the printed WARNING.
    std::string osc_bind     = "127.0.0.1";
    std::string wav_path;
    std::string layout_path  = resolve_default_layout("lab_8ch.yaml");
    std::string osc_dialect  = "legacy"; // "legacy" or "adm"
    // v0.5.1 Q1 — optional one-shot binaural telemetry exercise. The
    // standalone otherwise never invokes the probe path (only VST3
    // setActive does), so the soak harness has no way to observe the
    // /sys/binaural_warning surface end-to-end without these flags.
    std::string binaural_sofa;
    bool        binaural_enable    = false;
    bool        force_probe        = false;
    bool        emit_no_sofa_after = false;
    int         emit_after_ms      = 1000;
    bool        inject_probe_throughput   = false;
    float       inject_probe_throughput_v = 0.5f;

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
        else if (a == "--osc-bind")    osc_bind     = nexts("127.0.0.1");
        else if (a == "--wav")         wav_path     = nexts("");
        else if (a == "--layout")      layout_path  = nexts(layout_path.c_str());
        else if (a == "--osc-dialect") osc_dialect  = nexts("legacy");
        else if (a == "--binaural-sofa") binaural_sofa = nexts("");
        else if (a == "--binaural-enable") binaural_enable = true;
        else if (a == "--force-probe") force_probe = true;
        else if (a == "--inject-probe-throughput") {
            inject_probe_throughput   = true;
            // CLI arg is the throughput multiplier (e.g. "0.5"); we
            // accept it as a string then atof for fractional values
            // since nexti() only handles ints.
            std::string raw = nexts("0.5");
            inject_probe_throughput_v = static_cast<float>(std::atof(raw.c_str()));
        }
        else if (a == "--emit-no-sofa-after-ms") {
            emit_no_sofa_after = true;
            emit_after_ms      = nexti(emit_after_ms);
        }
        else if (a == "--help" || a == "-h") {
            std::printf("Usage: spatial_engine_core [--backend null|dante] "
                        "[--seconds N] [--block 64] [--channels 8] [--rate 48000] "
                        "[--osc-port 9100] [--osc-bind 127.0.0.1] "
                        "[--wav OUTPUT.wav] [--layout PATH.yaml] "
                        "[--osc-dialect legacy|adm] [--binaural-sofa PATH] "
                        "[--binaural-enable] [--force-probe] "
                        "[--emit-no-sofa-after-ms N]\n"
                        "\n"
                        "  --osc-bind ADDR  Inbound OSC UDP bind address. Default 127.0.0.1\n"
                        "                   (loopback only — single-host IPC). Pass 0.0.0.0\n"
                        "                   to bind every interface (cross-machine setups).\n"
                        "                   SECURITY: OSC commands are UNAUTHENTICATED. Any\n"
                        "                   host that can reach the bound interface can drive\n"
                        "                   /sys/load_layout, /sys/binaural_sofa, etc. Use\n"
                        "                   non-loopback only on trusted networks behind a\n"
                        "                   firewall that filters the OSC port.\n");
            return 0;
        }
    }

    print_banner(backend_name, osc_port);
    std::signal(SIGINT, handle_sigint);

    // Engine
    spe::core::SpatialEngine engine(osc_port);

    // v0.5.1 Sec H2 — wire bind address into OSCBackend BEFORE the engine's
    // prepareToPlay() lights up the listener. Default loopback is silent;
    // explicit non-loopback opt-in prints a WARNING so operators understand
    // they have just exposed the unauthenticated OSC surface beyond 127.0.0.1.
    engine.oscBackend().setBindAddr(osc_bind);
    if (osc_bind != "127.0.0.1") {
        std::fprintf(stderr,
            "WARNING: OSC listener bound to %s. Engine is reachable from LAN; "
            "OSC commands are unauthenticated. Use only on trusted networks.\n",
            osc_bind.c_str());
    }

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

    // v0.5.1 Q1 — optional binaural pre-config so the soak harness can
    // exercise the OSC outbound warning channel from the standalone.
    if (!binaural_sofa.empty()) {
        engine.setBinauralSofaPath(binaural_sofa);
        std::printf("  binaural-sofa: %s\n", binaural_sofa.c_str());
    }
    if (binaural_enable) {
        engine.setBinauralEnabled(true);
        std::printf("  binaural-enable: 1\n");
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
    std::printf("  OSC listener: %s:%d (ADM-OSC /adm/obj/N/aed)\n",
                osc_bind.c_str(), osc_port);
    std::printf("  running %d s — Ctrl-C to stop...\n", run_seconds);

    // Registry-based instance forwarding: re-read every 5s (ADR 0011 §4).
    // A UDP send socket is created once for forwarding to plugin instances.
    int fwd_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fwd_fd < 0) {
        std::fprintf(stderr, "[forwarder] WARNING: could not create forward socket: %s\n",
                     std::strerror(errno));
    }
    std::vector<ForwardEntry> fwd_entries;
    auto last_registry_reload = std::chrono::steady_clock::time_point{};

    using clock = std::chrono::steady_clock;
    const auto start_time = clock::now();
    const auto deadline = start_time + std::chrono::seconds(run_seconds);

    // v0.5.1 Q1 — schedule the requested one-shot warning emissions. Both
    // emissions are issued AFTER emit_after_ms so the harness has time to
    // open its listener and exchange a handshake (which captures the peer
    // endpoint inside the engine's OSCBackend). emit_after_ms defaults to
    // 1000 ms which is the smallest interval that consistently gives the
    // Python harness time to land its handshake.
    const auto emit_deadline = start_time + std::chrono::milliseconds(emit_after_ms);
    bool emitted_warnings = false;

    while (!g_quit.load() && clock::now() < deadline) {
        auto now = clock::now();

        // Reload registry every 5 seconds.
        if (fwd_fd >= 0 &&
            now - last_registry_reload >= std::chrono::seconds(5)) {
            fwd_entries = read_registry();
            last_registry_reload = now;
            if (!fwd_entries.empty()) {
                std::fprintf(stderr,
                    "[forwarder] registry reloaded: registry_active_instances=%zu\n",
                    fwd_entries.size());
            }
        }

        // One-shot warning fan-out (force-probe + inject-probe-throughput
        // + no-sofa). Designed so the soak harness can choose either real
        // or synthetic probe behaviour depending on host CPU.
        if (!emitted_warnings && now >= emit_deadline) {
            emitted_warnings = true;
            if (inject_probe_throughput) {
                engine.injectProbeThroughputAndEmit(inject_probe_throughput_v);
                std::fprintf(stderr, "[probe] injected throughput=%.2fx RT code='%s'\n",
                             (double)inject_probe_throughput_v,
                             engine.binauralProbeWarningCode());
            } else if (force_probe) {
                const float t = engine.triggerBinauralProbe();
                std::fprintf(stderr, "[probe] throughput=%.2fx RT code='%s'\n",
                             (double)t, engine.binauralProbeWarningCode());
            }
            if (emit_no_sofa_after) {
                engine.oscBackend().sendReply("/sys/binaural_warning", ",s",
                                              "no_sofa_loaded");
                std::fprintf(stderr, "[no_sofa] emitted /sys/binaural_warning ,s\n");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (fwd_fd >= 0) ::close(fwd_fd);
    (void)forward_to_instances; // referenced from forwarder helpers; suppress unused warning

    backend->stop();
    std::printf("  stopped. blocks=%llu xruns=%llu\n",
                static_cast<unsigned long long>(engine.blocksProcessed()),
                static_cast<unsigned long long>(backend->xrunCount()));
    return 0;
}

// core/src/bin/spatial_engine_core.cpp
#include "audio_io/AudioBackend.h"
#include "audio_io/NullBackend.h"
#include "audio_io/AudioCallback.h"
#include "core/Constants.h"
#include "core/SpatialEngine.h"
#include "bin/MetricsEmit.h"
#include "bin/PlayerStaleWatchdog.h"
#include "geometry/LayoutLoader.h"
#include "ipc/CommandDecoder.h"
#include "ipc/SceneController.h"   // v0.9 Lane E (E-M3): scene library + load
#include "scene/CueEngine.h"       // v0.9 Lane E (E-M3): cue automation
#include "util/RtAssertNoAlloc.h"
#include "WavWriter.h"

// DanteBackend.h is JUCE-free safe (JUCE parts are guarded inside it); include
// unconditionally so list_output_devices() / --list-audio-devices works in both
// builds (returns empty without JUCE).
#include "audio_io/DanteBackend.h"

// ADR 0019 PR3: shm input backend (POSIX shm — Linux/macOS only, JUCE-free).
#if defined(__linux__) || defined(__APPLE__)
#include "audio_io/SharedRingBackend.h"
#include "audio_io/shm/SharedMemoryRegion.h"
#include "bin/ShmTelemetryEmitter.h"   // ADR 0019 PR4 (D4): control-thread tick
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <variant>
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

// v0.9 Lane E (E-M3) — scene library directory. Mirrors the registry path
// derivation: $XDG_CONFIG_HOME/spatial_engine/scenes (or ~/.config/...). The
// SceneController owns scene .json + index.json + cuelist.json here.
static std::string scenes_dir_path()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && xdg[0] != '\0') {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home ? home : "/tmp") + "/.config";
    }
    return base + "/spatial_engine/scenes";
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

void print_banner(const std::string& backend_name, const std::string& input_backend_name,
                  int osc_port) {
    std::printf("spatial_engine_core v" SPE_VERSION_STR " (full render chain)\n");
    std::printf("  MAX_OBJECTS=%d  MAX_BLOCK=%d\n", spe::MAX_OBJECTS, spe::MAX_BLOCK);
#ifdef SPE_HAVE_JUCE
    std::printf("  JUCE: linked\n");
#else
    std::printf("  JUCE: not linked  OSC-UDP: port %d\n", osc_port);
#endif
    // ADR 0019 PR3: input is selected independently of the output backend.
    std::printf("  input=%s  output=%s\n", input_backend_name.c_str(), backend_name.c_str());
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
    // ADR 0019 PR3: input backend selection, distinct from --backend (output).
    // "dante" (default) preserves current behavior (the --backend selection
    // drives I/O); "shm:<path>" pulls PCM from a producer ring; "null" is a
    // distinct null INPUT source.
    std::string input_backend = "dante";
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
    // Sec H3 — optional shared-secret for the OSC command surface. Empty = no
    // auth (default). When set, only clients presenting it via /sys/auth are
    // admitted. Recommended whenever --osc-bind is non-loopback.
    std::string osc_token;
    // Object dry-signal source: "sine" (default, internal test tones) or
    // "input" (route input_channels[i] → object i; pair with --input-backend
    // shm:<path> to render a real streamed stem through the per-object DSP).
    std::string object_source = "sine";
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
        else if (a == "--input-backend") input_backend = nexts("dante");
        else if (a == "--seconds")     run_seconds  = nexti(run_seconds);
        else if (a == "--block")       block_size   = nexti(block_size);
        else if (a == "--channels")    channels     = nexti(channels);
        else if (a == "--rate")        sr           = nexti(static_cast<int>(sr));
        else if (a == "--osc-port")    osc_port     = nexti(osc_port);
        else if (a == "--osc-bind")    osc_bind     = nexts("127.0.0.1");
        else if (a == "--wav")         wav_path     = nexts("");
        else if (a == "--layout")      layout_path  = nexts(layout_path.c_str());
        else if (a == "--osc-dialect") osc_dialect  = nexts("legacy");
        else if (a == "--osc-token")   osc_token    = nexts("");
        else if (a == "--object-source") object_source = nexts("sine");
        else if (a == "--list-audio-devices") {
            const auto devs = spe::audio_io::list_output_devices();
            std::printf("Available output audio devices (%zu):\n", devs.size());
            for (const auto& d : devs) std::printf("  %s\n", d.c_str());
            if (devs.empty())
                std::printf("  (none — headless host / no soundcard / JUCE-free build)\n");
            return 0;
        }
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
            std::printf("Usage: spatial_engine_core [--backend null|dante|device[:NAME]] "
                        "[--input-backend shm:<path>|null|dante] "
                        "[--seconds N] [--block 64] [--channels 8] [--rate 48000] "
                        "[--osc-port 9100] [--osc-bind 127.0.0.1] "
                        "[--wav OUTPUT.wav] [--layout PATH.yaml] "
                        "[--osc-dialect legacy|adm] [--object-source sine|input] "
                        "[--osc-token SECRET] "
                        "[--list-audio-devices] [--binaural-sofa PATH] "
                        "[--binaural-enable] [--force-probe] "
                        "[--emit-no-sofa-after-ms N]\n"
                        "\n"
                        "  --backend X        Output sink. null = discard; device[:NAME] = a real\n"
                        "                   soundcard via JUCE AudioDeviceManager (default device,\n"
                        "                   or the named one); dante = device alias (lab default).\n"
                        "                   --list-audio-devices prints the available outputs.\n"
                        "  --input-backend X  Input source, distinct from --backend (output).\n"
                        "                   shm:<path> = pull PCM from a producer ring (Linux/\n"
                        "                   macOS); pairs with --backend for output. null = a\n"
                        "                   distinct null INPUT source. dante (default) = current\n"
                        "                   behavior (the --backend selection drives I/O).\n"
                        "  --object-source X  Object dry signal. sine (default) = internal test\n"
                        "                   tones; input = route input_channels[i] into object i\n"
                        "                   (pair with --input-backend shm:<path> to render a real\n"
                        "                   streamed stem through the per-object DSP + panner).\n"
                        "                   Per-object input routing (channel remap + per-route\n"
                        "                   gain + fan-out) is set at RUNTIME over OSC — no new\n"
                        "                   flag: /obj/input ,iif obj_id src_ch gain (src_ch=-1\n"
                        "                   keeps the default 1:1; out-of-range src → sine tone).\n"
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

    print_banner(backend_name, input_backend, osc_port);
    std::signal(SIGINT, handle_sigint);

    // Engine
    spe::core::SpatialEngine engine(osc_port);

    // v0.5.1 Sec H2 — wire bind address into OSCBackend BEFORE the engine's
    // prepareToPlay() lights up the listener. Default loopback is silent;
    // explicit non-loopback opt-in prints a WARNING so operators understand
    // they have just exposed the unauthenticated OSC surface beyond 127.0.0.1.
    engine.oscBackend().setBindAddr(osc_bind);
    // Sec H3 — apply the OSC auth token (empty = disabled) BEFORE start().
    engine.oscBackend().setAuthToken(osc_token);
    if (!osc_token.empty()) {
        std::printf("  osc-auth: ON (clients must present the token via /sys/auth)\n");
    }
    if (osc_bind != "127.0.0.1") {
        if (osc_token.empty()) {
            std::fprintf(stderr,
                "WARNING: OSC listener bound to %s with NO --osc-token. The engine "
                "is reachable from LAN and the command surface is UNAUTHENTICATED. "
                "Pass --osc-token <secret> or use only on trusted networks.\n",
                osc_bind.c_str());
        } else {
            std::fprintf(stderr,
                "NOTE: OSC listener bound to %s; token auth is ON.\n", osc_bind.c_str());
        }
    }

    // Apply OSC dialect (--osc-dialect legacy|adm)
    if (osc_dialect == "adm") {
        engine.oscBackend().setDialect(spe::ipc::WireDialect::AdmV1);
        std::printf("  osc-dialect: adm (ADM-OSC v1.0 encode)\n");
    } else {
        std::printf("  osc-dialect: legacy\n");
    }

    // Apply object dry-signal source (--object-source sine|input)
    const bool object_source_input = (object_source == "input");
    engine.setObjectSourceInput(object_source_input);
    std::printf("  object-source: %s\n",
                object_source_input ? "input (route input_channels[i] → object i)"
                                    : "sine (internal test tones)");

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

    // ── v0.9 Lane E (E-M3) — FIRST-EVER scene/cue wiring into the daemon ──────
    // SceneController + CueEngine live on this control loop ONLY (fix 1a). The
    // CueEngine emitFn posts object-update Commands to the OSCBackend's
    // control→UDP outbound mailbox; the UDP listener thread drains + forwards
    // them via the existing sink_ so the audio cmd_fifo_ keeps ONE producer.
    const std::string scenes_dir = scenes_dir_path();
    {
        std::error_code ec;
        std::filesystem::create_directories(scenes_dir, ec);
    }
    spe::ipc::SceneController scene_ctrl(scenes_dir);
    // F4b — wire the live object-state provider so /scene/save captures the
    // authoritative per-object state from the engine (consistent three-buffer
    // snapshot of obj_cache_), instead of writing an empty objects vector.
    scene_ctrl.setObjectStateProvider(
        [&engine](std::vector<spe::ipc::ObjectSnapshot>& out) {
            engine.snapshotObjects(out);
        });
    spe::scene::CueEngine cue_engine(
        &scene_ctrl, static_cast<float>(sr),
        [&engine](const spe::ipc::Command& c) {
            return engine.oscBackend().postOutbound(c);
        });
    // Load a cuelist from disk if present (manual list otherwise empty).
    {
        spe::scene::CueList cl;
        if (cl.loadFromDisk(scenes_dir)) cue_engine.setCueList(cl);
    }
    std::printf("  scenes dir: %s\n", scenes_dir.c_str());

    // ── Backend selection (ADR 0019 PR3) ──────────────────────────────────
    // The INPUT backend (--input-backend) is the loop driver. When it is shm or
    // null, that backend owns the output staging the engine renders into and is
    // the sole driver; the --backend (output) selection only matters for the
    // dante path (current behavior).
    const bool input_is_shm  = (input_backend.rfind("shm:", 0) == 0);
    const bool input_is_null = (input_backend == "null");

    std::unique_ptr<spe::audio_io::AudioBackend> backend;

#if defined(__linux__) || defined(__APPLE__)
    std::unique_ptr<spe::audio_io::SharedRingBackend> shm_backend;  // kept alive when shm
#endif

    if (input_is_shm) {
#if defined(__linux__) || defined(__APPLE__)
        const std::string shm_path = input_backend.substr(4);
        shm_backend = spe::audio_io::SharedRingBackend::attach(
            shm_path, spe::audio_io::shm::AttachMode::OpenExisting);
        if (!shm_backend) {
            std::fprintf(stderr,
                "[spatial_engine_core] shm input attach failed: %s\n",
                shm_path.c_str());
            return 1;  // no Dante fallback — the operator explicitly asked for shm
        }
#else
        std::fprintf(stderr,
            "[spatial_engine_core] shm input requires Linux/macOS.\n");
        return 1;
#endif
    } else if (input_is_null) {
        // Gap-c: a distinct null INPUT source (input_channels=channels), separate
        // from --backend null (output). This drives the loop with a null input.
        backend = spe::audio_io::make_null_backend(sr, channels, block_size,
                                                   /*input_channels=*/channels);
    } else if (backend_name == "dante"
               || backend_name == "device"
               || backend_name.rfind("device:", 0) == 0) {
#if defined(SPE_HAVE_JUCE)
        // Real soundcard output via JUCE AudioDeviceManager. "dante" = default
        // device (the Dante virtual card in the lab); "device" = default;
        // "device:<name>" = a specific output device (see --list-audio-devices).
        std::string dev_name;
        if (backend_name.rfind("device:", 0) == 0)
            dev_name = backend_name.substr(std::string("device:").size());
        backend = spe::audio_io::make_device_backend(sr, block_size, dev_name);
#else
        std::fprintf(stderr,
            "[warn] '%s' needs a JUCE build (AudioDeviceManager); using null.\n",
            backend_name.c_str());
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

    // Start the driver. The shm path uses the 3-arg start (engine block +
    // output channels) so the backend owns the output staging; all other paths
    // use the AudioBackend 1-arg start.
    spe::audio_io::BackendError err = spe::audio_io::BackendError::Ok;
#if defined(__linux__) || defined(__APPLE__)
    if (input_is_shm) {
        err = shm_backend->start(callback, block_size, channels);
    } else
#endif
    {
        err = backend->start(callback);
    }
    if (err != spe::audio_io::BackendError::Ok) {
        std::fprintf(stderr, "[spatial_engine_core] backend start failed: %s\n",
                     spe::audio_io::describe(err));
        return 1;
    }

    // Non-owning driver pointer (SharedRingBackend IS an AudioBackend), so the
    // run loop / banner / teardown are agnostic to which input backend drives.
    spe::audio_io::AudioBackend* driver = backend.get();
#if defined(__linux__) || defined(__APPLE__)
    if (input_is_shm) driver = shm_backend.get();
#endif

    std::printf("  backend: %s\n", driver->description().c_str());
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

    // ADR 0018 D-5 — periodic external-player staleness watchdog. The check
    // detects the ABSENCE of incoming `,d` pings, so it cannot be driven by
    // ping arrival; it needs this control-thread timer. servicePlayerStaleWatchdog
    // gates the call to ~1 Hz and reads wall-clock unix ms — a cheap,
    // allocation-free clock read on the control/IO thread (NEVER the audio
    // thread). checkPlayerHeartbeatStale() is a no-op until at least one
    // external ping has been seen and rate-limits its own /sys/warning to once
    // per 30 s, so calling it every second is safe.
    auto last_stale_check = std::chrono::steady_clock::time_point{};

    // ADR 0019 PR4 (D4) — shm diagnostics tick state. A default-constructed
    // time_point forces the first shm-gated iteration to fire immediately.
    // The emitter self-seeds on its first attached tick (internal seeded_ flag)
    // so no caller-side seed bookkeeping is needed here.
#if defined(__linux__) || defined(__APPLE__)
    auto last_shm_diag_check = std::chrono::steady_clock::time_point{};
    spe::bin::ShmTelemetryEmitter shm_telemetry;
#endif

    // v0.8 P1.1 (DSP-1 / M2HOA-Q14) — ~1 Hz control-thread tick for the
    // ambisonic decoder-type runtime apply. The audio thread cannot do
    // the rebuild (it allocates), so the FIFO stores the new type and
    // this tick rebuilds + atomically publishes via the lock-free
    // double-buffer (see AmbiDecoder.h BINDING INVARIANT — 1 Hz keeps the
    // inactive slot quiescent ~93 audio blocks before reuse). The forwarder
    // is a no-op when the pending type already matches the applied type,
    // so calling it every loop iteration is cheap.
    auto last_ambi_decoder_apply = std::chrono::steady_clock::time_point{};

    // v0.9 Lane A (A-M1) — ~1 Hz /sys/metrics emit gate. A default-constructed
    // time_point forces the first iteration to fire immediately. UNCONDITIONAL:
    // emitted on every backend (null/dante/shm) so the dashboard always has
    // cpu/xrun telemetry, not just on the shm path.
    auto last_metrics_emit = std::chrono::steady_clock::time_point{};

    while (!g_quit.load() && clock::now() < deadline) {
        auto now = clock::now();

        // Wall-clock unix ms, computed ONCE per loop body (D2 / ADR §2.3:
        // producer_heartbeat_ms is wall-clock). Shared by the D-5 player-stale
        // watchdog and the PR4 shm diagnostics tick.
        const int64_t now_unix_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();

        // ADR 0018 D-5 — drive the staleness watchdog (gated to ~1 Hz inside).
        spe::bin::servicePlayerStaleWatchdog(engine, now, last_stale_check,
                                             now_unix_ms);

        // v0.9 Lane E (E-M3) — drain the UDP→control inbound mailbox (decoded
        // /cue/* + Scene* commands queued on the UDP thread) and APPLY them on
        // this control loop, then tick the cue crossfade/dwell clock. CueEngine
        // state is single-threaded here; object updates are funnelled back to
        // the UDP producer via the outbound mailbox (fix 1a).
        {
            spe::ipc::Command inc;
            while (engine.oscBackend().drainInbound(inc)) {
                switch (inc.tag) {
                    case spe::ipc::CommandTag::CueGo: {
                        const auto& p = std::get<spe::ipc::PayloadCueGo>(inc.payload);
                        cue_engine.go(p.index, now_unix_ms);
                        break;
                    }
                    case spe::ipc::CommandTag::CueNext: cue_engine.next(now_unix_ms); break;
                    case spe::ipc::CommandTag::CuePrev: cue_engine.prev(now_unix_ms); break;
                    case spe::ipc::CommandTag::CueStop: cue_engine.stop(now_unix_ms); break;
                    default:
                        // Scene* library ops: apply on the control thread.
                        scene_ctrl.handleCommand(inc);
                        break;
                }
            }
            cue_engine.tick(now_unix_ms);
        }

        // v0.8 P1.1 — ~1 Hz ambisonic decoder-type apply tick. Cheap (no-op
        // when nothing changed); rebuilds + publishes via lock-free double-
        // buffer when /sys/ambi_decoder_type drove a new type since the
        // last apply.
        if (now - last_ambi_decoder_apply >= std::chrono::seconds(1)) {
            last_ambi_decoder_apply = now;
            engine.applyPendingAmbiDecoderChange();
            engine.applyPendingBinauralSofa(); // B-M3: live SOFA swap on ~1 Hz control tick
        }

        // v0.9 Lane A (A-M1) — ~1 Hz /sys/metrics emit. UNCONDITIONAL (every
        // backend). One ,s key=value message per field — reuses the existing
        // 3-arg sendReply(addr, ",s", kv) overload (OSCBackend.h:120), exactly
        // like ShmTelemetryEmitter::emitState does for /sys/state. NO new
        // encoder. cpu_pct / p99_us come from the engine's single-owner
        // ObservabilityCounters (audio thread stores them each block); peak
        // comes off the CPU meter scalar; xrun_count is the backend device
        // underrun counter; engine_overrun_count is the engine's internal
        // oversized-block counter; binaural_demote_count is the sticky runtime
        // auto-demote flag (0/1 — see follow-up note).
        if (now - last_metrics_emit >= std::chrono::seconds(1)) {
            last_metrics_emit = now;
            auto& obs = engine.observabilityCounters();
            // Shared 6-field emit (review CONCERN-2): identical builder as the
            // e2e test exercises. Loads the scalar atomics / device counter,
            // then formats the ",s" key=value wire messages.
            spe::bin::emitSysMetrics(
                engine.oscBackend(),
                obs.cpu_pct_audio_thread.load(std::memory_order_relaxed),
                engine.cpuMeter().peakPct(),
                obs.per_block_time_p99_us.load(std::memory_order_relaxed),
                static_cast<std::uint64_t>(driver->xrunCount()),
                static_cast<std::uint64_t>(engine.engineOverrunCount()),
                engine.binauralIsRuntimeDemoted() ? 1u : 0u);
        }

        // ADR 0019 PR4 (D4) — shm-gated 1 Hz diagnostics tick. poll_diagnostics
        // is called from production for the first time here (PR3 left it
        // test-only); the emitter then maps the four PR2 warning counters +
        // producer/consumer state onto /sys/warning + /sys/state. shm-gated so
        // the dante/null paths are byte-identical (PM5).
#if defined(__linux__) || defined(__APPLE__)
        if (input_is_shm && (now - last_shm_diag_check >= std::chrono::seconds(1))) {
            last_shm_diag_check = now;
            const std::uint64_t now_steady_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch()).count());
            const std::uint64_t now_unix_ms_u64 =
                static_cast<std::uint64_t>(now_unix_ms);
            shm_backend->poll_diagnostics(now_unix_ms_u64, now_steady_ns);  // D2
            shm_telemetry.tick(*shm_backend, engine.oscBackend(),
                               now_unix_ms_u64);                            // D1+D3
        }
#endif

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

    driver->stop();
    std::printf("  stopped. blocks=%llu xruns=%llu\n",
                static_cast<unsigned long long>(engine.blocksProcessed()),
                static_cast<unsigned long long>(driver->xrunCount()));
    // Sec H3 — audit summary (only meaningful when token auth was enabled).
    if (engine.oscBackend().authEnabled()) {
        std::printf("  osc-auth audit: accepted=%llu rejected=%llu rate_dropped=%llu\n",
                    static_cast<unsigned long long>(engine.oscBackend().authAccepted()),
                    static_cast<unsigned long long>(engine.oscBackend().authRejected()),
                    static_cast<unsigned long long>(engine.oscBackend().rateDropped()));
    }
    return 0;
}

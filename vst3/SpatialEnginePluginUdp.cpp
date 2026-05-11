// vst3/SpatialEnginePluginUdp.cpp
// Per-instance UDP bind + recv thread for the in-plugin OSC path (ADR 0010 A1-ε).
// JUCE-free: no JUCE includes.

#include "SpatialEnginePluginUdp.h"

#include "ipc/CommandDecoder.h"  // spe::ipc::CommandDecoder (from core/src)

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>

namespace spe::vst3 {

SpatialEnginePluginUdp::SpatialEnginePluginUdp(
    std::string_view plugin_comm,
    spe::util::SpscRing<AudioCommand, 1024>* cmd_ring,
    PushParamEditFn push_param_edit)
    : plugin_comm_(plugin_comm)
    , cmd_ring_(cmd_ring)
    , push_param_edit_(std::move(push_param_edit))
{}

SpatialEnginePluginUdp::~SpatialEnginePluginUdp()
{
    stop();
}

bool SpatialEnginePluginUdp::start()
{
    if (running_.load()) return true; // already started

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "[PluginUdp] socket() failed: %s\n", std::strerror(errno));
        return false;
    }

    // 100ms recv timeout for clean shutdown (mirrors OSCBackend pattern).
    struct timeval tv{0, 100000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Walk ports kBasePort .. kBasePort+kPortRange-1, then fall back to ephemeral.
    uint16_t resolved_port = 0;
    for (int i = 0; i <= kPortRange; ++i) {
        uint16_t candidate = (i < kPortRange) ? static_cast<uint16_t>(kBasePort + i) : 0u;

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(candidate);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

        if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            // Determine actual bound port (needed for ephemeral case).
            struct sockaddr_in actual{};
            socklen_t len = sizeof(actual);
            if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&actual), &len) == 0) {
                resolved_port = ntohs(actual.sin_port);
            } else {
                resolved_port = candidate;
            }
            break;
        }

        if (i == kPortRange) {
            // All attempts failed — this should not happen for ephemeral (port=0).
            std::fprintf(stderr, "[PluginUdp] bind() failed for all ports including ephemeral: %s\n",
                         std::strerror(errno));
            ::close(fd);
            return false;
        }

        if (candidate > 0 && errno == EADDRINUSE) {
            if (i == kPortRange - 1) {
                // Exhausted range — next iteration will try ephemeral.
                std::fprintf(stderr, "[PluginUdp] ports %d-%d all EADDRINUSE, falling back to ephemeral\n",
                             kBasePort, kBasePort + kPortRange - 1);
            }
            continue;
        }

        // Unexpected error.
        std::fprintf(stderr, "[PluginUdp] bind(%d) failed: %s\n", candidate, std::strerror(errno));
        ::close(fd);
        return false;
    }

    udp_fd_     = fd;
    bound_port_.store(resolved_port);

    // Register in the instance registry.
    auto entry   = registry_.registerSelf(resolved_port, plugin_comm_);
    instance_id_ = entry.instance_id;

    if (resolved_port >= static_cast<uint16_t>(kBasePort) &&
        resolved_port < static_cast<uint16_t>(kBasePort + kPortRange)) {
        std::fprintf(stderr, "[PluginUdp] bound port=%d instance_id=%u\n",
                     resolved_port, instance_id_);
    } else {
        std::fprintf(stderr, "[PluginUdp] ephemeral port=%d instance_id=%u\n",
                     resolved_port, instance_id_);
    }

    running_.store(true);
    recv_thread_ = std::thread([this]() { recvLoop(); });
    return true;
}

void SpatialEnginePluginUdp::stop()
{
    if (!running_.load()) return;
    running_.store(false);

    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    // Unregister from registry.
    if (instance_id_ > 0) {
        registry_.unregisterSelf(instance_id_);
        instance_id_ = 0;
    }
    bound_port_.store(0);
}

// ---------------------------------------------------------------------------
// S4: ADM-OSC command → controller ParamID/normalizedValue mapping (Option A).
// Maps all 8 params (kMute=7 activated in Phase 4 validation followup).
// Returns false if this command type has no controller-side parameter mapping.
//
// Normalization mirrors SpatialEngineController param table:
//   kPanAz (0):      az_rad in [-pi, pi]  → norm = az_rad/(2*pi) + 0.5
//   kPanEl (1):      el_rad in [-pi/2, pi/2] → norm = el_rad/pi + 0.5
//   kSourceWidth (2): width_rad in [0, pi] → norm = width_rad/pi
//   kMasterGain (3): gain (linear ×1 scale), mapped as: norm = gainPlainToNorm(gain_db)
//                    The payload carries gain as a linear multiplier; we must map via dB.
//                    Approximate: gain_db = 20*log10(max(gain, 1e-10))  then skew.
//   kAmbiOrder (4):  discrete int 1..3 → norm = (order-1)/2.0
//   kRoomPreset (5): discrete int 0..3 → norm = idx/3.0
//   kBypass (6):     boolean → norm = 0.0 or 1.0
//   kMute (7):       boolean → norm = 1.0 (muted) or 0.0 (unmuted)
// ---------------------------------------------------------------------------
static constexpr double kPi_udp = 3.14159265358979323846;

// Gain skew helpers (copy of SpatialEngineController logic; must stay in sync).
static double udp_gainPlainToNorm(double plain) noexcept
{
    constexpr double kGainMin    = -60.0;
    constexpr double kGainMax    =   6.0;
    constexpr double kGainCentre =   0.0;
    static const double kGainSkew = [](){
        double ratio = (kGainCentre - kGainMin) / (kGainMax - kGainMin);
        return std::log(0.5) / std::log(ratio);
    }();
    if (plain <= kGainMin) return 0.0;
    if (plain >= kGainMax) return 1.0;
    return std::pow((plain - kGainMin) / (kGainMax - kGainMin), kGainSkew);
}

static bool audioCommandToParamEdit(
    const AudioCommand& ac,
    uint32_t& out_param_id,
    double&   out_norm) noexcept
{
    using T = spe::ipc::CommandTag;
    switch (ac.tag) {
        case T::ObjMove: {
            // /adm/obj/N/azim or /adm/obj/N/aed → kPanAz + kPanEl.
            // We push az and el as two separate edits; caller handles loop.
            // For now push az only; el pushed separately via the second call with
            // out_param_id == kPanEl sentinel (handled in recvLoop below).
            out_param_id = 0; // kPanAz
            double az = ac.payload.obj_move.az_rad;
            out_norm = az / (2.0 * kPi_udp) + 0.5;
            if (out_norm < 0.0) out_norm = 0.0;
            if (out_norm > 1.0) out_norm = 1.0;
            return true;
        }
        case T::ObjGain: {
            // /adm/obj/N/gain → kMasterGain (id=3).
            // payload.obj_gain.gain is a linear scale factor.
            out_param_id = 3; // kMasterGain
            double gain_linear = ac.payload.obj_gain.gain;
            double gain_db = (gain_linear > 0.f)
                ? 20.0 * std::log10(static_cast<double>(gain_linear))
                : -60.0;
            out_norm = udp_gainPlainToNorm(gain_db);
            return true;
        }
        case T::ObjWidth: {
            // /adm/obj/N/width → kSourceWidth (id=2).
            out_param_id = 2; // kSourceWidth
            double w = ac.payload.obj_width.width_rad;
            out_norm = w / kPi_udp;
            if (out_norm < 0.0) out_norm = 0.0;
            if (out_norm > 1.0) out_norm = 1.0;
            return true;
        }
        case T::SysAmbiOrder: {
            // /sys/ambi_order → kAmbiOrder (id=4). Order in {1,2,3}.
            out_param_id = 4; // kAmbiOrder
            int order = ac.payload.sys_ambi_order.order;
            if (order < 1) order = 1;
            if (order > 3) order = 3;
            out_norm = (order - 1) / 2.0;
            return true;
        }
        case T::ObjMute: {
            // /adm/obj/N/mute → kMute (id=7).
            // payload.obj_mute.muted: true → norm 1.0, false → norm 0.0.
            out_param_id = 7; // kMute
            out_norm = ac.payload.obj_mute.muted ? 1.0 : 0.0;
            return true;
        }
        // All other tags have no kCanAutomate controller parameter mapping.
        default:
            return false;
    }
}

void SpatialEnginePluginUdp::recvLoop()
{
    spe::ipc::CommandDecoder decoder;
    std::array<uint8_t, 65536> buf{};

    while (running_.load()) {
        ssize_t n = ::recv(udp_fd_, buf.data(), buf.size(), 0);
        if (n > 0 && running_.load()) {
            // Decode ADM-OSC packet — may allocate (UDP thread; per ADR 0010 §A4-β
            // the producer side is allowed to allocate; only pop side must be RT-safe).
            auto cmd = decoder.decode(
                std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
            packet_count_.fetch_add(1, std::memory_order_relaxed);

            // Convert to trivially-copyable AudioCommand (shared by audio + reverse paths).
            // fromCommand() returns false for non-POD / audio-irrelevant tags.
            AudioCommand ac;
            bool audio_relevant = fromCommand(cmd, ac);

            // --- Audio path (S3): push to audio-callback ring ---
            if (cmd_ring_) {
                if (audio_relevant) {
                    if (!cmd_ring_->push(ac)) {
                        // Ring full — increment drop counter (mirrors ring's own drops_).
                        drop_count_.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    // Tag not relevant to audio path (ObjName, Scene*, Unknown, etc.)
                    drop_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // --- Reverse path (S4): push to controller marshaling ring ---
            // Strategy (a): UDP thread pushes (paramId, normalizedValue) to
            // the controller's SPSC ring; the message thread drains via
            // notify() → drainParamEdits() → performEdit().
            // This path fires whenever push_param_edit_ is wired (independent of
            // cmd_ring_; both paths operate on the same decoded AudioCommand).
            if (audio_relevant && push_param_edit_) {
                uint32_t param_id = 0;
                double   norm     = 0.0;
                if (audioCommandToParamEdit(ac, param_id, norm)) {
                    if (!push_param_edit_(param_id, norm)) {
                        reverse_drop_count_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                // ObjMove also carries elevation → push kPanEl (id=1) separately.
                if (ac.tag == spe::ipc::CommandTag::ObjMove) {
                    double el = ac.payload.obj_move.el_rad;
                    double el_norm = el / kPi_udp + 0.5;
                    if (el_norm < 0.0) el_norm = 0.0;
                    if (el_norm > 1.0) el_norm = 1.0;
                    if (!push_param_edit_(1 /*kPanEl*/, el_norm)) {
                        reverse_drop_count_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }
    }
}

} // namespace spe::vst3

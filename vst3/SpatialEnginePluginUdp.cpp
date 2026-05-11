// vst3/SpatialEnginePluginUdp.cpp
// Per-instance UDP bind + recv thread for the in-plugin OSC path (ADR 0010 A1-ε).
// JUCE-free: no JUCE includes.

#include "SpatialEnginePluginUdp.h"

#include "ipc/CommandDecoder.h"  // spe::ipc::CommandDecoder (from core/src)

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>

namespace spe::vst3 {

SpatialEnginePluginUdp::SpatialEnginePluginUdp(std::string_view plugin_comm)
    : plugin_comm_(plugin_comm)
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

void SpatialEnginePluginUdp::recvLoop()
{
    spe::ipc::CommandDecoder decoder;
    std::array<uint8_t, 65536> buf{};

    while (running_.load()) {
        ssize_t n = ::recv(udp_fd_, buf.data(), buf.size(), 0);
        if (n > 0 && running_.load()) {
            // S2 stub: decode packet (validates it) and count it.
            // S3 will push the resulting Command into an SPSC ring.
            auto cmd = decoder.decode(std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
            (void)cmd;
            packet_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

} // namespace spe::vst3

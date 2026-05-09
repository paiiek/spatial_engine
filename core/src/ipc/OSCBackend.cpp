// core/src/ipc/OSCBackend.cpp
// JUCE-free stub path active when SPE_HAVE_JUCE is not defined or 0.

#include "ipc/OSCBackend.h"

#if defined(SPE_HAVE_JUCE) && SPE_HAVE_JUCE
// ---- JUCE path (compiled when JUCE submodule is present) -------------------
// Full UDP receive/send via juce::OSCReceiver / juce::OSCSender.
// OSC bytes are decoded on the JUCE message thread, then forwarded to sink_.
// The audio thread never touches this class.

#include <juce_osc/juce_osc.h>

namespace spe::ipc {

void OSCBackend::start() {
    running_ = true;
    // JUCE receiver would be started here when listener port is set.
    // Implementation deferred to v1+ (requires JUCE message loop).
}

void OSCBackend::stop() {
    running_ = false;
}

void OSCBackend::dispatch(Command const& cmd) {
    // Encode to bytes and send via JUCE sender.
    // Deferred to v1+: requires juce::OSCSender::send().
    (void)cmd;
}

void OSCBackend::injectPacket(std::span<const uint8_t> packet) noexcept {
    Command cmd = decoder_.decode(packet);
    if (cmd.tag != CommandTag::Unknown && sink_) {
        sink_(cmd);
    }
}

} // namespace spe::ipc

#else
// ---- JUCE-free stub path ---------------------------------------------------
// POSIX UDP listener when listen_port_ > 0; in-process dispatch for tests.

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <array>

namespace spe::ipc {

void OSCBackend::start() {
    running_ = true;
    if (listen_port_ <= 0) return; // no UDP in test mode

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;

    // 100ms receive timeout for clean shutdown
    struct timeval tv{0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(listen_port_));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return;
    }
    udp_fd_ = fd;

    udp_thread_ = std::thread([this]() {
        std::array<uint8_t, 65536> buf{};
        while (running_) {
            ssize_t n = recv(udp_fd_, buf.data(), buf.size(), 0);
            if (n > 0 && running_) {
                injectPacket(std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
            }
        }
    });
}

void OSCBackend::stop() {
    running_ = false;
    if (udp_fd_ >= 0) {
        close(udp_fd_);
        udp_fd_ = -1;
    }
    if (udp_thread_.joinable()) udp_thread_.join();
}

void OSCBackend::dispatch(Command const& cmd) {
    if (!running_) return;
    // Encode to OSC bytes using the active dialect, then decode back.
    std::vector<uint8_t> buf;
    if (decoder_.encode(cmd, buf, dialect_)) {
        Command decoded = decoder_.decode(std::span<const uint8_t>(buf));
        if (decoded.tag != CommandTag::Unknown && sink_) {
            sink_(decoded);
        }
    } else if (sink_) {
        // For tags that encode() doesn't support in this dialect, forward directly.
        sink_(cmd);
    }
}

void OSCBackend::injectPacket(std::span<const uint8_t> packet) noexcept {
    Command cmd = decoder_.decode(packet);
    if (cmd.tag != CommandTag::Unknown && sink_) {
        sink_(cmd);
    }
}

} // namespace spe::ipc
#endif

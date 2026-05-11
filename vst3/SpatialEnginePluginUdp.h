// vst3/SpatialEnginePluginUdp.h
// Per-instance UDP I/O thread for the VST3 plugin process (ADR 0010 A1-ε).
// Lifecycle: start() on setActive(true), stop() on setActive(false)/terminate().
// JUCE-free: no JUCE includes.
#pragma once

#include "osc/PluginInstanceRegistry.h"
#include "AudioCommand.h"
#include "util/SpscRing.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace spe::vst3 {

class SpatialEnginePluginUdp {
public:
    // plugin_comm: expected DAW process name (passed to registry for GC hints).
    // cmd_ring: audio-path SPSC ring owned by SpatialEngineProcessor; must
    //           outlive this object (processor ensures UDP is stopped before
    //           the ring is destroyed).
    explicit SpatialEnginePluginUdp(
        std::string_view plugin_comm = "spatial_engine_vst3",
        spe::util::SpscRing<AudioCommand, 1024>* cmd_ring = nullptr);
    ~SpatialEnginePluginUdp();

    // Bind UDP socket and start recv thread. Returns true on success.
    // Walks ports 9100+0 .. 9100+15 on EADDRINUSE, then falls back to ephemeral.
    bool start();

    // Signal stop, join recv thread, close socket, unregister from registry.
    void stop();

    // Port that was successfully bound (0 if not started).
    uint16_t boundPort() const noexcept { return bound_port_.load(); }

    // Packet counter: incremented on every received datagram.
    uint64_t packetCount() const noexcept { return packet_count_.load(); }

    // Drop counter: incremented when cmd_ring_ is full or tag is audio-irrelevant.
    uint64_t dropCount() const noexcept { return drop_count_.load(); }

private:
    void recvLoop();

    static constexpr uint16_t kBasePort    = 9100;
    static constexpr int      kPortRange   = 16;   // try 9100..9115

    std::string plugin_comm_;
    std::atomic<bool>     running_{false};
    std::atomic<uint16_t> bound_port_{0};
    std::atomic<uint64_t> packet_count_{0};
    std::atomic<uint64_t> drop_count_{0};
    int                   udp_fd_{-1};
    std::thread           recv_thread_;
    osc::PluginInstanceRegistry registry_;
    uint32_t              instance_id_{0};
    // Non-owning pointer to the audio-path ring (owned by processor).
    spe::util::SpscRing<AudioCommand, 1024>* cmd_ring_{nullptr};
};

} // namespace spe::vst3

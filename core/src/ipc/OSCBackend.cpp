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
// No UDP wire. In-process: dispatch() encodes → decode round-trip → sink_.
// Unit tests exercise the full Command/Decode/StateModel path without sockets.

namespace spe::ipc {

void OSCBackend::start() { running_ = true; }

void OSCBackend::stop()  { running_ = false; }

void OSCBackend::dispatch(Command const& cmd) {
    if (!running_) return;
    // Encode to OSC bytes then decode back — validates codec round-trip.
    std::vector<uint8_t> buf;
    if (decoder_.encode(cmd, buf)) {
        Command decoded = decoder_.decode(std::span<const uint8_t>(buf));
        if (decoded.tag != CommandTag::Unknown && sink_) {
            sink_(decoded);
        }
    } else if (sink_) {
        // For tags that encode() doesn't support, forward directly.
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

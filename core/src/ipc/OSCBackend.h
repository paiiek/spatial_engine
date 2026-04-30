// core/src/ipc/OSCBackend.h
// OSC transport backend. Two compile paths:
//   SPE_HAVE_JUCE=1 : juce::OSCReceiver + juce::OSCSender, SPSC FIFO crossing.
//   SPE_HAVE_JUCE=0 : In-memory dispatch stub (wire I/O deferred to v1+).
// Audio thread never crosses this boundary.

#pragma once
#include "ExternalControl.h"
#include "CommandDecoder.h"
#include <functional>
#include <span>
#include <cstdint>

namespace spe::ipc {

class OSCBackend final : public ExternalControl {
public:
    // Callback invoked on control thread with each decoded Command.
    using CommandSink = std::function<void(const Command&)>;

    explicit OSCBackend(CommandSink sink) : sink_(std::move(sink)) {}
    ~OSCBackend() override { stop(); }

    // ExternalControl interface.
    // dispatch(): encode cmd to OSC bytes and (JUCE path) send via UDP;
    //             (stub path) route directly to sink_ for in-process testing.
    void dispatch(Command const& cmd) override;
    void start() override;
    void stop()  override;

    // Inject raw OSC bytes — for tests or internal loopback.
    void injectPacket(std::span<const uint8_t> packet) noexcept;

    CommandDecoder& decoder() noexcept { return decoder_; }

private:
    CommandSink    sink_;
    CommandDecoder decoder_;
    bool           running_ = false;
};

} // namespace spe::ipc

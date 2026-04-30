// core/src/ipc/ProtocolVersion.h
// Handshake state machine and schema-version mismatch handling.

#pragma once
#include "Command.h"
#include <cstdint>

namespace spe::ipc {

constexpr uint16_t CURRENT_SCHEMA_VERSION = SCHEMA_VERSION; // == 1

enum class HandshakeState : uint8_t {
    Pending = 0,
    Ok      = 1,
    Mismatch= 2,
};

class ProtocolVersion {
public:
    ProtocolVersion() = default;

    // Process an incoming handshake command. Returns a Reply.
    // Sets internal state to Ok or Mismatch.
    Reply processHandshake(const PayloadSysHandshake& hs) noexcept;

    HandshakeState state() const noexcept { return state_; }
    uint16_t       clientVersion() const noexcept { return client_version_; }

    // Build a /sys/error reply for version mismatch.
    static Reply makeMismatchReply(uint16_t client_ver) noexcept;
    // Build a /sys/handshake_ok reply.
    static Reply makeOkReply() noexcept;

    void reset() noexcept { state_ = HandshakeState::Pending; client_version_ = 0; }

private:
    HandshakeState state_          = HandshakeState::Pending;
    uint16_t       client_version_ = 0;
};

} // namespace spe::ipc

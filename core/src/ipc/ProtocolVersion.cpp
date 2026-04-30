// core/src/ipc/ProtocolVersion.cpp

#include "ipc/ProtocolVersion.h"
#include <string>

namespace spe::ipc {

Reply ProtocolVersion::processHandshake(const PayloadSysHandshake& hs) noexcept {
    client_version_ = hs.client_schema_version;
    if (hs.client_schema_version == CURRENT_SCHEMA_VERSION) {
        state_ = HandshakeState::Ok;
        return makeOkReply();
    } else {
        state_ = HandshakeState::Mismatch;
        return makeMismatchReply(hs.client_schema_version);
    }
}

Reply ProtocolVersion::makeMismatchReply(uint16_t client_ver) noexcept {
    Reply r;
    r.tag     = ReplyTag::HandshakeMismatch;
    r.seq     = 0;
    r.message = "schema_version mismatch: client=" + std::to_string(client_ver)
                + " engine=" + std::to_string(CURRENT_SCHEMA_VERSION);
    return r;
}

Reply ProtocolVersion::makeOkReply() noexcept {
    Reply r;
    r.tag     = ReplyTag::HandshakeOk;
    r.seq     = 0;
    r.message = "handshake_ok schema_version=" + std::to_string(CURRENT_SCHEMA_VERSION);
    return r;
}

} // namespace spe::ipc

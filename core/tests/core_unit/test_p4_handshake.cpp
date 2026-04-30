// test_p4_handshake.cpp
// P4: schema_version mismatch → /sys/error reply.
//     matching version → handshake OK.

#include "ipc/ProtocolVersion.h"
#include <cassert>
#include <cstdio>

using namespace spe::ipc;

int main() {
    ProtocolVersion pv;

    // 1. Matching version → HandshakeOk.
    {
        PayloadSysHandshake hs;
        hs.client_schema_version = CURRENT_SCHEMA_VERSION;
        Reply r = pv.processHandshake(hs);
        assert(r.tag == ReplyTag::HandshakeOk);
        assert(pv.state() == HandshakeState::Ok);
        assert(pv.clientVersion() == CURRENT_SCHEMA_VERSION);
    }

    // 2. Mismatched version → HandshakeMismatch reply with /sys/error tag.
    {
        pv.reset();
        PayloadSysHandshake hs;
        hs.client_schema_version = static_cast<uint16_t>(CURRENT_SCHEMA_VERSION + 1);
        Reply r = pv.processHandshake(hs);
        assert(r.tag == ReplyTag::HandshakeMismatch);
        assert(pv.state() == HandshakeState::Mismatch);
        // message must contain "mismatch"
        assert(r.message.find("mismatch") != std::string::npos);
    }

    // 3. Zero version → mismatch (unless SCHEMA_VERSION == 0, which it isn't).
    {
        pv.reset();
        PayloadSysHandshake hs;
        hs.client_schema_version = 0;
        Reply r = pv.processHandshake(hs);
        assert(r.tag == ReplyTag::HandshakeMismatch);
    }

    std::puts("PASS test_p4_handshake");
    return 0;
}

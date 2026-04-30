// test_p4_command_decode.cpp
// P4: round-trip encode→decode for each OSC address pattern.
//     malformed packet → reject counter incremented.

#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace spe::ipc;

static Command roundtrip(CommandDecoder& dec, const Command& cmd) {
    std::vector<uint8_t> buf;
    bool ok = dec.encode(cmd, buf);
    assert(ok);
    (void)ok;
    return dec.decode(std::span<const uint8_t>(buf));
}

int main() {
    CommandDecoder dec;

    // --- ObjMove ---
    {
        Command cmd;
        cmd.tag  = CommandTag::ObjMove;
        cmd.seq  = 10;
        cmd.id   = 1;
        PayloadObjMove p; p.obj_id=0; p.az_rad=1.5f; p.el_rad=0.3f; p.dist_m=2.f;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::ObjMove);
        assert(rt.seq == 10);
        auto& rp = std::get<PayloadObjMove>(rt.payload);
        assert(rp.obj_id == 0);
        assert(rp.az_rad == 1.5f);
        assert(rp.el_rad == 0.3f);
        assert(rp.dist_m == 2.f);
        (void)rp;
    }

    // --- ObjGain ---
    {
        Command cmd;
        cmd.tag = CommandTag::ObjGain; cmd.seq=2; cmd.id=2;
        PayloadObjGain p; p.obj_id=3; p.gain=0.75f;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::ObjGain);
        auto& rp = std::get<PayloadObjGain>(rt.payload);
        assert(rp.obj_id == 3);
        assert(rp.gain == 0.75f);
        (void)rp;
    }

    // --- ObjActive ---
    {
        Command cmd;
        cmd.tag = CommandTag::ObjActive; cmd.seq=3; cmd.id=3;
        PayloadObjActive p; p.obj_id=1; p.active=true;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::ObjActive);
        auto& rp = std::get<PayloadObjActive>(rt.payload);
        assert(rp.obj_id == 1);
        assert(rp.active == true);
        (void)rp;
    }

    // --- ObjAlgo ---
    {
        Command cmd;
        cmd.tag = CommandTag::ObjAlgo; cmd.seq=4; cmd.id=4;
        PayloadObjAlgo p; p.obj_id=2; p.algo=Algorithm::DBAP;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::ObjAlgo);
        auto& rp = std::get<PayloadObjAlgo>(rt.payload);
        assert(rp.algo == Algorithm::DBAP);
        (void)rp;
    }

    // --- SysHandshake ---
    {
        Command cmd;
        cmd.tag = CommandTag::SysHandshake; cmd.seq=5; cmd.id=5;
        PayloadSysHandshake p; p.client_schema_version=1;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::SysHandshake);
        auto& rp = std::get<PayloadSysHandshake>(rt.payload);
        assert(rp.client_schema_version == 1);
        (void)rp;
    }

    // --- SysAlgoSwap ---
    {
        Command cmd;
        cmd.tag = CommandTag::SysAlgoSwap; cmd.seq=6; cmd.id=6;
        PayloadSysAlgoSwap p; p.algo=Algorithm::WFS;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::SysAlgoSwap);
        auto& rp = std::get<PayloadSysAlgoSwap>(rt.payload);
        assert(rp.algo == Algorithm::WFS);
        (void)rp;
    }

    // --- SysReset ---
    {
        Command cmd;
        cmd.tag = CommandTag::SysReset; cmd.seq=7; cmd.id=7;
        cmd.payload = PayloadSysReset{};
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::SysReset);
    }

    // --- HbPing ---
    {
        Command cmd;
        cmd.tag = CommandTag::HbPing; cmd.seq=8; cmd.id=8;
        PayloadHbPing p; p.timestamp_ms = 123456789ULL;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::HbPing);
        auto& rp = std::get<PayloadHbPing>(rt.payload);
        assert(rp.timestamp_ms == 123456789ULL);
        (void)rp;
    }

    // --- HbPong ---
    {
        Command cmd;
        cmd.tag = CommandTag::HbPong; cmd.seq=9; cmd.id=9;
        PayloadHbPong p; p.timestamp_ms = 987654321ULL;
        cmd.payload = p;
        Command rt = roundtrip(dec, cmd);
        assert(rt.tag == CommandTag::HbPong);
        auto& rp = std::get<PayloadHbPong>(rt.payload);
        assert(rp.timestamp_ms == 987654321ULL);
        (void)rp;
    }

    // No rejects so far.
    assert(dec.rejectCount() == 0);

    // --- Malformed packet: too short ---
    {
        uint8_t bad[] = {0x01};
        Command rt = dec.decode(std::span<const uint8_t>(bad, 1));
        assert(rt.tag == CommandTag::Unknown);
        assert(dec.rejectCount() == 1);
    }

    // --- Malformed packet: doesn't start with '/' ---
    {
        uint8_t bad[] = {'X','Y','Z','\0', ',','\0','\0','\0'};
        Command rt = dec.decode(std::span<const uint8_t>(bad, sizeof(bad)));
        assert(rt.tag == CommandTag::Unknown);
        assert(dec.rejectCount() == 2);
    }

    // --- Unknown address: valid OSC format but unrecognized path ---
    {
        uint8_t pkt[] = {
            '/',  'u','n','k', 'n','o','w','n', '/',  'p','a','t', 'h','\0','\0','\0',
            ',', '\0','\0','\0'
        };
        Command rt = dec.decode(std::span<const uint8_t>(pkt, sizeof(pkt)));
        assert(rt.tag == CommandTag::Unknown);
        assert(dec.rejectCount() == 3);
    }

    std::puts("PASS test_p4_command_decode");
    return 0;
}

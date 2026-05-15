// test_osc_sys_load_layout_roundtrip.cpp
// P2: encode Command{SysLoadLayout, path="configs/lab_4ch.yaml"} via
//     CommandDecoder::encode(), decode via CommandDecoder::decode(),
//     assert decoded payload string matches.

#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace spe::ipc;

int main() {
    CommandDecoder dec;

    const std::string kPath = "configs/lab_4ch.yaml";

    Command cmd;
    cmd.tag = CommandTag::SysLoadLayout;
    cmd.seq = 42;
    cmd.id  = 1;
    PayloadSysLoadLayout p;
    p.path = kPath;
    cmd.payload = p;

    std::vector<uint8_t> buf;
    bool ok = dec.encode(cmd, buf);
    assert(ok && "encode failed");

    Command rt = dec.decode(std::span<const uint8_t>(buf));
    assert(rt.tag == CommandTag::SysLoadLayout);
    auto& rp = std::get<PayloadSysLoadLayout>(rt.payload);
    assert(rp.path == kPath);

    assert(dec.rejectCount() == 0);

    std::puts("PASS test_osc_sys_load_layout_roundtrip");
    return 0;
}

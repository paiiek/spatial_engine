// test_osc_sys_binaural_enable_roundtrip.cpp
// P3: encode Command{SysBinauralEnable, enable=0} and enable=1, decode,
//     assert int payload matches.

#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"

#include <cassert>
#include <cstdio>

using namespace spe::ipc;

static void test_enable(CommandDecoder& dec, int value) {
    Command cmd;
    cmd.tag = CommandTag::SysBinauralEnable;
    cmd.seq = static_cast<uint32_t>(value + 1);
    cmd.id  = 1;
    PayloadSysBinauralEnable p;
    p.enable = value;
    cmd.payload = p;

    std::vector<uint8_t> buf;
    bool ok = dec.encode(cmd, buf);
    assert(ok && "encode failed");

    Command rt = dec.decode(std::span<const uint8_t>(buf));
    assert(rt.tag == CommandTag::SysBinauralEnable);
    auto& rp = std::get<PayloadSysBinauralEnable>(rt.payload);
    assert(rp.enable == value);
    (void)rp;
}

int main() {
    CommandDecoder dec;

    test_enable(dec, 0);
    test_enable(dec, 1);

    assert(dec.rejectCount() == 0);

    std::puts("PASS test_osc_sys_binaural_enable_roundtrip");
    return 0;
}

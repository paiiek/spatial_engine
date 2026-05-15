// test_osc_sys_binaural_sofa_roundtrip.cpp
// P3: encode Command{SysBinauralSofa, path="data/hrtf.speh"} via
//     CommandDecoder::encode(), decode, assert path matches.

#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace spe::ipc;

int main() {
    CommandDecoder dec;

    const std::string kPath = "data/hrtf.speh";

    Command cmd;
    cmd.tag = CommandTag::SysBinauralSofa;
    cmd.seq = 7;
    cmd.id  = 1;
    PayloadSysBinauralSofa p;
    p.path = kPath;
    cmd.payload = p;

    std::vector<uint8_t> buf;
    bool ok = dec.encode(cmd, buf);
    assert(ok && "encode failed");

    Command rt = dec.decode(std::span<const uint8_t>(buf));
    assert(rt.tag == CommandTag::SysBinauralSofa);
    auto& rp = std::get<PayloadSysBinauralSofa>(rt.payload);
    assert(rp.path == kPath);

    assert(dec.rejectCount() == 0);

    std::puts("PASS test_osc_sys_binaural_sofa_roundtrip");
    return 0;
}

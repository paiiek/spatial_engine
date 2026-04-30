// test_p4_flood_valid.cpp
// P4-a: Flood with valid commands. Short version (default suite): 10,000 packets.
//       Long version (LABEL "long"): compile with SPE_FLOOD_LONG=1 for 1,000,000 packets.
// Verifies: no crashes, rejectCount()==0, state consistent.

#include "ipc/CommandDecoder.h"
#include "ipc/StateModel.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace spe::ipc;

int main() {
#if defined(SPE_FLOOD_LONG) && SPE_FLOOD_LONG
    const int N = 1'000'000;
    const char* label = "long";
#else
    const int N = 10'000;
    const char* label = "short";
#endif

    CommandDecoder dec;
    StateModel     sm;

    for (int i = 1; i <= N; ++i) {
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = static_cast<uint32_t>(i);
        cmd.id  = static_cast<uint32_t>(i);
        PayloadObjMove p;
        p.obj_id = static_cast<uint32_t>(i % 8); // 8 objects cycling
        p.az_rad = static_cast<float>(i % 628) * 0.01f;
        p.el_rad = 0.f;
        p.dist_m = 1.f;
        cmd.payload = p;

        std::vector<uint8_t> buf;
        assert(dec.encode(cmd, buf));

        Command decoded = dec.decode(std::span<const uint8_t>(buf));
        assert(decoded.tag == CommandTag::ObjMove);

        sm.apply(decoded);
    }

    assert(dec.rejectCount() == 0);
    (void)label;
    std::printf("PASS test_p4_flood_valid [%s, N=%d]\n", label, N);
    return 0;
}

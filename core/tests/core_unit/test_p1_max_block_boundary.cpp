// P1 exit criterion: NullBackend constructed with a block size exceeding
// MAX_BLOCK refuses to start with BlockSizeExceedsMax.

#include "audio_io/NullBackend.h"
#include "core/Constants.h"
#include "core/SpatialEngine.h"

#include <cstdio>

int main() {
    spe::audio_io::NullBackend backend(48000.0, 8, spe::MAX_BLOCK + 1);
    spe::core::SpatialEngine engine;

    auto err = backend.start(&engine);
    if (err != spe::audio_io::BackendError::BlockSizeExceedsMax) {
        std::fprintf(stderr, "FAIL: expected block_size_exceeds_max, got %s\n",
                     spe::audio_io::describe(err));
        return 1;
    }

    spe::audio_io::NullBackend ok_backend(48000.0, 8, spe::MAX_BLOCK);
    if (ok_backend.start(&engine) != spe::audio_io::BackendError::Ok) {
        std::fprintf(stderr, "FAIL: MAX_BLOCK exact size should be accepted\n");
        return 1;
    }
    ok_backend.stop();

    std::printf("p1_max_block_boundary OK\n");
    return 0;
}

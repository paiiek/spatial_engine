// test_vst3_setup_processing_applies_layout.cpp
// P2: after loading state with layoutPath="configs/lab_4ch.yaml",
//     setupProcessing() must apply the layout to the engine.
//     Verify via layout_.speakers.size() == 4 (via spkGainLinSize accessor).

#include "SpatialEngineProcessor.hpp"
#include "core/SpatialEngine.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int32  kMagicV4 = 0x34455053; // 'SPE4' LE
static constexpr uint16 kSecEngineCore    = 0x0001;
static constexpr uint16 kSecLayoutPath    = 0x0002;
static constexpr uint16 kSecSofaPath      = 0x0003;
static constexpr uint16 kSecBinauralState = 0x0004;

// Build a minimal v4 state blob with the given layout path.
static std::vector<uint8_t> makeV4Blob(const std::string& layout_path) {
    std::vector<uint8_t> blob;
    blob.resize(8);
    int32  magic = kMagicV4; std::memcpy(blob.data() + 0, &magic, 4);
    uint16 ver   = 4;        std::memcpy(blob.data() + 4, &ver, 2);
    uint16 sc    = 4;        std::memcpy(blob.data() + 6, &sc, 2);

    auto append = [&](uint16 id, const uint8_t* data, uint32 len) {
        uint8_t hdr[6];
        std::memcpy(hdr + 0, &id, 2);
        std::memcpy(hdr + 2, &len, 4);
        blob.insert(blob.end(), hdr, hdr + 6);
        blob.insert(blob.end(), data, data + len);
    };

    uint8_t core[32]{};
    append(kSecEngineCore, core, 32);
    append(kSecLayoutPath,
           reinterpret_cast<const uint8_t*>(layout_path.data()),
           static_cast<uint32>(layout_path.size()));
    uint8_t empty = 0;
    append(kSecSofaPath, &empty, 0);
    uint8_t bin[2] = {0, 0};
    append(kSecBinauralState, bin, 2);
    return blob;
}

int main(int argc, char** argv) {
    std::string configs_dir;
    if (argc >= 2) configs_dir = std::string(argv[1]) + "/";
    else           configs_dir = std::string(SPE_CONFIGS_DIR) + "/";

    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; std::printf("PASS %s\n", name); }
        else      { ++fail; std::fprintf(stderr, "FAIL %s\n", name); }
    };

    const std::string kPath = configs_dir + "lab_4ch.yaml";

    spe::vst3::SpatialEngineProcessor proc;

    // Load state with layout path set.
    auto blob = makeV4Blob(kPath);
    MemoryStream* ms = new MemoryStream();
    int32 w = 0;
    ms->write(blob.data(), static_cast<int32>(blob.size()), &w);
    int64 z = 0; ms->seek(0, IBStream::kIBSeekSet, &z);
    tresult r = proc.setState(ms);
    ms->release();
    CHECK(r == kResultOk, "setState_ok");
    CHECK(proc.engine().layoutPath() == kPath, "layout_path_set");

    // setupProcessing should apply the layout.
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = 48000.0;
    tresult sr = proc.setupProcessing(setup);
    CHECK(sr == kResultOk, "setupProcessing_ok");

    // After setupProcessing the engine should have 4 speaker gain slots.
    // spkGainLinSize() reflects num_speakers after setLayout().
    CHECK(proc.engine().spkGainLinSize() == 4, "numSpeakers_4_after_setup");

    std::printf("vst3_setup_processing_applies_layout: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

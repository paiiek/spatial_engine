// test_vst3_state_persists_sofa_path.cpp
// P3: state v4 round-trip preserves sofa path + binaural enable flag.
//
// 1. Instantiate processor, set sofaPath + binauralEnabled via engine().
// 2. Save state (getState).
// 3. Instantiate fresh processor, load state (setState).
// 4. Assert sofa path and binaural enable flag are restored.

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

static void setupProc(spe::vst3::SpatialEngineProcessor& proc) {
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = 48000.0;
    proc.setupProcessing(setup);
    proc.setActive(true);
}

static std::vector<uint8_t> dumpState(spe::vst3::SpatialEngineProcessor& proc) {
    MemoryStream* ms = new MemoryStream();
    proc.getState(ms);
    int64 end = 0; ms->seek(0, IBStream::kIBSeekEnd, &end);
    int64 zero = 0; ms->seek(0, IBStream::kIBSeekSet, &zero);
    std::vector<uint8_t> out(static_cast<size_t>(end));
    if (end > 0) {
        int32 nr = 0;
        ms->read(out.data(), static_cast<int32>(end), &nr);
        out.resize(static_cast<size_t>(nr));
    }
    ms->release();
    return out;
}

int main() {
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; std::printf("PASS %s\n", name); }
        else      { ++fail; std::fprintf(stderr, "FAIL %s\n", name); }
    };

    const std::string kSofaPath = "data/hrtf.speh";

    // --- Write side ---
    std::vector<uint8_t> blob;
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        proc.engine().setBinauralSofaPath(kSofaPath);
        proc.engine().setBinauralEnabled(true);
        blob = dumpState(proc);
    }

    // --- Read side ---
    {
        spe::vst3::SpatialEngineProcessor proc2;
        MemoryStream* ms = new MemoryStream();
        int32 w = 0;
        ms->write(blob.data(), static_cast<int32>(blob.size()), &w);
        int64 z = 0; ms->seek(0, IBStream::kIBSeekSet, &z);
        tresult r = proc2.setState(ms);
        ms->release();

        CHECK(r == kResultOk, "sofa_path_setState_ok");
        CHECK(proc2.engine().binauralSofaPath() == kSofaPath, "sofa_path_persisted");
        CHECK(proc2.engine().binauralEnabled() == true, "binaural_enable_persisted");
    }

    std::printf("vst3_state_persists_sofa_path: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

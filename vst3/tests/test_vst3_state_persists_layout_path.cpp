// test_vst3_state_persists_layout_path.cpp
// P2: state v4 round-trip preserves layout path.
//
// 1. Instantiate processor, set layoutPath via engine(), save state (getState).
// 2. Instantiate fresh processor, load state (setState).
// 3. Assert engine().layoutPath() == "configs/lab_4ch.yaml".

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

    const std::string kPath = "configs/lab_4ch.yaml";

    // --- Write side ---
    std::vector<uint8_t> blob;
    {
        spe::vst3::SpatialEngineProcessor proc;
        setupProc(proc);
        proc.engine().setLayoutPath(kPath);
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

        CHECK(r == kResultOk, "layout_path_setState_ok");
        CHECK(proc2.engine().layoutPath() == kPath, "layout_path_persisted");
    }

    std::printf("vst3_state_persists_layout_path: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}

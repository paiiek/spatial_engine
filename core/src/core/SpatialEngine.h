// core/src/core/SpatialEngine.h
#pragma once

#include "audio_io/AudioCallback.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/OSCBackend.h"
#include "ipc/StateModel.h"
#include "render/VBAPRenderer.h"
#include "util/CommandFifo.h"
#include "util/TraceRing.h"
#include "util/XrunCounter.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace spe::core {

class SpatialEngine final : public spe::audio_io::AudioCallback {
public:
    // listen_port: OSC UDP port (0 = no UDP, for tests)
    explicit SpatialEngine(int listen_port = 0);
    ~SpatialEngine() override;

    // Set speaker layout BEFORE start(). Defaults to 8-ch circular if not set.
    void setLayout(spe::geometry::SpeakerLayout layout);

    void audioBlock(const spe::audio_io::AudioBlock& block) override;
    void prepareToPlay(double sample_rate, int max_block_size) override;
    void releaseResources() override;

    bool isPrepared()        const noexcept { return prepared_.load(); }
    std::uint64_t blocksProcessed() const noexcept { return blocks_processed_.load(); }
    const util::TraceRing256& trace() const noexcept { return trace_; }

private:
    // OSC receive → command FIFO → StateModel on audio thread
    util::CommandFifo<1024>  cmd_fifo_;
    ipc::OSCBackend          osc_backend_;
    ipc::StateModel          state_model_;

    // Renderer
    render::VBAPRenderer          vbap_;
    spe::geometry::SpeakerLayout  layout_;
    bool                     has_layout_ = false;

    // Per-object state cache (bypasses StateModel seq-drop for ADM-OSC seq=0)
    struct ObjCache { float az=0.f, el=0.f, dist=1.f; bool active=false; };
    std::array<ObjCache, MAX_OBJECTS> obj_cache_{};

    // Per-object sine oscillator (RT-safe: no alloc)
    std::array<float, MAX_OBJECTS> osc_phases_{};

    // Audio scratch (pre-allocated at prepareToPlay)
    // dry_scratch_[obj][sample]
    std::array<std::array<float, MAX_BLOCK>, MAX_OBJECTS> dry_scratch_{};
    std::array<const float*, MAX_OBJECTS>                 dry_ptrs_{};
    // mix_buf_: interleaved VBAP output [sample * num_speakers + spk]
    std::vector<float>  mix_buf_;

    std::atomic<bool>          prepared_{false};
    std::atomic<bool>          render_ready_{false};
    std::atomic<std::uint64_t> blocks_processed_{0};
    double                     sample_rate_{48000.0};
    int                        max_block_size_{64};
    util::TraceRing256         trace_;
    util::XrunCounter          internal_xruns_;
};

}  // namespace spe::core

// core/src/core/SpatialEngine.h
#pragma once

#include "audio_io/AudioCallback.h"
#include "core/Constants.h"
#include "dsp/PerObjectChain.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "ipc/OSCBackend.h"
#include "ipc/StateModel.h"
#include "output_backend/BinauralMonitor.h"
#include "render/DBAPRenderer.h"
#include "render/VBAPRenderer.h"
#include "render/WFSRenderer.h"
#include "reverb/FdnReverb.h"
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

    // Transport (P-D): play=audible, !play=silent (gain ramped to 0).
    // Safe to call from any thread.
    void setTransportPlay(bool play) noexcept { transport_play_.store(play); }
    bool isTransportPlaying() const noexcept  { return transport_play_.load(); }

private:
    // OSC receive → command FIFO → StateModel on audio thread
    util::CommandFifo<1024>  cmd_fifo_;
    ipc::OSCBackend          osc_backend_;
    ipc::StateModel          state_model_;

    // Renderers (one per algorithm, all run every block but only on objects
    // assigned to that algorithm). Active flag in masked ObjectState gates work.
    render::VBAPRenderer          vbap_;
    render::DBAPRenderer          dbap_;
    render::WFSRenderer           wfs_;

    // FDN reverb (mono send → mono wet) and binaural side-output (mono → L/R).
    reverb::FdnReverb             fdn_;
    output::BinauralMonitor       binaural_;
    bool                          binaural_ok_ = false;

    spe::geometry::SpeakerLayout  layout_;
    bool                     has_layout_ = false;

    // Per-object DSP chain: EQ → user delay → distance gain → HF rolloff → propagation delay → reverb send
    // Heap-allocated (each DelayLine is ~192 KB; 64 chains × 2 lines ≈ 24 MB → far too large for stack).
    std::vector<spe::dsp::PerObjectChain> chains_;

    // Per-object state cache (bypasses StateModel seq-drop for ADM-OSC seq=0)
    struct ObjCache {
        float az = 0.f, el = 0.f, dist = 1.f;
        bool  active = false;
        ipc::Algorithm algo = ipc::Algorithm::VBAP;
        float gain_lin     = 1.f;
        float reverb_send  = 0.f;
        float k_hf         = 0.5f;
        float user_delay_ms = 0.f;
        // 4-band EQ gain (dB) — freq/Q stay at PerObjectChain defaults
        std::array<float, 4> eq_gain_db{0.f, 0.f, 0.f, 0.f};
    };
    std::array<ObjCache, MAX_OBJECTS> obj_cache_{};

    // Per-object sine oscillator (RT-safe: no alloc)
    std::array<float, MAX_OBJECTS> osc_phases_{};

    // Noise generator (per-output-channel array verification)
    struct NoiseChan {
        float    gain_lin   = 0.f;     // 0 = silent (default); set by /noise/{ch}/gain
        bool     pink       = false;
        float    pink_state = 0.f;
        uint32_t rng        = 0xCAFEBABEu;
    };
    std::vector<NoiseChan> noise_chans_;

    // Audio scratch (pre-allocated at prepareToPlay)
    // dry_scratch_[obj][sample]
    std::array<std::array<float, MAX_BLOCK>, MAX_OBJECTS> dry_scratch_{};
    std::array<const float*, MAX_OBJECTS>                 dry_ptrs_{};
    // mix_buf_: interleaved final output [sample * num_speakers + spk]
    // Per-algorithm scratches summed into mix_buf_.
    std::vector<float>  mix_buf_;
    std::vector<float>  vbap_scratch_;
    std::vector<float>  dbap_scratch_;
    std::vector<float>  wfs_scratch_;
    // Mono reverb send & wet buses
    std::vector<float>  reverb_send_buf_;
    std::vector<float>  reverb_wet_buf_;
    // Binaural side-output L/R
    std::vector<float>  binaural_l_buf_;
    std::vector<float>  binaural_r_buf_;

    std::atomic<bool>          prepared_{false};
    std::atomic<bool>          render_ready_{false};
    std::atomic<bool>          transport_play_{true};
    std::atomic<std::uint64_t> blocks_processed_{0};
    double                     sample_rate_{48000.0};
    int                        max_block_size_{64};
    util::TraceRing256         trace_;
    util::XrunCounter          internal_xruns_;
};

}  // namespace spe::core

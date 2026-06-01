// core/src/core/SpatialEngine.h
#pragma once

#include "audio_io/AudioCallback.h"
#include "core/Constants.h"
#include "dsp/ChannelLimiter.h"
#include "dsp/DelayLine.h"
#include "dsp/PerObjectChain.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "ipc/OSCBackend.h"
#include "ipc/StateModel.h"
#include "hrtf/HrtfCatalog.h"
#include "output_backend/BinauralMonitor.h"
#include "render/AmbisonicRenderer.h"
#include "render/DBAPRenderer.h"
#include "render/VBAPRenderer.h"
#include "render/WFSRenderer.h"
#include "reverb/FdnReverb.h"
#include "reverb/ReverbEngine.h"
#include "sync/LtcChase.h"
#include "util/CommandFifo.h"
#include "util/CpuMeter.h"
#include "util/ObservabilityCounters.h"
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

    // ─────────────────────────────────────────────────────────────────────
    // v0.4 — runtime path injection (control-thread only).
    // Setters store the path/flag; the audio path consumes them lazily at
    // the next prepareToPlay() boundary. None of these touch RT state, so
    // they are safe to call from the OSC handler dispatch.
    // ─────────────────────────────────────────────────────────────────────
    void setLayoutPath(const std::string& path)        { layout_path_ = path; }
    void setBinauralSofaPath(const std::string& path)  { binaural_sofa_path_ = path; }
    void setBinauralEnabled(bool on)                   { binaural_enabled_.store(on); }

    // v0.5 P4: binaural mode injection. Same control-thread contract as the
    // other setters above. Forwards to BinauralMonitor::setRequestedMode().
    // 0 = B1 Direct (default), 1 = B2 AmbiVS.
    void setBinauralMode(int mode) {
        binaural_.setRequestedMode(mode == 1 ? output::BinauralMode::AmbiVS
                                              : output::BinauralMode::Direct);
    }
    int binauralMode() const noexcept {
        return static_cast<int>(binaural_.requestedMode());
    }
    int effectiveBinauralMode() const noexcept {
        return static_cast<int>(binaural_.effectiveMode());
    }
    // Probe accessors — surfaced for VST3 status emission & state v4.
    float binauralProbeThroughput() const noexcept {
        return binaural_.probeThroughput();
    }
    const char* binauralProbeWarningCode() const noexcept {
        return binaural_.probeWarningCode();
    }
    // v0.5 P4.1 (A6): host-lifecycle probe trigger. Call this from
    // VST3 SpatialEngineProcessor::setActive(true) — control thread only.
    // Runs the synthetic 24-fan-out throughput bench; on insufficient
    // CPU headroom (<1.5x RT) clamps effectiveBinauralMode() to Direct
    // and surfaces binauralProbeWarningCode() == "ambivs_disabled_cpu".
    // v0.5.1 Q1 (A2 resolution): after the clamp, emit
    //   /sys/binaural_warning ,sf "ambivs_disabled_cpu" <throughput>
    // through the OSC outbound reply channel. Drops silently if no client
    // has talked to us yet (no captured peer endpoint).
    float triggerBinauralProbe();

    // v0.5.1 Q1 — engine-level forwarder for binaural-bus telemetry. Lets
    // the VST3 heartbeat tick read the failure counter without taking a
    // direct dependency on BinauralMonitor.
    std::uint64_t loadIntoFailuresCount() const noexcept {
        return binaural_.loadIntoFailures();
    }

    // v0.5.1 Q2 — engine-level forwarder for the mode-transition crossfade
    // truncation telemetry. Audio thread sets the flag in BinauralMonitor
    // when a ramp is armed with kXfadeBlocks=1 (probe-clamped CPU
    // truncation); the IO-thread heartbeat (1 Hz) drains the flag via
    // this forwarder and emits /sys/binaural_warning ,s "xfade_truncated_cpu".
    bool binauralDrainXfadeTruncatedPending() noexcept {
        return binaural_.drainXfadeTruncatedPending();
    }

    // v0.5.1 Q2 — engine-level forwarder. Test/heartbeat-only visibility into
    // whether a B1↔B2 ramp is currently in flight on the audio thread.
    bool binauralXfadeActive() const noexcept {
        return binaural_.xfadeActive();
    }

    // v0.6 #5 — engine-level forwarder for the runtime sticky-underrun
    // auto-demote telemetry. Audio thread sets the flag in BinauralMonitor
    // when B2's wall-clock cost exceeds the budget for kRuntimeDemoteStrikes
    // consecutive blocks. The IO-thread heartbeat drains the flag and emits
    // /sys/binaural_warning ,s "ambivs_demoted_runtime" exactly once per
    // demote event (sticky for the BinauralMonitor's lifetime).
    bool binauralDrainRuntimeDemotePending() noexcept {
        return binaural_.drainRuntimeDemotePending();
    }

    // v0.6 #5 — true once the runtime auto-demote has fired. Sticky until
    // a fresh prepareToPlay() re-initialises BinauralMonitor. Used by tests
    // and (optionally) by /sys/state snapshots.
    bool binauralIsRuntimeDemoted() const noexcept {
        return binaural_.isRuntimeDemoted();
    }

    // v0.6 #5 — test-only hooks for deterministic auto-demote scenarios.
    void injectBinauralRuntimeUnderrunStrikesForTest() noexcept {
        binaural_.injectRuntimeUnderrunStrikesForTest();
    }
    void clearBinauralRuntimeDemoteForTest() noexcept {
        binaural_.clearRuntimeDemoteForTest();
    }
    void recordBinauralB2BlockTimingForTest(int block_size,
                                            float sample_rate,
                                            long long elapsed_ns) noexcept {
        binaural_.recordB2BlockTiming(block_size, sample_rate, elapsed_ns);
    }

    // v0.7 D-S1 — forwarder for the user-controlled runtime demote reset hatch.
    // Called from the OSC IO thread after decoding /sys/binaural_reset_demote ,i 1.
    // now_ns: steady_clock nanoseconds from the calling context.
    output::BinauralMonitor::ResetResult
    resetBinauralRuntimeDemoteFromUser(int64_t now_ns) noexcept {
        return binaural_.resetRuntimeDemoteFromUser(now_ns);
    }

    // v0.7 D-S1 — IO-thread drain latches for new warning strings.
    bool binauralDrainResetDemoteAcceptedPending() noexcept {
        return binaural_.drainResetDemoteAcceptedPending();
    }
    bool binauralDrainResetDemoteCooldownPending() noexcept {
        return binaural_.drainResetDemoteCooldownPending();
    }

    // v0.7 D-S3 — demote-moment snapshot forwarders for the heartbeat drain
    // /sys/binaural_diag ,iif emission. Read by IO thread after
    // binauralDrainRuntimeDemotePending() returns true.
    int binauralSnapshotMaxRatioX1000() const noexcept {
        return binaural_.snapshotRuntimeDemoteMaxRatioX1000();
    }
    int binauralSnapshotBlockSizeAtEvent() const noexcept {
        return binaural_.snapshotRuntimeDemoteBlockSizeAtEvent();
    }
    int binauralSnapshotSampleRateAtEvent() const noexcept {
        return binaural_.snapshotRuntimeDemoteSampleRateAtEvent();
    }

    // v0.6 D-M2 — engine-level forwarders for the steady_clock vDSO
    // probe results. Audio thread reads isSteadyClockFast() through the
    // forwarder to gate the wall-clock brackets in audioBlock(); IO
    // thread heartbeat drains drainRtTimingUnavailablePending() to emit
    // /sys/binaural_warning ,s "rt_timing_unavailable" exactly once
    // per BinauralMonitor lifetime when the probe finds the platform
    // slow.
    bool binauralIsSteadyClockFast() const noexcept {
        return binaural_.isSteadyClockFast();
    }
    bool binauralDrainRtTimingUnavailablePending() noexcept {
        return binaural_.drainRtTimingUnavailablePending();
    }

    // Test-only forwarder — drives the slow path for deterministic
    // verification on fast CI runners.
    void injectBinauralSteadyClockSlowForTest() noexcept {
        binaural_.injectSteadyClockSlowForTest();
    }

    // v0.5.1 Q1 — test-only hook: inject a synthetic probe throughput and
    // emit the matching /sys/binaural_warning if the injected value forces
    // a B2→B1 fallback. Used exclusively by the soak harness CLI flag
    // `--inject-probe-throughput`. NOT for production callers.
    void injectProbeThroughputAndEmit(float throughput_rt);

    const std::string& layoutPath()         const noexcept { return layout_path_; }
    const std::string& binauralSofaPath()   const noexcept { return binaural_sofa_path_; }
    bool               binauralEnabled()    const noexcept { return binaural_enabled_.load(); }

    void audioBlock(const spe::audio_io::AudioBlock& block) override;
    void prepareToPlay(double sample_rate, int max_block_size) override;
    void releaseResources() override;

    bool isPrepared()        const noexcept { return prepared_.load(); }
    std::uint64_t blocksProcessed() const noexcept { return blocks_processed_.load(); }
    const util::TraceRing256& trace() const noexcept { return trace_; }

    // v0.9 Lane A (A-M1) — engine-internal overrun count (audioBlock saw a
    // block with num_frames > MAX_BLOCK and refused it). Distinct from the
    // backend device xrun count (driver->xrunCount()). Emitted on
    // /sys/metrics as engine_overrun_count.
    std::uint64_t engineOverrunCount() const noexcept { return internal_xruns_.overruns(); }

    // Transport (P-D): play=audible, !play=silent (gain ramped to 0).
    // Safe to call from any thread.
    void setTransportPlay(bool play) noexcept { transport_play_.store(play); }
    bool isTransportPlaying() const noexcept  { return transport_play_.load(); }

    // ─────────────────────────────────────────────────────────────────────
    // ADR 0018 D-5 — external-player heartbeat liveness.
    //
    // The engine does NOT run HeartbeatMonitor on inbound /hb/ping from the
    // player (the engine is the audio source-of-truth; the player's death
    // just means OSC stops and the engine keeps rendering). Instead we keep a
    // single low-priority "last seen" wall-clock timestamp, ticked from the
    // control-thread drain when a /hb/ping arrives from the EXTERNAL player
    // (carries `,d`; PayloadHbPing::from_external == true). The engine's own
    // 10 Hz publisher (`,h`) does NOT tick this.
    //
    // checkPlayerHeartbeatStale() runs on the control / IO thread (NEVER the
    // audio thread): when the last external ping is older than 5 s it emits
    // /sys/warning ,iis 0 0 "player_heartbeat_stale" "<seconds>" at most once
    // per 30 s, piggybacking on the existing outbound warning channel (ADR
    // 0017 telemetry rules). The latch clears on the next external ping.
    // ─────────────────────────────────────────────────────────────────────
    static constexpr int64_t kPlayerHeartbeatStaleMs    = 5000;   // 5× the 1 Hz period
    static constexpr int64_t kPlayerStaleWarnIntervalMs = 30000;  // ≤ once per 30 s

    int64_t lastPlayerPingUnixMs() const noexcept {
        return last_player_ping_unix_ms_.load(std::memory_order_relaxed);
    }

    // Control/IO-thread tick: evaluate staleness against `now_unix_ms` and emit
    // the warning at most once per 30 s window. now_unix_ms is supplied by the
    // caller so tests can drive a deterministic mocked clock. Returns true iff
    // a warning was emitted on this call. No-op when no external ping has been
    // seen yet (last_player_ping_unix_ms_ == 0).
    bool checkPlayerHeartbeatStale(int64_t now_unix_ms) noexcept;

    // Test-only: deterministically record an external player ping at a given
    // wall-clock and clear the staleness latch (mirrors the control-thread
    // HbPing-from-external path without needing a UDP round-trip).
    void recordPlayerPingForTest(int64_t unix_ms) noexcept {
        last_player_ping_unix_ms_.store(unix_ms, std::memory_order_relaxed);
        player_stale_latched_.store(false, std::memory_order_relaxed);
    }

    // VST3 layer host→core control plane (Phase C C2 §15.A).
    // Forwards in-process commands to the OSC backend's sink, bypassing
    // OSC encode/decode. Lock-free (cmd_fifo_ enqueue inside sink_).
    inline void dispatchCommand(spe::ipc::Command const& cmd) noexcept {
        osc_backend_.injectCommand(cmd);
    }

    // Expose OSCBackend for dialect configuration (--osc-dialect CLI flag).
    ipc::OSCBackend& oscBackend() noexcept { return osc_backend_; }

    // v0.9 Lane A (A-M1) — single-owner ObservabilityCounters instance. The
    // audio thread stores cpu_pct/p99 here each block (relaxed scalar atomics);
    // the control-thread 1 Hz tick loads them for /sys/metrics. NET-NEW: the
    // struct was previously dead-code (instantiated nowhere). Mirrors the
    // oscBackend() accessor above.
    util::ObservabilityCounters& observabilityCounters() noexcept { return obs_counters_; }

    // v0.9 Lane A (A-M1) — read-only access to the audio-thread CPU meter so
    // the control thread can load the scalar peak% (single relaxed atomic).
    const util::CpuMeter& cpuMeter() const noexcept { return cpu_meter_; }

    // C1.d — LTC chase from input ch 0. When enabled, audioBlock() taps
    // input_channels[0] (if present) and pushes the samples into the
    // internal LtcChase ring. updateLtcChase() drains the ring on the
    // control thread and runs the M7 biphase decoder.
    void setLtcChaseEnable(bool enable) noexcept { ltc_chase_enable_.store(enable); }
    bool isLtcChaseEnabled() const noexcept       { return ltc_chase_enable_.load(); }

    // Control-thread tick: drain LTC ring + decode. Lock-free, fast; safe
    // to call from any non-RT context (e.g. between audio blocks in tests
    // or on a dedicated control timer in production).
    void updateLtcChase() noexcept                { ltc_chase_.update(); }

    // v0.8 P1.1 (DSP-1 / M2HOA-Q14) — control-thread forwarder for the
    // ambisonic decoder-type runtime apply. The audio-thread FIFO drain
    // stores the new type via ambisonic_.setDecoderType() (atomic), but
    // the actual matrix rebuild (which ALLOCATES) must happen here on the
    // control thread. The rebuild publishes the new matrices via the
    // AmbiDecoder lock-free double-buffer (see core/src/ambi/AmbiDecoder.h
    // BINDING INVARIANT). MUST be called from a non-RT context — e.g. the
    // ~1 Hz control tick in core/src/bin/spatial_engine_core.cpp. Calling
    // it faster than ~one-per-audio-block would violate the 2-slot
    // timing-slack invariant; the production cadence (1 Hz) is ~93 audio
    // blocks of margin at 512/48k.
    void applyPendingAmbiDecoderChange() {
        ambisonic_.applyPendingDecoderTypeChange();
    }

    // B-M3 — ~1 Hz control-tick: if a /sys/binaural_sofa_select has set a
    // pending path, perform the actual load + swap off the audio thread.
    // Mirrors applyPendingAmbiDecoderChange() in cadence and threading contract.
    void applyPendingBinauralSofa();

    bool getLtcCurrentTimecode(spe::sync::Timecode& out) const noexcept {
        return ltc_chase_.getCurrentTimecode(out);
    }

    // ─────────────────────────────────────────────────────────────────────
    // v0.3.1 test introspection — per-speaker state by vector position.
    // Used by OSC channel-routing tests to verify that wire channel N lands
    // in the vector position whose YAML channel field is N. Not RT-safe;
    // callers must avoid concurrent FIFO drains. Returns sentinel on OOB.
    // ─────────────────────────────────────────────────────────────────────
    float spkGainLinAt(size_t idx) const noexcept {
        return (idx < spk_gain_lin_.size()) ? spk_gain_lin_[idx] : -1.f;
    }
    float spkLimiterThresholdAt(size_t idx) const noexcept {
        return (idx < spk_limiters_.size()) ? spk_limiters_[idx].getThreshold() : -1.f;
    }
    float noiseGainLinAt(size_t idx) const noexcept {
        return (idx < noise_chans_.size()) ? noise_chans_[idx].gain_lin : -1.f;
    }
    bool noisePinkAt(size_t idx) const noexcept {
        return (idx < noise_chans_.size()) ? noise_chans_[idx].pink : false;
    }
    size_t spkGainLinSize()    const noexcept { return spk_gain_lin_.size(); }
    size_t spkLimitersSize()   const noexcept { return spk_limiters_.size(); }
    size_t noiseChansSize()    const noexcept { return noise_chans_.size(); }
    // v0.9 Lane C (D2 test introspection) — per-object cache active flag by
    // object id. Used to prove objects up to MAX_OBJECTS-1 are populated by the
    // FIFO drain at the configured cap. Not RT-safe; returns false on OOB.
    bool objCacheActiveAt(size_t obj_id) const noexcept {
        return (obj_id < obj_cache_.size()) ? obj_cache_[obj_id].active : false;
    }
    size_t objCacheSize() const noexcept { return obj_cache_.size(); }
    std::uint64_t ltcFramesDecoded() const noexcept { return ltc_chase_.framesDecoded(); }
    std::uint64_t ltcRingDrops()     const noexcept { return ltc_chase_.ringDrops(); }
    bool          ltcLocked()        const noexcept { return ltc_chase_.isLocked(); }

    // v0.5 P3: binaural bus 1 readout. Returns pointers to the most recent
    // block's binaural L/R outputs. Valid until the next audioBlock() call.
    // Returns nullptr if no .speh is loaded (caller should fall back to its
    // own placeholder). Length matches the last audioBlock's num_frames.
    const float* binauralL() const noexcept {
        return binaural_ok_ && binaural_.hasHrtf() ? binaural_l_buf_.data() : nullptr;
    }
    const float* binauralR() const noexcept {
        return binaural_ok_ && binaural_.hasHrtf() ? binaural_r_buf_.data() : nullptr;
    }
    // v0.5.1 Q1 — true iff a .speh has loaded HRTF data. Used by VST3
    // process() to decide whether to emit the "no_sofa_loaded" warning.
    bool binauralHasHrtf() const noexcept {
        return binaural_ok_ && binaural_.hasHrtf();
    }

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
    render::AmbisonicRenderer     ambisonic_;

    // FDN reverb (mono send → mono wet) and binaural side-output (mono → L/R).
    reverb::FdnReverb                        fdn_;
    std::unique_ptr<reverb::IReverbEngine>   ir_reverb_;
    std::atomic<int>                         active_reverb_{0}; // 0=FDN, 1=IR
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
        float width_rad = 0.f;  // source spread in radians (0 = point source)
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
    // v0.9 Lane C (hoist): per-block per-algorithm object-state scratch. Moved
    // off the audio-callback stack to engine members so the audio thread never
    // carries ~10 KB of stack arrays at MAX_OBJECTS=128. Fixed-size, alloc-once,
    // rewritten every block in audioBlock — behaviour-identical to the prior
    // stack locals.
    std::array<render::ObjectState, MAX_OBJECTS> vbap_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> dbap_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> wfs_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> ambisonic_objs_{};
    // mix_buf_: interleaved final output [sample * num_speakers + spk]
    // Per-algorithm scratches summed into mix_buf_.
    std::vector<float>  mix_buf_;
    std::vector<float>  vbap_scratch_;
    std::vector<float>  dbap_scratch_;
    std::vector<float>  wfs_scratch_;
    std::vector<float>  ambisonic_scratch_;
    // Per-speaker time-alignment (delay + gain)
    std::vector<spe::dsp::DelayLine>       spk_delays_;
    std::vector<float>                     spk_gain_lin_;
    std::vector<float>                     spk_delay_samples_;
    std::vector<spe::dsp::ChannelLimiter>  spk_limiters_;

    // Mono reverb send & wet buses
    std::vector<float>  reverb_send_buf_;
    std::vector<float>  reverb_wet_buf_;
    // Binaural side-output L/R
    std::vector<float>  binaural_l_buf_;
    std::vector<float>  binaural_r_buf_;
    // v0.5 P3: per-object temporary buffers for B1 per-object HRTF sum.
    std::vector<float>  bin_tmp_L_;
    std::vector<float>  bin_tmp_R_;
    // v0.5 P3: limiters on the binaural bus output to prevent clipping under
    // heavy multi-object summation.
    spe::dsp::ChannelLimiter binaural_lim_L_;
    spe::dsp::ChannelLimiter binaural_lim_R_;

    // v0.5 P4: 3rd-order SH (16 ACN channels) accumulator for B2 AmbiVS.
    // b2_sh_scratch_[k][n] = sum over active objects of (sh_coeffs[k] * dry[n] * gain).
    // b2_sh_ptrs_ point into b2_sh_scratch_ and are passed to processBlockB2().
    std::array<std::array<float, MAX_BLOCK>, 16> b2_sh_scratch_{};
    std::array<const float*, 16>                 b2_sh_ptrs_{};

    // v0.5.1 Q2 (A3) — outgoing / incoming branch scratch buffers for the
    // B1↔B2 mode-transition crossfade. Pre-allocated MAX_BLOCK each (no heap
    // touch on the audio thread). Used ONLY when BinauralMonitor's xfade is
    // active: the engine renders both branches into these, then envelope-mixes
    // into binaural_l_buf_ / binaural_r_buf_. In steady state these stay
    // untouched.
    std::array<float, MAX_BLOCK> bin_xfade_out_L_{};
    std::array<float, MAX_BLOCK> bin_xfade_out_R_{};
    std::array<float, MAX_BLOCK> bin_xfade_in_L_{};
    std::array<float, MAX_BLOCK> bin_xfade_in_R_{};

    std::atomic<bool>          prepared_{false};
    std::atomic<bool>          render_ready_{false};
    std::atomic<bool>          transport_play_{true};
    std::atomic<bool>          ltc_chase_enable_{false};

    // ADR 0018 D-5 — external-player heartbeat liveness state. All accessed
    // from the control / IO thread only (plus a relaxed read for /sys/state).
    // last_player_ping_unix_ms_: 0 = no external ping seen yet.
    // last_stale_warning_unix_ms_: window-start of the most recent emission.
    // player_stale_latched_: true while inside a stale window (cleared on the
    //   next external ping so a resume re-arms the warning).
    std::atomic<int64_t>       last_player_ping_unix_ms_{0};
    int64_t                    last_stale_warning_unix_ms_{0};
    std::atomic<bool>          player_stale_latched_{false};
    // v0.4 — control-thread storage for runtime path injection. Audio path
    // does NOT read these directly; consumed by prepareToPlay() / VST3
    // setupProcessing() at block boundaries.
    std::string                layout_path_;
    std::string                binaural_sofa_path_;
    std::atomic<bool>          binaural_enabled_{false};
    // B-M3 — HRTF catalog (loaded at startup / prepareToPlay, control-thread
    // only). Used by the SysBinauralSofaSelect OSC handler to resolve a catalog
    // name → speh_path before setting the pending flag below.
    hrtf::HrtfCatalog          hrtf_catalog_;
    // B-M3 — pending SOFA swap from /sys/binaural_sofa_select. The OSC control
    // thread stores name + resolved path here; applyPendingBinauralSofa() on
    // the ~1 Hz tick consumes them. The flag uses relaxed stores/loads — both
    // accesses are on the control thread (OSC callback + control tick), never
    // the audio thread.
    std::string                pending_binaural_sofa_path_;
    std::atomic<bool>          pending_binaural_sofa_flag_{false};
    spe::sync::LtcChase        ltc_chase_;
    std::atomic<std::uint64_t> blocks_processed_{0};
    double                     sample_rate_{48000.0};
    int                        max_block_size_{64};
    util::TraceRing256         trace_;
    util::XrunCounter          internal_xruns_;
    // v0.9 Lane A (A-M1) — NET-NEW. Audio thread owns cpu_meter_ measurement
    // and stores scalar results into obs_counters_ (single relaxed atomics).
    util::CpuMeter             cpu_meter_;
    util::ObservabilityCounters obs_counters_;
};

}  // namespace spe::core

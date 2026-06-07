// core/src/core/SpatialEngine.h
#pragma once

#include "audio_io/AudioCallback.h"
#include "coords/Coords.h"
#include "core/Constants.h"
#include "dsp/ChannelLimiter.h"
#include "dsp/DelayLine.h"
#include "dsp/PerObjectChain.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "ipc/OSCBackend.h"
#include "ipc/SceneSnapshot.h"
#include "ipc/StateModel.h"
#include "hrtf/HrtfCatalog.h"
#include "output_backend/BinauralMonitor.h"
#include "render/AmbisonicRenderer.h"
#include "render/DBAPRenderer.h"
#include "render/VAPRenderer.h"
#include "render/VBAPRenderer.h"
#include "render/WFSRenderer.h"
#include "reverb/FdnReverb.h"
#include "reverb/ReverbEngine.h"
#include "render/ported/RoomFdn.h"
#include "render/ported/RoomEarly.h"
#include "render/ported/RoomBiquad.h"
#include "render/ported/RoomCluster.h"
#include "render/ported/RoomDistanceGain.h"
#include "render/ported/SpeakerDecorrelation.h"
#include "sync/LtcChase.h"
#include "util/CommandFifo.h"
#include "util/CpuMeter.h"
#include "util/ObservabilityCounters.h"
#include "util/TraceRing.h"
#include "util/XrunCounter.h"

#include <array>
#include <algorithm>
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
    // Phase 2.6b — set the binaural head pose (degrees). Control-thread API
    // mirroring the /ypr OSC path; used by tests and host integrations.
    void setHeadYpr(float yaw_deg, float pitch_deg, float roll_deg) {
        head_yaw_deg_.store(yaw_deg,   std::memory_order_relaxed);
        head_pitch_deg_.store(pitch_deg, std::memory_order_relaxed);
        head_roll_deg_.store(roll_deg, std::memory_order_relaxed);
    }
    // Phase 2.5 — binaural monitor EQ master enable. Atomic store, so this is
    // safe from the control thread (mirrors the /sys/binaural_eq/enable path).
    void setBinauralEqEnabled(bool on) {
        binaural_eq_active_.store(on, std::memory_order_relaxed);
    }
    // Phase 2.1 — binaural HRTF prefeed LP corner (Hz). Atomic store (control
    // thread); the audio path reads it once per block. Mirrors the
    // /sys/binaural_prefeed path. A corner above Nyquist is an effective bypass.
    void setBinauralPrefeedCutoff(float cutoff_hz) {
        bin_prefeed_cutoff_hz_.store(cutoff_hz, std::memory_order_relaxed);
    }
    float binauralPrefeedCutoffForTest() const noexcept {
        return bin_prefeed_cutoff_hz_.load(std::memory_order_relaxed);
    }
    // Phase 2.4 — binaural monitor stereo delay tap (ms). Atomic store (control
    // thread); the audio path reads it once per block. Mirrors /sys/binaural_delay.
    void setBinauralDelayMs(float ms) {
        bin_delay_ms_.store(ms, std::memory_order_relaxed);
    }
    float binauralDelayMsForTest() const noexcept {
        return bin_delay_ms_.load(std::memory_order_relaxed);
    }

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
    // Phase 2.6b — current binaural head pose (degrees), relaxed reads.
    float headYawDeg()   const noexcept { return head_yaw_deg_.load(std::memory_order_relaxed); }
    float headPitchDeg() const noexcept { return head_pitch_deg_.load(std::memory_order_relaxed); }
    float headRollDeg()  const noexcept { return head_roll_deg_.load(std::memory_order_relaxed); }

    void audioBlock(const spe::audio_io::AudioBlock& block) override;
    void prepareToPlay(double sample_rate, int max_block_size) override;
    void releaseResources() override;

    bool isPrepared()        const noexcept { return prepared_.load(); }
    std::uint64_t blocksProcessed() const noexcept { return blocks_processed_.load(); }
    const util::TraceRing256& trace() const noexcept { return trace_; }

    // ⑥e (late opp source-bias) — unit direction of late FDN line `k` (0..7),
    // steered from its static cube corner {±1,±1,±1}/√3 toward `opp` by
    // kLateCornerTowardOpposite=0.5 then renormalized. Byte-faithful to
    // RoomEngine.cpp:567-572. Pure math (no engine state) — exposed for unit
    // tests; computeLateFdnGains() applies the VBAP/diffuse on top of this.
    static iae::Vec3 lateFdnLineDirection(int k, const iae::Vec3& opp) noexcept;

    // ⑥e-2 — per-object cluster send amount [0,1] (scaled by 0.48 internally,
    // RoomEngine.cpp:415). RT-safe to call from the control thread; the audio
    // thread reads it once per block. ⑥e-4 OSC will route an address here.
    void setRoomClusterSend01(float v) noexcept {
        room_cluster_send01_.store(std::clamp(v, 0.f, 1.f), std::memory_order_relaxed);
    }

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

    // ⑥e-4 test-only — room absorption-EQ coefficient introspection. Used to
    // prove /room/eq/early keeps the cluster-bus EQ and EVERY per-object early EQ
    // coefficient-locked (the reference lockstep contract). RoomBiquad exposes
    // b0()..a2(); these just hand out the live filters. Not RT-safe; control
    // thread / test only.
    const iae::RoomBiquad& clusterEqHpForTest() const noexcept { return cluster_eq_hp_; }
    const iae::RoomBiquad& clusterEqLpForTest() const noexcept { return cluster_eq_lp_; }
    const iae::RoomBiquad& earlyEqHpForTest(size_t i) const noexcept {
        return er_eq_hp_[i];
    }
    const iae::RoomBiquad& earlyEqLpForTest(size_t i) const noexcept {
        return er_eq_lp_[i];
    }
    // ⑥e-4-A — late-bus EQ introspection (/room/eq/late). Separate bus from the
    // early/cluster EQ; its own corners. Not RT-safe; test only.
    const iae::RoomBiquad& lateEqHpForTest() const noexcept { return late_eq_hp_; }
    const iae::RoomBiquad& lateEqLpForTest() const noexcept { return late_eq_lp_; }
    // Phase 2.5 — binaural monitor EQ introspection (test only, not RT-safe).
    bool binauralEqActiveForTest() const noexcept {
        return binaural_eq_active_.load(std::memory_order_relaxed);
    }
    const iae::RoomBiquad& binauralEqLForTest(size_t b) const noexcept { return bin_eq_L_[b]; }
    const iae::RoomBiquad& binauralEqRForTest(size_t b) const noexcept { return bin_eq_R_[b]; }
    float binauralEqGainDbForTest(size_t b) const noexcept { return bin_eq_gain_db_[b]; }
    // ⑦ test-only — decorrelation param introspection (not RT-safe).
    bool          decorrEnabledForTest() const noexcept { return decorr_enabled_; }
    float         decorrMixForTest()     const noexcept { return decorr_mix01_; }
    float         decorrSpreadForTest()  const noexcept { return decorr_spread_ms_; }
    float         decorrApForTest()      const noexcept { return decorr_ap_; }
    int           decorrStagesForTest()  const noexcept { return decorr_stages_; }
    std::uint32_t decorrSeedForTest()    const noexcept { return decorr_seed_; }

    // F4b: consistent control-thread snapshot of authoritative object state into
    // scene ObjectSnapshots. Synchronized via the three-buffer published_index_
    // handshake; safe to call from the control loop concurrently with the RT
    // audioBlock. Emits objects driven at least once (touched heuristic). NOT RT.
    // Distinct from objCacheActiveAt (test-only / "Not RT-safe") — this is its
    // own synchronized path.
    void snapshotObjects(std::vector<ipc::ObjectSnapshot>& out) const;

    // ⑥h — capture the live room engine state into a RoomSnapshot for scene save
    // (/scene/save) and named recall (/room/preset). Reads the room param members
    // + atomics + the enable gate (active_reverb_==2). Control thread only (NOT
    // RT-safe); the values are control-mutated via applyRoomCtl on the audio
    // thread, but a torn read here at worst captures a one-block-stale scalar,
    // which is harmless for a save. Sets out.present = true.
    void snapshotRoom(ipc::RoomSnapshot& out) const;

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
    render::VAPRenderer           vap_;

    // FDN reverb (mono send → mono wet) and binaural side-output (mono → L/R).
    reverb::FdnReverb                        fdn_;
    std::unique_ptr<reverb::IReverbEngine>   ir_reverb_;
    std::atomic<int>                         active_reverb_{0}; // 0=FDN, 1=IR, 2=Room

    // ⑥b Dreamscape room engine — spatial late reverb. The mono reverb send is
    // run through the ported 8-line FDN; each line tap is fanned out across the
    // speaker bus via a cube-corner VBAP gain vector. ⑥e (late opp source-bias)
    // makes these gains dynamic: each block fdn_line_gains_ is recomputed by
    // computeLateFdnGains() steering the corners toward the source-energy-
    // opposite axis, with a WFS-fraction-driven diffuse amount.
    iae::RoomFdn                                              room_fdn_;
    // ⑥e-4 — authoritative late-FDN params (t60 / HF damping). The engine owns
    // this struct so /room/t60 and /room/late/hf can update one field and
    // re-push the whole struct via room_fdn_.setParams(). Touched only on the
    // audio thread (drain) after prepare; RoomFdn has no getter.
    iae::RoomFdnParams                                        room_fdn_params_{};
    std::array<std::array<float, spe::MAX_SPEAKERS>,
               iae::RoomFdn::kOrder>                          fdn_line_gains_{};
    std::vector<float>                                        room_lines_;   // [kOrder * maxBlock]
    // ⑥e-4-A (Phase-5 Late bus EQ) — the mono late send is run through a single
    // absorption HP→LP (reference lateBusHp/Lp, RoomEngine.cpp:650-658) BEFORE
    // the FDN. Default corners HP45 / LP16000 (kRoomLateHpfHz/Lpf); OSC-tunable
    // via /room/eq/late. This is a SEPARATE bus from the early/cluster EQ (those
    // stay coefficient-locked to each other); the late bus has its own corners.
    iae::RoomBiquad                                           late_eq_hp_{};
    iae::RoomBiquad                                           late_eq_lp_{};
    std::vector<float>                                        late_in_buf_;  // [maxBlock] EQ'd late send
    // ⑥f distance-gain curve params (reference roomDistance* / roomEarlyGain* /
    // roomLateGain*, SpatialSessionState.h). Map a source distance to early-tap
    // and late-send multipliers via iae::roomDistanceGainDbLinear. Touched only
    // on the audio thread (drain + render) after prepare → plain members.
    float                                                     room_dist_near_m_        = 0.5f;
    float                                                     room_dist_far_m_         = 24.f;
    float                                                     room_dist_linearity01_   = 0.35f;
    float                                                     room_early_gain_close_db_= -10.f;
    float                                                     room_early_gain_far_db_  = -18.f;
    float                                                     room_late_gain_close_db_ = -12.f;
    float                                                     room_late_gain_far_db_   = 0.f;
    // ⑥g — early-reflection predelay (ms), OSC-tunable via /room/predelay. Read
    // in renderRoomEarly; the pds it derives is clamped to er_predelay_max_-1
    // (< stride), so the single-branch ring wrap stays valid at any predelay.
    // Default single-sourced from the ported reference constant.
    float                                                     room_early_predelay_ms_  = iae::kRoomEarlyPredelayMs;
    // ⑥h — absorption-EQ corner frequencies kept as scalars alongside the
    // biquads, so the live corners are introspectable (snapshotRoom / scene save)
    // without reverse-engineering coefficients. Updated in applyEqEarly/applyEqLate.
    float                                                     room_eq_early_hp_ = iae::kRoomEarlyClusterHpfHz;
    float                                                     room_eq_early_lp_ = iae::kRoomEarlyClusterLpfHz;
    float                                                     room_eq_late_hp_  = iae::kRoomLateHpfHz;
    float                                                     room_eq_late_lp_  = iae::kRoomLateLpfHz;

    // ⑦ Speaker decorrelation — per-speaker Schroeder allpass cascade applied on
    // the output bus after the per-speaker gain/delay deinterleave (faithful to
    // the reference, which decorrelates after speaker gains). Params are plain
    // members written only on the audio thread (FIFO drain → applyDecorrCtl) and
    // read in the same audioBlock's output loop — race-free, no atomics needed.
    iae::SpeakerDecorrelationBank                             decorr_bank_;
    bool                                                      decorr_enabled_   = false;
    float                                                     decorr_mix01_     = 0.35f;
    float                                                     decorr_spread_ms_ = 4.0f;
    float                                                     decorr_ap_        = 0.62f;
    int                                                       decorr_stages_    = 4;
    std::uint32_t                                             decorr_seed_      = 0;
    // ⑦ — apply one drained DecorrCtl command (audio thread). Clamps + stores the
    // params; the bank reconfigures lazily per channel on the next output loop.
    void applyDecorrCtl(const util::QueuedCmd& qc) noexcept;
    // Dedicated room late bus: per-object reverb send scaled by that object's
    // lateMul, summed. Separate from reverb_send_buf_ (which feeds the non-room
    // FDN/IR un-scaled), mirroring the reference's separate lateBusInput.
    std::vector<float>                                        room_late_send_buf_;  // [maxBlock]
    bool                                                      room_ready_ = false;
    // ⑥e — fill fdn_line_gains_ for the current block: each Hadamard line is
    // steered from its static cube corner toward `opp` (the axis opposite the
    // late source-energy centroid; kLateCornerTowardOpposite=0.5) then blended
    // toward uniform diffuse by `lateDiffuse01`. Byte-faithful to
    // RoomEngine.cpp:567-583. RT-safe (vbap_gain_into uses stack scratch).
    void computeLateFdnGains(const iae::Vec3& opp, float lateDiffuse01,
                             int n_spk) noexcept;

    // ⑥d Shoebox early reflections — per-object first-order image-source taps.
    // Each active object's send-scaled dry signal is delayed through 6 per-image
    // ring buffers and panned (width-spread VBAP) onto the bus. Predelay /
    // absorption-EQ / cluster are increment ⑥e.
    static constexpr int kErRingLen = 512;     // matches reference kErRingLen
    iae::RoomEarlyParams                                     room_early_params_{};
    std::vector<float>                                       er_rings_;       // [MAX_OBJECTS*6*kErRingLen]
    std::array<std::array<int, iae::kNumFirstOrderImages>,
               spe::MAX_OBJECTS>                             er_write_pos_{};
    // ⑥e-3b — early predelay (per-object ring) + absorption EQ (per-object HP→LP)
    // applied to the send-scaled mono BEFORE the image-source ring taps. Faithful
    // to RoomEngine.cpp:374-406 (predelay then earlyClusterHp/Lp); EQ corner freqs
    // are the reference earlyCluster defaults (HP 120 / LP 10000 Hz) — OSC tuning
    // of predelay-ms / corners is the later ⑥e-4 increment.
    int                                                     er_predelay_max_ = 4801;
    int                                                     er_predelay_stride_ = 0;
    std::vector<float>                                      er_predelay_lines_; // [MAX_OBJECTS*stride]
    std::array<int, spe::MAX_OBJECTS>                       er_predelay_wpos_{};
    std::array<iae::RoomBiquad, spe::MAX_OBJECTS>           er_eq_hp_{};
    std::array<iae::RoomBiquad, spe::MAX_OBJECTS>           er_eq_lp_{};
    // Render the early reflections for all active objects into mix_buf_.
    void renderRoomEarly(int n_spk, int num_frames) noexcept;

    // ⑥e-4 — clear all room reverb tails/state for a clean onset when switching
    // INTO room mode (late FDN, early rings + predelay + per-object EQ, cluster
    // line + bus EQ). Factored out of the ReverbSelect drain so both
    // /reverb/select "room" and /room/enable 1 share one implementation. RT-safe
    // (fills + biquad reset; no allocation). Audio-thread only.
    void resetRoomState() noexcept;

    // ⑥e-4 — apply one drained RoomCtl command to the live room state. Runs on
    // the AUDIO thread inside the FIFO drain (same thread as the room render),
    // so RoomFdn/RoomCluster setParams() and the absorption-EQ recoeffs are
    // race-free and allocation-free. SetAll applies every field in one call =
    // atomic. EqEarly recoeffs the cluster-bus EQ and all per-object early EQ in
    // lockstep (reference contract). All values are clamped here, next to the
    // DSP that consumes them. Enable aliases /reverb/select "room" (1) / fdn (0).
    void applyRoomCtl(const util::QueuedCmd& qc) noexcept;

    // Phase 2.5 — apply one drained SysBinauralEq command. Runs on the AUDIO
    // thread inside the FIFO drain (same thread as the binaural post-chain), so
    // the RBJ peak recoeffs (RoomBiquad::setPeak, pure float math) are race-free
    // and allocation-free. Enable toggles the master flag; Band clamps + recoeffs
    // one band's L/R biquads (shared coeffs, independent state). Coeff-only — no
    // state reset, so live tuning never ticks the bus.
    void applyBinauralEq(const util::QueuedCmd& qc) noexcept;

    // ⑥e-2 Cluster (mid-field) diffusion — a shared mono bus fed by every active
    // object's predelayed+EQ'd send (xdel*cSend), run through an absorption EQ
    // (HP120→LP10000, the same earlyCluster corners) and the 6-tap feedforward
    // diffusion line, then fanned across the array via the opp-biased clusterU
    // gains. Byte-faithful to RoomEngine.cpp:414-419 / :553-565 / :594-647.
    // roomClusterSend01 / diffusion / virtual-volume are fixed at the reference
    // defaults until ⑥e-4 OSC tuning.
    iae::RoomCluster                                       room_cluster_;
    // ⑥e-4 — authoritative cluster params (diffusion / virtual volume). Same
    // ownership rationale as room_fdn_params_: /room/cluster/{diffusion,volume}
    // update one field and re-push via room_cluster_.setParams().
    iae::RoomClusterParams                                 room_cluster_params_{};
    std::vector<float>                                     cluster_bus_;    // [maxBlock] mono send bus
    std::vector<float>                                     cluster_out_;    // [maxBlock] diffused mono
    iae::RoomBiquad                                        cluster_eq_hp_{};
    iae::RoomBiquad                                        cluster_eq_lp_{};
    std::array<float, spe::MAX_SPEAKERS>                   cluster_gains_{}; // per-block diffuse VBAP
    // Read once per block on the audio thread; atomic so the ⑥e-4 OSC handler (or
    // a test) can set it from the control thread without a data race.
    std::atomic<float>                                    room_cluster_send01_{0.4f};
    // ⑥e-4 — early-reflection width-spread cone (degrees). Read once per block in
    // renderRoomEarly; atomic so /room/early/width (or a test) can set it from a
    // non-audio thread without a data race. Reference default 45° (was a local
    // constexpr before OSC plumbing).
    std::atomic<float>                                    room_early_width_deg_{45.f};
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

    // F4b: control-thread-readable, audio-thread-published CONSISTENT snapshot of
    // obj_cache_, via a LOCK-FREE single-writer/single-reader THREE-buffer
    // reader-claim handshake. The live obj_cache_ and the renderer read path are
    // UNCHANGED; only these three buffers are shared cross-thread. They are
    // allocated at engine construction (NOT on the audio thread) and persist
    // across prepare/stop-restart, like obj_cache_.
    //
    // Why this (not an optimistic rotation+seqlock): with an unbounded-rate
    // audio-thread writer, an optimistic seqlock reader memcpy is a FORMAL data
    // race under TSan even when its retry rejects the torn result (the bytes are
    // concurrently written while being read). The project bar is ZERO races with
    // NO suppression. Here the writer avoids any buffer that is either currently
    // published OR claimed-busy by the reader; with 3 buffers a free third always
    // exists. The reader claims the index it will read and re-confirms it is still
    // the published one.
    //
    // Safety is NOT a hard by-construction bound — it rests on a liveness property
    // (same class the plan acknowledges): correctness requires the writer to
    // observe the reader's `busy` claim within ~1 publish cadence. `busy` and
    // `published` are independent atomics with no joint atomicity, so the abstract
    // memory model permits a writer that misses a fresh `busy` claim for >=2
    // consecutive dirty blocks to pick a buffer the reader is mid-copy of. That
    // window is timing-unreachable here: the reader's ~8 KB copy completes in
    // << one audio-block period (~2.6 ms @64/48k), so the writer almost always
    // sees the claim by the next dirty block, and single-location coherence covers
    // the round after. Verified empirically by `soak_scene_save_race` (AC9) under
    // TSan (0 races / 0 tears) — that gate, not pure construction, is the proof.
    // The RT writer never blocks (two atomic loads + one release store; no alloc/lock).
    std::array<std::array<ObjCache, MAX_OBJECTS>, 3> snap_buf_{}; // ~24 KB @128
    std::atomic<int>         snap_published_idx_{-1};   // last index published; -1 = none yet
    mutable std::atomic<int> snap_reader_busy_idx_{-1}; // index reader is reading; -1 = idle

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
    // Phase 2.1 — binaural HRTF prefeed one-pole low-pass (BinauralMonitorChain.
    // cpp:106-125). Each active object's dry signal is filtered ONCE per block
    // into bin_prefeed_ before the B1/B2 render branches (both read it as the
    // HRTF input), so the LP state advances exactly once per block even when an
    // HRTF crossfade calls render_branch twice. Applied to the mono input
    // pre-HRTF (identically to both ears → no interaural-timing change). The
    // corner is tunable via /sys/binaural_prefeed (default 4200 Hz); a corner
    // above Nyquist is an effective passthrough. Audio-thread-only state.
    std::array<std::array<float, MAX_BLOCK>, MAX_OBJECTS> bin_prefeed_{};
    std::array<float, MAX_OBJECTS>                        bin_prefeed_lp_{};
    std::atomic<float>                                    bin_prefeed_cutoff_hz_{iae::kBinauralPrefeedLowPassHz};
    // Phase 2.4 — binaural monitor stereo delay ring (BinauralMonitorChain.cpp:
    // 132-154), on the final L/R bus BEFORE the EQ (reference order
    // HRTF→delay→EQ→limit). Rings are pre-allocated at prepareToPlay (control
    // thread); the audio path only does modulo indexing (alloc 0). The tap (ms)
    // is read once per block from a relaxed atomic and clamped to the ring; the
    // write index is audio-thread-only. 0 ms = passthrough.
    std::vector<float>                                    bin_delay_L_;
    std::vector<float>                                    bin_delay_R_;
    int                                                   bin_delay_write_ = 0;
    std::atomic<float>                                    bin_delay_ms_{0.f};
    // v0.9 Lane C (hoist): per-block per-algorithm object-state scratch. Moved
    // off the audio-callback stack to engine members so the audio thread never
    // carries ~10 KB of stack arrays at MAX_OBJECTS=128. Fixed-size, alloc-once,
    // rewritten every block in audioBlock — behaviour-identical to the prior
    // stack locals.
    std::array<render::ObjectState, MAX_OBJECTS> vbap_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> dbap_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> wfs_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> ambisonic_objs_{};
    std::array<render::ObjectState, MAX_OBJECTS> vap_objs_{};
    // mix_buf_: interleaved final output [sample * num_speakers + spk]
    // Per-algorithm scratches summed into mix_buf_.
    std::vector<float>  mix_buf_;
    std::vector<float>  vbap_scratch_;
    std::vector<float>  dbap_scratch_;
    std::vector<float>  wfs_scratch_;
    std::vector<float>  ambisonic_scratch_;
    std::vector<float>  vap_scratch_;
    // Per-speaker time-alignment (delay + gain).
    // spk_delays_ is user-settable & UNCLAMPED (layout YAML delay_ms →
    // delay_ms*0.001*sr) and may legitimately exceed 341 ms → KEEP the large
    // 48000 capacity (DelayLine48k). It is per-SPEAKER not per-object (~1.5 MB),
    // so keeping it large is footprint-neutral. (Lane F5 §0.4 #4 / amendment 1.)
    std::vector<spe::dsp::DelayLine48k>    spk_delays_;
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
    // Phase 2.6b — binaural head-tracking angles (DEGREES). Written by the OSC
    // control thread (relaxed store) on /ypr; read once per block by the B1
    // audio path (relaxed load) and applied via rotate_engine_dir_by_head
    // before the per-object HRTF setDirection. 3 independent relaxed atomics:
    // the inter-member tearing window is one block and converges; a seqlock is
    // YAGNI for a head pose that updates at tracker rates (<=120 Hz).
    std::atomic<float>         head_yaw_deg_{0.f};
    std::atomic<float>         head_pitch_deg_{0.f};
    std::atomic<float>         head_roll_deg_{0.f};
    // Phase 2.5 — binaural monitor 5-band peak EQ on the final L/R bus
    // (BinauralMonitorChain.cpp:156-202). The master flag is an atomic (read
    // once/block by the audio path; settable from the control thread). The
    // per-band coeffs (bin_eq_L_/R_) and param mirrors are touched ONLY on the
    // audio thread (initialize() + the FIFO drain applyBinauralEq + the per-
    // sample post-chain), so they need no atomics. L/R share coeffs but keep
    // independent biquad state. Default = off + flat (0 dB) = unity passthrough.
    std::atomic<bool>          binaural_eq_active_{false};
    std::array<iae::RoomBiquad, iae::kBinauralEqBands> bin_eq_L_{};
    std::array<iae::RoomBiquad, iae::kBinauralEqBands> bin_eq_R_{};
    std::array<float, iae::kBinauralEqBands>           bin_eq_freq_{};
    std::array<float, iae::kBinauralEqBands>           bin_eq_gain_db_{};
    std::array<float, iae::kBinauralEqBands>           bin_eq_q_{};
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

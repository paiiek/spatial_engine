// core/src/core/SpatialEngine.cpp

#include "ambi/AmbisonicEncoder.h"
#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "render/AlgorithmAnalyticReference.h"
#include "ipc/AdmOscConstants.h"
#include "reverb/ReverbEngine.h"
#include "util/DenormalGuard.h"
#include "util/RtAssertNoAlloc.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace spe::core {

SpatialEngine::SpatialEngine(int listen_port)
    : osc_backend_(
        [this](const ipc::Command& cmd) {
            // OSC thread → push to RT-safe FIFO
            util::QueuedCmd qc;
            qc.tag = cmd.tag;
            switch (cmd.tag) {
            case ipc::CommandTag::ObjMove:
                if (auto* p = std::get_if<ipc::PayloadObjMove>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.az_rad = p->az_rad;
                    qc.el_rad = p->el_rad;
                    qc.dist_m = p->dist_m;
                    qc.active = true;
                    // M5.1 echo: ObjMove originates from /adm/obj/N/aed.
                    // Phase 3.1 — p->az_rad is engine-frame (right+); the echo
                    // re-emits ADM-frame (left+), so negate engine -> ADM. With
                    // the inbound decode negation this makes the echo a faithful
                    // ADM->ADM passthrough (an external "left" comes back "left").
                    static constexpr float kRad2Deg =
                        180.f / 3.14159265358979323846f;
                    static constexpr float kMaxDist = 20.f;
                    osc_backend_.echoPlane().markAed(
                        p->obj_id, -p->az_rad * kRad2Deg,
                        p->el_rad * kRad2Deg,
                        kMaxDist > 0.f ? p->dist_m / kMaxDist : 0.f);
                }
                break;
            case ipc::CommandTag::ObjGain:
                if (auto* p = std::get_if<ipc::PayloadObjGain>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.gain   = p->gain;
                    // M5.1 echo.
                    osc_backend_.echoPlane().markGain(p->obj_id, p->gain);
                }
                break;
            case ipc::CommandTag::ObjActive:
                if (auto* p = std::get_if<ipc::PayloadObjActive>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.active = p->active;
                }
                break;
            case ipc::CommandTag::ObjAlgo:
                if (auto* p = std::get_if<ipc::PayloadObjAlgo>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.algo   = p->algo;
                    // F5-M3b (Option C): on the FIRST WFS activation, allocate the
                    // WFS delay lines on THIS (OSC/control, non-RT) thread and
                    // publish ready_ BEFORE this algo switch is pushed to the FIFO
                    // (cmd_fifo_.push below). So by the time the audio thread drains
                    // the switch and routes the object to wfs_objs_, the storage is
                    // built and ready_ is visible. Idempotent; no-op after the first.
                    if (p->algo == ipc::Algorithm::WFS) wfs_.ensureAllocated();
                }
                break;
            case ipc::CommandTag::NoiseType:
                if (auto* p = std::get_if<ipc::PayloadNoiseType>(&cmd.payload)) {
                    qc.noise_ch   = p->channel;
                    qc.noise_mode = p->mode;
                }
                break;
            case ipc::CommandTag::NoiseGain:
                if (auto* p = std::get_if<ipc::PayloadNoiseGain>(&cmd.payload)) {
                    qc.noise_ch     = p->channel;
                    qc.noise_gain_db = p->gain_db;
                }
                break;
            case ipc::CommandTag::NoiseSource:
                if (auto* p = std::get_if<ipc::PayloadNoiseSource>(&cmd.payload)) {
                    qc.noise_ch     = p->channel;
                    qc.noise_source = p->source;
                }
                break;
            case ipc::CommandTag::TransportPlay:
                // M5.1 — echo transport events.
                osc_backend_.echoPlane().markTransportPlay();
                break;
            case ipc::CommandTag::TransportStop:
                osc_backend_.echoPlane().markTransportStop();
                break;
            case ipc::CommandTag::ObjDsp:
                if (auto* p = std::get_if<ipc::PayloadObjDsp>(&cmd.payload)) {
                    qc.obj_id    = p->obj_id;
                    qc.dsp_param = static_cast<uint8_t>(p->param);
                    qc.dsp_value = p->value;
                }
                break;
            case ipc::CommandTag::SysReset:
                break;
            case ipc::CommandTag::SysAmbiOrder:
                if (auto* p = std::get_if<ipc::PayloadSysAmbiOrder>(&cmd.payload)) {
                    qc.ambi_order = p->order;
                }
                break;
            case ipc::CommandTag::SysAmbiDecoderType:
                if (auto* p = std::get_if<ipc::PayloadSysAmbiDecoderType>(&cmd.payload)) {
                    qc.ambi_decoder_type = p->type;
                }
                break;
            case ipc::CommandTag::SysLtcChase:
                if (auto* p = std::get_if<ipc::PayloadSysLtcChase>(&cmd.payload)) {
                    qc.ltc_chase_enable = p->enable ? 1 : 0;
                }
                break;
            case ipc::CommandTag::ReverbSelect:
                if (auto* p = std::get_if<ipc::PayloadReverbSelect>(&cmd.payload)) {
                    qc.reverb_which = p->which;
                }
                break;
            case ipc::CommandTag::RoomCtl:
                // ⑥e-4 — marshal the room params into the POD FIFO slot; the
                // audio-thread drain (applyRoomCtl) clamps + applies them. POD
                // throughout, so this rides the normal cmd_fifo_ push below.
                if (auto* p = std::get_if<ipc::PayloadRoomCtl>(&cmd.payload)) {
                    qc.room_op                   = static_cast<uint8_t>(p->op);
                    qc.room_enable               = p->enable;
                    qc.room_t60                  = p->t60;
                    qc.room_sx                   = p->sx;
                    qc.room_sy                   = p->sy;
                    qc.room_sz                   = p->sz;
                    qc.room_early_width_deg      = p->early_width_deg;
                    qc.room_early_balance01      = p->early_balance01;
                    qc.room_cluster_send01       = p->cluster_send01;
                    qc.room_cluster_diffusion01  = p->cluster_diffusion01;
                    qc.room_cluster_volume_m3    = p->cluster_volume_m3;
                    qc.room_eq_early_hp          = p->eq_early_hp;
                    qc.room_eq_early_lp          = p->eq_early_lp;
                    qc.room_late_hf_corner_hz    = p->late_hf_corner_hz;
                    qc.room_late_hf_ratio01      = p->late_hf_ratio01;
                    qc.room_eq_late_hp           = p->eq_late_hp;
                    qc.room_eq_late_lp           = p->eq_late_lp;
                    qc.room_dist_near_m          = p->dist_near_m;
                    qc.room_dist_far_m           = p->dist_far_m;
                    qc.room_dist_linearity01     = p->dist_linearity01;
                    qc.room_early_gain_close_db  = p->early_gain_close_db;
                    qc.room_early_gain_far_db    = p->early_gain_far_db;
                    qc.room_late_gain_close_db   = p->late_gain_close_db;
                    qc.room_late_gain_far_db     = p->late_gain_far_db;
                    qc.room_early_predelay_ms    = p->early_predelay_ms;
                }
                break;
            case ipc::CommandTag::DecorrCtl:
                // ⑦ — marshal decorrelation params into the POD FIFO slot; the
                // audio-thread drain (applyDecorrCtl) clamps + stores them.
                if (auto* p = std::get_if<ipc::PayloadDecorrCtl>(&cmd.payload)) {
                    qc.decorr_op        = static_cast<uint8_t>(p->op);
                    qc.decorr_enabled   = p->enabled;
                    qc.decorr_mix01     = p->mix01;
                    qc.decorr_spread_ms = p->delay_spread_ms;
                    qc.decorr_ap        = p->ap_coeff01;
                    qc.decorr_stages    = p->stages;
                    qc.decorr_seed      = p->seed;
                }
                break;
            case ipc::CommandTag::SysBinauralEq:
                // Phase 2.5 — marshal the binaural EQ op into the POD FIFO slot;
                // the audio-thread drain (applyBinauralEq) clamps + recoeffs.
                if (auto* p = std::get_if<ipc::PayloadSysBinauralEq>(&cmd.payload)) {
                    qc.bin_eq_op      = static_cast<uint8_t>(p->op);
                    qc.bin_eq_enable  = p->enable;
                    qc.bin_eq_band    = p->band;
                    qc.bin_eq_freq_hz = p->freq_hz;
                    qc.bin_eq_gain_db = p->gain_db;
                    qc.bin_eq_q       = p->q;
                }
                break;
            case ipc::CommandTag::OutputGain:
                if (auto* p = std::get_if<ipc::PayloadOutputGain>(&cmd.payload)) {
                    qc.output_ch       = p->channel;
                    qc.output_value_db = p->gain_db;
                }
                break;
            case ipc::CommandTag::OutputLimit:
                if (auto* p = std::get_if<ipc::PayloadOutputLimit>(&cmd.payload)) {
                    qc.output_ch       = p->channel;
                    qc.output_value_db = p->threshold_db;
                }
                break;
            // Phase C3 ADM-OSC v1.0 extended tags — obj_cache_ path only (ADR 0006)
            case ipc::CommandTag::ObjMute:
                if (auto* p = std::get_if<ipc::PayloadObjMute>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.active = !p->muted;  // mute=true → active=false
                    // M5.1 echo.
                    osc_backend_.echoPlane().markMute(p->obj_id,
                                                      p->muted ? 1 : 0);
                }
                break;
            case ipc::CommandTag::ObjXYZ:
                if (auto* p = std::get_if<ipc::PayloadObjXYZ>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.xyz_x  = p->x;
                    qc.xyz_y  = p->y;
                    qc.xyz_z  = p->z;
                    // M5.1 echo.
                    osc_backend_.echoPlane().markXyz(p->obj_id, p->x, p->y,
                                                     p->z);
                }
                break;
            case ipc::CommandTag::ObjActiveAdm:
                if (auto* p = std::get_if<ipc::PayloadObjActiveAdm>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    qc.active = p->active;
                    // M5.1 echo.
                    osc_backend_.echoPlane().markActive(p->obj_id,
                                                        p->active ? 1 : 0);
                }
                break;
            case ipc::CommandTag::ObjWidth:
                if (auto* p = std::get_if<ipc::PayloadObjWidth>(&cmd.payload)) {
                    qc.obj_id    = p->obj_id;
                    qc.width_rad = p->width_rad;
                    // M5.1 echo.
                    osc_backend_.echoPlane().markWidth(p->obj_id, p->width_rad);
                }
                break;
            case ipc::CommandTag::ObjName:
                if (auto* p = std::get_if<ipc::PayloadObjName>(&cmd.payload)) {
                    qc.obj_id = p->obj_id;
                    std::memcpy(qc.obj_name, p->name, 32);
                    // M5.1 echo.
                    osc_backend_.echoPlane().markName(p->obj_id, p->name);
                }
                break;
            // v0.4 — runtime path injection. Strings are NOT queueable through
            // the POD FIFO; handle on the control thread before the FIFO push
            // and early-return so no QueuedCmd is enqueued.
            case ipc::CommandTag::SysLoadLayout:
                if (auto* p = std::get_if<ipc::PayloadSysLoadLayout>(&cmd.payload)) {
                    layout_path_ = p->path;
                }
                return;
            case ipc::CommandTag::SysBinauralSofa:
                if (auto* p = std::get_if<ipc::PayloadSysBinauralSofa>(&cmd.payload)) {
                    binaural_sofa_path_ = p->path;
                }
                return;
            case ipc::CommandTag::SysBinauralEnable:
                if (auto* p = std::get_if<ipc::PayloadSysBinauralEnable>(&cmd.payload)) {
                    binaural_enabled_.store(p->enable);
                }
                return;
            case ipc::CommandTag::SysBinauralMode:
                if (auto* p = std::get_if<ipc::PayloadSysBinauralMode>(&cmd.payload)) {
                    binaural_.setRequestedMode(
                        p->mode == 1 ? output::BinauralMode::AmbiVS
                                     : output::BinauralMode::Direct);
                }
                return;
            case ipc::CommandTag::SysBinauralSofaSelect:
                // B-M3: resolve catalog name → speh_path on the control thread,
                // store the path for the ~1 Hz applyPendingBinauralSofa() tick.
                // Audio thread is NOT involved — this is an early-return path.
                if (auto* p = std::get_if<ipc::PayloadSysBinauralSofaSelect>(&cmd.payload)) {
                    // HrtfCatalog is control-thread-only; catalog_ is loaded at
                    // prepareToPlay / startup. Unknown name = silent no-op.
                    const auto* entry = hrtf_catalog_.find(p->name);
                    if (entry && !entry->speh_path.empty()) {
                        pending_binaural_sofa_path_ = entry->speh_path;
                        // release/acquire (paired with applyPendingBinauralSofa's
                        // acquire-load): publish pending_binaural_sofa_path_ before
                        // the flag so the control tick never sees a torn std::string.
                        pending_binaural_sofa_flag_.store(true, std::memory_order_release);
                        state_model_.setPendingSofaName(p->name);
                    }
                    // Out-of-catalog name → safe no-op (no crash).
                }
                return;
            case ipc::CommandTag::LayoutSlot:
                // Phase 4.3 Inc 2b — stash the layout-slot op for the ~1 Hz
                // control tick (applyPendingLayoutSlotOp), mirroring the
                // SysBinauralSofaSelect early-return above. The payload carries a
                // std::string label; the copy here runs on the OSC-IO/control
                // thread (alloc is FINE — NOT the audio drain). The control tick
                // is the SOLE owner of the LayoutLibrary and performs all file
                // I/O + replies. Audio thread is NOT involved — early-return path
                // (no QueuedCmd is enqueued).
                if (auto* p = std::get_if<ipc::PayloadLayoutSlot>(&cmd.payload)) {
                    pending_layout_op_ = *p;
                    // release: publish the pending_layout_op_ payload (incl. its
                    // std::string label) BEFORE the flag flips, so the control
                    // tick's acquire-load observes a fully-written op (no torn
                    // std::string read across the UDP-IO → control-tick handoff).
                    pending_layout_op_flag_.store(true, std::memory_order_release);
                }
                return;
            case ipc::CommandTag::SysBinauralResetDemote:
                // v0.7 D-S1 — user-controlled reset hatch. Runs on the OSC IO
                // thread; delegates entirely to BinauralMonitor. The result
                // (Accepted / CooldownActive / NotDemoted) arms the appropriate
                // warning latch; the heartbeat drain emits the OSC reply.
                if (auto* p = std::get_if<ipc::PayloadSysBinauralResetDemote>(&cmd.payload)) {
                    if (p->enable) {
                        using clock = std::chrono::steady_clock;
                        const int64_t now_ns =
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                clock::now().time_since_epoch()).count();
                        binaural_.resetRuntimeDemoteFromUser(now_ns);
                    }
                }
                return;
            case ipc::CommandTag::SysHeadYpr:
                // Phase 2.6b — binaural head tracking. Store the pose (degrees)
                // straight into the engine's relaxed atomics on the control
                // thread; the audio path reads them once per block. No FIFO slot
                // (float-only, no per-object state), so early-return like the
                // other control-thread /sys verbs above.
                if (auto* p = std::get_if<ipc::PayloadSysHeadYpr>(&cmd.payload)) {
                    head_yaw_deg_.store(p->yaw_deg,     std::memory_order_relaxed);
                    head_pitch_deg_.store(p->pitch_deg, std::memory_order_relaxed);
                    head_roll_deg_.store(p->roll_deg,   std::memory_order_relaxed);
                }
                return;
            case ipc::CommandTag::SysBinauralPrefeed:
                // Phase 2.1 — binaural HRTF prefeed LP corner. Single float into
                // a relaxed atomic on the control thread; the audio path reads it
                // once per block and recomputes the one-pole coeff. Float-only,
                // no per-object state → early-return (no FIFO slot), like /ypr.
                if (auto* p = std::get_if<ipc::PayloadSysBinauralPrefeed>(&cmd.payload)) {
                    bin_prefeed_cutoff_hz_.store(p->cutoff_hz, std::memory_order_relaxed);
                }
                return;
            case ipc::CommandTag::SysBinauralDelay:
                // Phase 2.4 — binaural monitor stereo delay tap (ms). Single
                // float into a relaxed atomic on the control thread; the audio
                // path reads it once per block and clamps to the ring. Float-only
                // → early-return (no FIFO slot), like /ypr / prefeed.
                if (auto* p = std::get_if<ipc::PayloadSysBinauralDelay>(&cmd.payload)) {
                    bin_delay_ms_.store(p->ms, std::memory_order_relaxed);
                }
                return;
            case ipc::CommandTag::SysHandshake:
                // v0.5.1 Q1 (WM-2) — when the client supplies an explicit
                // reply_port, retarget our captured peer endpoint so future
                // /sys/binaural_warning + /sys/binaural_status replies land
                // there instead of the sender's ephemeral source port.
                if (auto* p = std::get_if<ipc::PayloadSysHandshake>(&cmd.payload)) {
                    if (p->reply_port != 0) {
                        osc_backend_.overridePeerPort(p->reply_port);
                    }
                    // M5.1 — register echo subscriber when tag matches.
                    if (p->reply_port > 0 &&
                        std::strcmp(p->subscriber_tag,
                                    ipc::EchoPlane::kEchoSubscriberTag) == 0) {
                        using clock = std::chrono::steady_clock;
                        const int64_t now_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                clock::now().time_since_epoch())
                                .count();
                        // Peer IP is in last_peer_endpoint_ (IPv4 network order).
                        // Access via the captured sockaddr in OSCBackend — we use
                        // the reply_port the client advertised as the echo port.
                        const auto& ep = osc_backend_.lastPeerEndpoint();
                        if (ep.ss_family == AF_INET) {
                            const auto* sa4 =
                                reinterpret_cast<const struct sockaddr_in*>(&ep);
                            osc_backend_.echoPlane().addSubscriber(
                                sa4->sin_addr.s_addr, p->reply_port,
                                p->subscriber_tag, now_ms);
                        }
                    }
                }
                return;
            case ipc::CommandTag::HbPing:
                // ADR 0018 D-5 — tick the external-player liveness timestamp
                // when this ping came from the player (carries `,d`). The
                // engine's own 10 Hz publisher (`,h`) is excluded so its
                // internal loopback never masks a dead player. Clearing the
                // latch here means a resumed player re-arms the stale warning.
                if (auto* p = std::get_if<ipc::PayloadHbPing>(&cmd.payload)) {
                    if (p->from_external) {
                        using clock = std::chrono::system_clock;
                        const int64_t now_unix_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                clock::now().time_since_epoch()).count();
                        last_player_ping_unix_ms_.store(
                            now_unix_ms, std::memory_order_relaxed);
                        player_stale_latched_.store(
                            false, std::memory_order_relaxed);
                    }
                }
                // M5.1 — refresh TTL for any echo subscriber from this peer.
                if (osc_backend_.echoPlane().hasSubscribers()) {
                    const auto& ep = osc_backend_.lastPeerEndpoint();
                    if (ep.ss_family == AF_INET) {
                        using clock = std::chrono::steady_clock;
                        const int64_t now_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                clock::now().time_since_epoch())
                                .count();
                        const auto* sa4 =
                            reinterpret_cast<const struct sockaddr_in*>(&ep);
                        osc_backend_.echoPlane().touchSubscriberHb(
                            sa4->sin_addr.s_addr, now_ms);
                        osc_backend_.echoPlane().evictStale(now_ms);
                    }
                }
                return;
            default:
                return;  // not queued
            }
            cmd_fifo_.push(qc);
            // M5.1 — flush echo dirty bits to registered subscribers.
            // Called on the OSC IO thread once per inbound packet.
            if (osc_backend_.echoPlane().hasSubscribers()) {
                using clock = std::chrono::steady_clock;
                const int64_t now_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        clock::now().time_since_epoch())
                        .count();
                osc_backend_.echoPlane().flush(now_ms,
                                               osc_backend_.udpFdForEcho());
            }
        },
        listen_port)
{}

SpatialEngine::~SpatialEngine() {
    osc_backend_.stop();
}

void SpatialEngine::setLayout(spe::geometry::SpeakerLayout layout) {
    layout_     = std::move(layout);
    has_layout_ = true;
}

// F4b — consistent control-thread snapshot of obj_cache_ via the three-buffer
// published_index_ handshake (retry-until-stable seqlock reader). NOT RT-safe;
// called from the control loop only. See SpatialEngine.h for the mechanism.
void SpatialEngine::snapshotRoom(ipc::RoomSnapshot& out) const {
    out.present             = true;
    out.enabled             = (active_reverb_.load(std::memory_order_relaxed) == 2);
    out.t60                 = room_fdn_params_.t60Seconds;
    out.sx                  = room_early_params_.halfExtents.x;
    out.sy                  = room_early_params_.halfExtents.y;
    out.sz                  = room_early_params_.halfExtents.z;
    out.early_width_deg     = room_early_width_deg_.load(std::memory_order_relaxed);
    out.early_balance01     = room_early_params_.earlyLateBalance01;
    out.cluster_send01      = room_cluster_send01_.load(std::memory_order_relaxed);
    out.cluster_diffusion01 = room_cluster_params_.diffusion01;
    out.cluster_volume_m3   = room_cluster_params_.virtualVolumeM3;
    out.eq_early_hp         = room_eq_early_hp_;
    out.eq_early_lp         = room_eq_early_lp_;
    out.late_hf_corner_hz   = room_fdn_params_.hfDecayCornerHz;
    out.late_hf_ratio01     = room_fdn_params_.hfDecayRatio01;
    out.eq_late_hp          = room_eq_late_hp_;
    out.eq_late_lp          = room_eq_late_lp_;
    out.dist_near_m         = room_dist_near_m_;
    out.dist_far_m          = room_dist_far_m_;
    out.dist_linearity01    = room_dist_linearity01_;
    out.early_gain_close_db = room_early_gain_close_db_;
    out.early_gain_far_db   = room_early_gain_far_db_;
    out.late_gain_close_db  = room_late_gain_close_db_;
    out.late_gain_far_db    = room_late_gain_far_db_;
    out.early_predelay_ms   = room_early_predelay_ms_;
}

void SpatialEngine::snapshotObjects(std::vector<ipc::ObjectSnapshot>& out) const {
    // AC8 — algo enum ↔ ObjectSnapshot.algorithm (int) round-trip is a plain
    // static_cast in both directions; assert each variant survives compile-time.
    static_assert(static_cast<int>(ipc::Algorithm::VBAP)      == 0, "VBAP enum value");
    static_assert(static_cast<int>(ipc::Algorithm::WFS)       == 1, "WFS enum value");
    static_assert(static_cast<int>(ipc::Algorithm::DBAP)      == 2, "DBAP enum value");
    static_assert(static_cast<int>(ipc::Algorithm::Ambisonic) == 3, "Ambisonic enum value");
    static_assert(static_cast<int>(ipc::Algorithm::VAP)       == 4, "VAP enum value");
    static_assert(static_cast<ipc::Algorithm>(static_cast<int>(ipc::Algorithm::WFS))
                      == ipc::Algorithm::WFS, "algo int round-trip");

    out.clear();
    // Reader-claim fetch: load the published index, mark it busy (so the writer
    // skips it), then re-confirm it is still the published index. If a publish
    // slipped in between, retry from the new index. Once confirmed, the writer
    // is guaranteed to avoid this buffer until we release the claim, so the read
    // below races with nothing.
    int idx = snap_published_idx_.load(std::memory_order_acquire);
    if (idx < 0) {                               // nothing published → no objects driven
        snap_reader_busy_idx_.store(-1, std::memory_order_release);
        return;
    }
    for (;;) {
        snap_reader_busy_idx_.store(idx, std::memory_order_release);  // claim
        const int cur = snap_published_idx_.load(std::memory_order_acquire);
        if (cur == idx) break;                   // claim covers the live published buffer
        idx = cur;                               // a publish slipped in → re-claim
    }

    const std::array<ObjCache, MAX_OBJECTS>& local =
        snap_buf_[static_cast<std::size_t>(idx)];

    for (int i = 0; i < MAX_OBJECTS; ++i) {
        const ObjCache& c = local[static_cast<std::size_t>(i)];
        // Emit objects driven at least once: any non-default field, or active.
        const bool touched = c.active || c.az != 0.f || c.el != 0.f || c.dist != 1.f ||
                             c.gain_lin != 1.f || c.reverb_send != 0.f || c.width_rad != 0.f ||
                             c.algo != ipc::Algorithm::VBAP;
        if (!touched) continue;
        out.push_back(ipc::ObjectSnapshot{ /*id*/ i, c.az, c.el, c.dist,
                                           static_cast<int>(c.algo), c.gain_lin,
                                           /*muted*/ !c.active, c.width_rad, c.reverb_send });
    }
    // Release the claim so the writer may reuse this buffer once it publishes
    // elsewhere. (After this point we no longer touch snap_buf_[idx].)
    snap_reader_busy_idx_.store(-1, std::memory_order_release);
}

void SpatialEngine::prepareToPlay(double sample_rate, int max_block_size) {
    sample_rate_    = sample_rate;
    max_block_size_ = max_block_size;

    // Load default layout if not set. Probe common run-from CWDs so the
    // canonical core/build/ run does not emit a spurious parse warning.
    if (!has_layout_) {
        const char* candidates[] = {
            "configs/lab_8ch.yaml",
            "../configs/lab_8ch.yaml",
            "../../configs/lab_8ch.yaml",
        };
        for (const char* path : candidates) {
            auto result = spe::geometry::load_layout(path);
            if (spe::geometry::is_ok(result)) {
                layout_     = std::get<spe::geometry::SpeakerLayout>(result);
                has_layout_ = true;
                break;
            }
        }
    }

    if (has_layout_) {
        vbap_.prepareToPlay(layout_, sample_rate);
        dbap_.prepareToPlay(layout_, sample_rate);
        wfs_.prepareToPlay(layout_, sample_rate); // F5-M3b: leaves WFS lazy (unallocated)
        // F5-M3b safety net: prepareToPlay leaves WFS delay lines unallocated. If
        // a prior session already routed an object to WFS (obj_cache_ persists
        // across a re-prepare / layout reload), re-allocate now so that object is
        // not silently muted. Audio is stopped during prepare → reading obj_cache_
        // is safe; at first-ever prepare all objects are VBAP so this is a no-op.
        for (const auto& oc : obj_cache_)
            if (oc.algo == ipc::Algorithm::WFS) { wfs_.ensureAllocated(); break; }
        ambisonic_.applyPendingDecoderTypeChange(); // apply any pending type before prepare
        applyPendingBinauralSofa(); // B-M3: apply any pending catalog SOFA swap before prepare
        ambisonic_.prepareToPlay(layout_, sample_rate);
        vap_.prepareToPlay(layout_, sample_rate);
        const int n_spk = vbap_.numSpeakers();
        const size_t total = static_cast<size_t>(n_spk) * max_block_size;
        mix_buf_.assign(total, 0.f);
        vbap_scratch_.assign(total, 0.f);
        dbap_scratch_.assign(total, 0.f);
        wfs_scratch_.assign(total, 0.f);
        ambisonic_scratch_.assign(total, 0.f);
        vap_scratch_.assign(total, 0.f);
        // Per-channel noise generator state (one entry per speaker output).
        // Size > 1 avoids OOB if a /noise/{ch}/* command arrives before layout.
        noise_chans_.assign(static_cast<size_t>(std::max(n_spk, 1)), NoiseChan{});

        // ⑥b/⑥e — initialise the 8 late FDN line gain vectors. The authoritative
        // gains are recomputed every block in audioBlock via computeLateFdnGains()
        // with the live source-energy opp-bias; this control-thread call seeds them
        // with the reference no-energy default (opp = +up, minimum diffuse) so the
        // member is valid before the first room block. Native analytic VBAP, same
        // elevation-aware core the renderers use. room_ready_ gates the RT reads.
        computeLateFdnGains(iae::Vec3{ 0.f, 1.f, 0.f }, iae::kLateDiffuseMin, n_spk);
        room_ready_ = true;

        // Per-speaker time-alignment: delay lines + gain scalars
        spk_delays_.resize(static_cast<size_t>(n_spk));
        spk_gain_lin_.resize(static_cast<size_t>(n_spk));
        spk_delay_samples_.resize(static_cast<size_t>(n_spk));
        for (int i = 0; i < n_spk; ++i) {
            spk_delays_[static_cast<size_t>(i)].prepareToPlay(sample_rate);
            const float gain_db = (i < static_cast<int>(layout_.speakers.size()))
                ? layout_.speakers[static_cast<size_t>(i)].gain_db : 0.f;
            spk_gain_lin_[static_cast<size_t>(i)] = std::pow(10.f, gain_db / 20.f);
            const float delay_ms = (i < static_cast<int>(layout_.speakers.size()))
                ? layout_.speakers[static_cast<size_t>(i)].delay_ms : 0.f;
            spk_delay_samples_[static_cast<size_t>(i)] =
                delay_ms * 0.001f * static_cast<float>(sample_rate);
        }

        // Per-channel limiters
        spk_limiters_.assign(static_cast<size_t>(n_spk), spe::dsp::ChannelLimiter{});
        for (auto& lim : spk_limiters_) lim.prepare(sample_rate);

        render_ready_.store(true);
    } else {
        // Provide minimum noise capacity (1 channel) so OSC commands don't OOB.
        if (noise_chans_.empty()) noise_chans_.resize(1);
    }

    // C1.d — initialise LtcChase ring + decoder for 25 fps default. The
    // chase is gated by ltc_chase_enable_ at audioBlock entry; until /sys/
    // ltc_chase ,i 1 fires, no input samples land in the ring.
    ltc_chase_.prepare(sample_rate, /*fps_hint=*/25);

    // FDN reverb (mono send → mono wet)
    fdn_.prepareToPlay(sample_rate, max_block_size);
    // IR convolution reverb
    reverb::ReverbConfig ir_cfg;
    ir_cfg.type       = reverb::ReverbType::IRConvolution;
    ir_cfg.sampleRate = sample_rate;
    ir_cfg.blockSize  = max_block_size;
    ir_reverb_ = reverb::createReverbEngine(ir_cfg);
    reverb_send_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    reverb_wet_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    // ⑥b spatial room engine — late FDN fed by the mono reverb send; per-line
    // taps fanned out to cube-corner directions (gains computed below once the
    // layout is known). RoomFdn defaults match the reference mid-range.
    room_fdn_.prepare(sample_rate, max_block_size);
    // ⑥e-4 — make the engine-owned param struct authoritative from the start so
    // /room/t60 and /room/late/hf update one field and re-push the whole struct.
    room_fdn_params_ = iae::RoomFdnParams{};
    room_fdn_.setParams(room_fdn_params_);
    room_lines_.assign(static_cast<size_t>(iae::RoomFdn::kOrder) *
                       static_cast<size_t>(max_block_size), 0.f);
    // ⑥e-4-A — late-bus absorption EQ (HP45→LP16000, reference lateBusHp/Lp).
    // Filters the mono late send before the FDN. Buffer is prepare-allocated.
    late_in_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    late_eq_hp_.setHighPass(sample_rate, iae::kRoomLateHpfHz);
    late_eq_lp_.setLowPass(sample_rate, iae::kRoomLateLpfHz);
    // ⑥f — dedicated room late bus (per-object lateMul-scaled send).
    room_late_send_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    // ⑦ — per-speaker decorrelation bank (heap delay rings sized here).
    decorr_bank_.prepare(sample_rate, max_block_size);
    // ⑥d early-reflection ring buffers: one kErRingLen ring per (object, image).
    er_rings_.assign(static_cast<size_t>(MAX_OBJECTS) *
                     static_cast<size_t>(iae::kNumFirstOrderImages) *
                     static_cast<size_t>(kErRingLen), 0.f);
    for (auto& obj_w : er_write_pos_) obj_w.fill(0);
    // ⑥e-3b — per-object early predelay lines (sized for the reference 100 ms max,
    // RoomEngine.cpp:220-222) + absorption-EQ biquads (HP 120 / LP 10000, the
    // reference earlyCluster defaults). Coeffs are fixed until ⑥e-4 OSC tuning.
    er_predelay_max_    = std::clamp(
        static_cast<int>(std::lround(0.1 * sample_rate)) + 1, 1, 19200);
    er_predelay_stride_ = er_predelay_max_ + MAX_BLOCK + 8;
    er_predelay_lines_.assign(static_cast<size_t>(MAX_OBJECTS) *
                              static_cast<size_t>(er_predelay_stride_), 0.f);
    er_predelay_wpos_.fill(0);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        er_eq_hp_[static_cast<size_t>(i)].setHighPass(sample_rate, iae::kRoomEarlyClusterHpfHz);
        er_eq_lp_[static_cast<size_t>(i)].setLowPass(sample_rate, iae::kRoomEarlyClusterLpfHz);
    }
    // ⑥e-2 cluster diffusion — shared mono bus + delay line + bus absorption EQ
    // (HP120→LP10000, the reference cEcHp/cEcLp = earlyCluster corners). Defaults
    // diffusion=0.48 / virtual-volume=630 m³ (RoomEngine.cpp:204-206/610-611).
    room_cluster_.prepare(sample_rate);
    room_cluster_params_ = iae::RoomClusterParams{};    // reference defaults
    room_cluster_.setParams(room_cluster_params_);      // ⑥e-4 authoritative
    cluster_bus_.assign(static_cast<size_t>(max_block_size), 0.f);
    cluster_out_.assign(static_cast<size_t>(max_block_size), 0.f);
    cluster_eq_hp_.setHighPass(sample_rate, iae::kRoomEarlyClusterHpfHz);
    cluster_eq_lp_.setLowPass(sample_rate, iae::kRoomEarlyClusterLpfHz);

    // Per-object DSP chain (EQ → user delay → distance gain → HF rolloff → propagation delay → reverb send)
    // Heap-allocated to avoid stack overflow (each chain owns ~384 KB of delay buffers).
    if (static_cast<int>(chains_.size()) != MAX_OBJECTS) {
        chains_.clear();
        chains_.resize(MAX_OBJECTS);
    }
    for (auto& c : chains_) c.prepareToPlay(sample_rate);

    // BinauralMonitor side-output. v0.5 P3: pass the .speh path through if one
    // has been set via OSC /sys/binaural_sofa. Pass-through fallback when empty.
    output::BinauralMonitor::Config bcfg;
    bcfg.sofaPath   = binaural_sofa_path_;
    bcfg.sampleRate = static_cast<float>(sample_rate);
    bcfg.blockSize  = max_block_size;
    binaural_ok_ = (binaural_.initialize(bcfg) == output::BinauralMonitor::InitResult::Ok);
    binaural_l_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    binaural_r_buf_.assign(static_cast<size_t>(max_block_size), 0.f);
    bin_tmp_L_.assign(static_cast<size_t>(max_block_size), 0.f);
    bin_tmp_R_.assign(static_cast<size_t>(max_block_size), 0.f);
    binaural_lim_L_.prepare(sample_rate);
    binaural_lim_R_.prepare(sample_rate);
    // -1 dBFS threshold on the binaural bus.
    binaural_lim_L_.setThreshold(std::pow(10.f, -1.f / 20.f));
    binaural_lim_R_.setThreshold(std::pow(10.f, -1.f / 20.f));

    // Phase 2.5 — binaural monitor 5-band peak EQ. Initialise each band to its
    // reference default freq/Q with 0 dB gain (a 0 dB peak biquad is a unity
    // passthrough), so the EQ is flat until /sys/binaural_eq tunes it. Reset the
    // L/R state for a clean start. Master flag defaults off (reference
    // binauralMonitorEqActive=false); a re-prepare leaves the current flag.
    for (int b = 0; b < iae::kBinauralEqBands; ++b) {
        const size_t bi = static_cast<size_t>(b);
        bin_eq_freq_[bi]    = iae::kBinauralEqDefaultFreqHz[bi];
        bin_eq_gain_db_[bi] = 0.f;
        bin_eq_q_[bi]       = iae::kBinauralEqDefaultQ[bi];
        bin_eq_L_[bi].setPeak(sample_rate, bin_eq_freq_[bi], bin_eq_q_[bi], 0.f);
        bin_eq_R_[bi].setPeak(sample_rate, bin_eq_freq_[bi], bin_eq_q_[bi], 0.f);
        bin_eq_L_[bi].reset();
        bin_eq_R_[bi].reset();
    }
    // Phase 2.1 — reset the binaural prefeed one-pole state (the cutoff atomic
    // keeps its current value across a re-prepare; default = 4200 Hz).
    bin_prefeed_lp_.fill(0.f);
    // Phase 2.4 — (re)allocate the binaural monitor stereo delay rings on the
    // control thread and clear them; the audio path never resizes. The tap
    // atomic keeps its value across a re-prepare (default 0 ms = passthrough).
    bin_delay_L_.assign(static_cast<size_t>(iae::kBinauralDelayRingCap), 0.f);
    bin_delay_R_.assign(static_cast<size_t>(iae::kBinauralDelayRingCap), 0.f);
    bin_delay_write_ = 0;

    // Wire dry_ptrs_ to scratch buffers
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        dry_ptrs_[static_cast<size_t>(i)] = dry_scratch_[static_cast<size_t>(i)].data();
    }
    // v0.5 P4: wire b2_sh_ptrs_ once; AmbiDecoder::decode() reads them every block.
    for (int k = 0; k < 16; ++k) {
        b2_sh_ptrs_[static_cast<size_t>(k)] =
            b2_sh_scratch_[static_cast<size_t>(k)].data();
    }

    // v0.9 Lane A (A-M1) — the block budget (num_frames / sample_rate) just
    // changed; clear the CPU meter so samples taken under the previous budget
    // cannot poison the EWMA/peak/p99 estimators.
    cpu_meter_.reset();

    osc_backend_.start();
    prepared_.store(true);
}

void SpatialEngine::releaseResources() {
    osc_backend_.stop();
    render_ready_.store(false);
    prepared_.store(false);
}

// ⑥d — render per-object Shoebox early reflections into mix_buf_. For each
// active object with a reverb send, its send-scaled dry signal is delayed
// through 6 first-order image ring buffers and panned via width-spread VBAP
// (3 samples, triangular {1,2,1}/4). RT-safe: ring buffers pre-allocated;
// vbap_gain_into uses stack scratch; no allocation here. (predelay / absorption
// EQ / cluster are ⑥e.)
void SpatialEngine::renderRoomEarly(int n_spk, int num_frames) noexcept {
    static constexpr int   kSpread       = iae::kEarlySpreadSamples;     // 3
    static constexpr float kErTri3[3]    = { 1.f, 2.f, 1.f };
    static constexpr float kErTri3Inv    = 1.f / 4.f;
    // ⑥e-4 — early width-spread cone, OSC-tunable via /room/early/width. Read
    // once per block (atomic) and clamped to a sane cone [0, 180]°.
    const float kEarlyWidthDeg =
        std::clamp(room_early_width_deg_.load(std::memory_order_relaxed), 0.f, 180.f);

    // ⑥e-2 — the cluster bus accumulates every object's xdel this block; clear it
    // before the per-object loop. cSend = jlimit(0,1,roomClusterSend01)*0.48
    // (RoomEngine.cpp:415); roomClusterSend01 is OSC-tunable via /room/cluster/send.
    std::fill(cluster_bus_.begin(), cluster_bus_.begin() + num_frames, 0.f);
    const float cSend =
        std::clamp(room_cluster_send01_.load(std::memory_order_relaxed), 0.f, 1.f) * 0.48f;

    for (int i = 0; i < MAX_OBJECTS; ++i) {
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        if (!c.active || c.reverb_send <= 1.0e-6f) continue;

        // Object position (listener at origin), mmhoa frame x=right,y=up,z=front.
        const float ce = std::cos(c.el);
        const iae::Vec3 pos{ c.dist * ce * std::sin(c.az),
                             c.dist * std::sin(c.el),
                             c.dist * ce * std::cos(c.az) };

        iae::EarlyReflection refl[iae::kNumFirstOrderImages];
        const int nv = iae::computeFirstOrderReflections(
            pos, room_early_params_, sample_rate_, kErRingLen, refl);
        if (nv <= 0) continue;

        const float* dry = dry_scratch_[static_cast<size_t>(i)].data();
        const float  send = c.reverb_send;
        // ⑥f — this object's early distance multiplier (reference earlyDistMul,
        // AudioEngine.cpp:824) scales the predelay input, so it attenuates BOTH
        // the 6 image-source rings and the cluster bus (both read xdel), exactly
        // as the reference scales wetMono before the predelay (RoomEngine.cpp:386).
        const float earlyMul = iae::roomDistanceGainDbLinear(
            c.dist, room_dist_near_m_, room_dist_far_m_,
            room_early_gain_close_db_, room_early_gain_far_db_, room_dist_linearity01_);

        // ⑥e-3b — predelay (per-object ring) then absorption EQ (HP→LP) on the
        // send-scaled mono, once per object; all 6 image rings read this xdel.
        // Faithful to RoomEngine.cpp:374-406. The single-branch ring wraps below
        // (pr += stride / prw reset) are valid only while pds < stride; pds is
        // clamped to er_predelay_max_-1 (< stride = er_predelay_max_+MAX_BLOCK+8)
        // here, so the invariant holds for ANY predelay. ⑥g exercises this via the
        // live /room/predelay path — the clamp must stay at max-1 (not stride).
        float xdel[MAX_BLOCK];
        {
            const int   stride = er_predelay_stride_;
            const int   pds = std::min(std::max(0, er_predelay_max_ - 1),
                static_cast<int>(std::lround(room_early_predelay_ms_ * 0.001 * sample_rate_)));
            float* pline = er_predelay_lines_.data() +
                           static_cast<size_t>(i) * static_cast<size_t>(stride);
            int& prw = er_predelay_wpos_[static_cast<size_t>(i)];
            iae::RoomBiquad& hp = er_eq_hp_[static_cast<size_t>(i)];
            iae::RoomBiquad& lp = er_eq_lp_[static_cast<size_t>(i)];
            for (int n = 0; n < num_frames; ++n) {
                int pr = prw - pds; if (pr < 0) pr += stride;
                float y = pline[static_cast<size_t>(pr)];
                pline[static_cast<size_t>(prw)] = dry[static_cast<size_t>(n)] * send * earlyMul;
                if (++prw >= stride) prw = 0;
                y = hp.processSample(y);
                xdel[n] = lp.processSample(y);
            }
        }

        // ⑥e-2 — feed this object's predelayed+EQ'd mono into the shared cluster
        // bus (RoomEngine.cpp:414-419). Same xdel the 6 image rings read below.
        for (int n = 0; n < num_frames; ++n)
            cluster_bus_[static_cast<size_t>(n)] += xdel[static_cast<size_t>(n)] * cSend;

        for (int t = 0; t < iae::kNumFirstOrderImages; ++t) {
            if (!refl[t].valid) continue;

            // Per-block: width-spread VBAP gains for this reflection direction.
            float spread_gains[kSpread][spe::MAX_SPEAKERS] = {};
            for (int wi = 0; wi < kSpread; ++wi) {
                const iae::Vec3 d =
                    iae::earlySpreadDirection(refl[t].dir, kEarlyWidthDeg, wi, kSpread);
                const float az = std::atan2(d.x, d.z);
                const float el = std::asin(std::clamp(d.y, -1.f, 1.f));
                render::AlgorithmAnalyticReference::vbap_gain_into(
                    layout_, az, el, spread_gains[wi], spe::MAX_SPEAKERS);
                // ⑥e — diffuse blend so early reflections lay across the array
                // instead of isolating on the VBAP triplet (RMS-preserving).
                iae::blendVbapWithUniformDiffuse(spread_gains[wi], n_spk,
                                                 iae::kErDiffuseNonWfs);
            }

            float* ring = er_rings_.data() +
                (static_cast<size_t>(i) * iae::kNumFirstOrderImages +
                 static_cast<size_t>(t)) * static_cast<size_t>(kErRingLen);
            int& wp = er_write_pos_[static_cast<size_t>(i)][static_cast<size_t>(t)];
            const int delay = refl[t].delaySamples;
            const float tapGain = refl[t].gain;

            for (int n = 0; n < num_frames; ++n) {
                ring[static_cast<size_t>(wp)] = xdel[static_cast<size_t>(n)];
                int rp = wp - delay; if (rp < 0) rp += kErRingLen;
                const float s = ring[static_cast<size_t>(rp)] * tapGain;
                if (++wp >= kErRingLen) wp = 0;
                if (std::abs(s) < 1.0e-9f) continue;
                for (int wi = 0; wi < kSpread; ++wi) {
                    const float w = s * kErTri3[wi] * kErTri3Inv;
                    const float* g = spread_gains[wi];
                    for (int sp = 0; sp < n_spk; ++sp)
                        mix_buf_[static_cast<size_t>(n * n_spk + sp)] += w * g[sp];
                }
            }
        }
    }
}

// ⑥e-4 — clear all room reverb tails/state for a clean onset when switching INTO
// room mode. Shared by /reverb/select "room" and /room/enable 1. RT-safe (fills +
// biquad reset; no allocation). Audio-thread only.
void SpatialEngine::resetRoomState() noexcept {
    room_fdn_.reset();
    std::fill(er_rings_.begin(), er_rings_.end(), 0.f);
    for (auto& obj_w : er_write_pos_) obj_w.fill(0);
    // ⑥e-3b — early predelay lines + per-object early EQ state.
    std::fill(er_predelay_lines_.begin(), er_predelay_lines_.end(), 0.f);
    er_predelay_wpos_.fill(0);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        er_eq_hp_[static_cast<size_t>(i)].reset();
        er_eq_lp_[static_cast<size_t>(i)].reset();
    }
    // ⑥e-2 — cluster delay line + bus EQ state.
    room_cluster_.reset();
    cluster_eq_hp_.reset();
    cluster_eq_lp_.reset();
    // ⑥e-4-A — late-bus EQ state.
    late_eq_hp_.reset();
    late_eq_lp_.reset();
}

// ⑥e-4 — apply one drained RoomCtl command to the live room state (audio thread,
// inside the FIFO drain). All values are clamped here, next to the DSP that
// consumes them. RoomFdn/RoomCluster setParams() only store the struct (no
// realloc — delay-line lengths are fixed at prepare), so they are RT-safe on the
// audio thread. SetAll applies every field in one call → atomic. EqEarly recoeffs
// the cluster-bus EQ and ALL per-object early EQ in lockstep (reference contract).
void SpatialEngine::applyRoomCtl(const util::QueuedCmd& qc) noexcept {
    using Op = ipc::PayloadRoomCtl::Op;

    // Absorption-EQ corner clamp: keep strictly inside (0, Nyquist).
    const float fnyq = static_cast<float>(0.45 * sample_rate_);
    auto clampHz = [&](float f) { return std::clamp(f, 10.f, std::max(20.f, fnyq)); };

    auto applyT60 = [&](float t60) {
        room_fdn_params_.t60Seconds = std::clamp(t60, 0.2f, 6.f);
        room_fdn_.setParams(room_fdn_params_);
    };
    auto applyLateHf = [&](float corner, float ratio) {
        room_fdn_params_.hfDecayCornerHz = std::clamp(corner, 800.f, 16000.f);
        room_fdn_params_.hfDecayRatio01  = std::clamp(ratio, 0.05f, 1.f);
        room_fdn_.setParams(room_fdn_params_);
    };
    auto applySize = [&](float sx, float sy, float sz) {
        // Floor at 0.5 m to match computeFirstOrderReflections' own re-floor
        // (RoomEarly.cpp) so the stored struct is authoritative — introspection
        // sees exactly the half-extents the early-reflection geometry uses.
        room_early_params_.halfExtents = iae::Vec3{
            std::max(0.5f, sx), std::max(0.5f, sy), std::max(0.5f, sz) };
    };
    auto applyEarlyBalance = [&](float b) {
        room_early_params_.earlyLateBalance01 = std::clamp(b, 0.f, 1.f);
    };
    auto applyEarlyWidth = [&](float w) {
        room_early_width_deg_.store(std::clamp(w, 0.f, 180.f),
                                    std::memory_order_relaxed);
    };
    auto applyClusterSend = [&](float s) {
        room_cluster_send01_.store(std::clamp(s, 0.f, 1.f),
                                   std::memory_order_relaxed);
    };
    auto applyClusterDiffusion = [&](float d) {
        room_cluster_params_.diffusion01 = std::clamp(d, 0.f, 1.f);
        room_cluster_.setParams(room_cluster_params_);
    };
    auto applyClusterVolume = [&](float v) {
        room_cluster_params_.virtualVolumeM3 = std::max(50.f, v);
        room_cluster_.setParams(room_cluster_params_);
    };
    // EQ lockstep: the cluster-bus absorption EQ and EVERY per-object early EQ
    // share the same HP/LP corners — recoeff all together so they never drift
    // (reference syncRoomEqCoeffsIfNeeded). Coeff-only (no state reset) = live
    // tuning without a tail discontinuity. RT-safe: float math only, no alloc.
    auto applyEqEarly = [&](float hp, float lp) {
        const float h = clampHz(hp);
        const float l = clampHz(lp);
        room_eq_early_hp_ = h;   // ⑥h — record live corners for snapshotRoom
        room_eq_early_lp_ = l;
        cluster_eq_hp_.setHighPass(sample_rate_, h);
        cluster_eq_lp_.setLowPass(sample_rate_, l);
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            er_eq_hp_[static_cast<size_t>(i)].setHighPass(sample_rate_, h);
            er_eq_lp_[static_cast<size_t>(i)].setLowPass(sample_rate_, l);
        }
    };
    // Late-bus EQ — a SEPARATE single filter pair (not locked to early/cluster);
    // the late FDN tail has its own corners (reference lateBusHp/Lp 45/16000).
    auto applyEqLate = [&](float hp, float lp) {
        room_eq_late_hp_ = clampHz(hp);   // ⑥h — record live corners
        room_eq_late_lp_ = clampHz(lp);
        late_eq_hp_.setHighPass(sample_rate_, room_eq_late_hp_);
        late_eq_lp_.setLowPass(sample_rate_, room_eq_late_lp_);
    };
    // ⑥f distance-gain params. iae::roomDistanceGainDbLinear re-clamps internally;
    // we also clamp the stored members to the reference pull ranges so
    // introspection is authoritative (SpatialSessionState.cpp:386-398).
    auto applyDistance = [&](float nearM, float farM, float lin) {
        room_dist_near_m_      = std::clamp(nearM, 0.05f, 40.f);
        // far: reference window [0.1,120] m, then floored to near+0.1
        // (SpatialSessionState.cpp:386-390) — keep the 120 m cap so the stored
        // member matches the reference pull range.
        room_dist_far_m_       = std::clamp(std::max(room_dist_near_m_ + 0.1f, farM), 0.1f, 120.f);
        room_dist_linearity01_ = std::clamp(lin, 0.f, 1.f);
    };
    auto applyEarlyGain = [&](float closeDb, float farDb) {
        room_early_gain_close_db_ = std::clamp(closeDb, -48.f, 12.f);
        room_early_gain_far_db_   = std::clamp(farDb, -48.f, 12.f);
    };
    auto applyLateGain = [&](float closeDb, float farDb) {
        room_late_gain_close_db_ = std::clamp(closeDb, -48.f, 12.f);
        room_late_gain_far_db_   = std::clamp(farDb, -48.f, 12.f);
    };
    // ⑥g — early predelay (ms). Clamp to the reference 0..100 ms window; the pds
    // it derives is further floored to er_predelay_max_-1 (< stride) in
    // renderRoomEarly, so the single-branch ring wrap stays valid at any value.
    auto applyPredelay = [&](float ms) {
        room_early_predelay_ms_ = std::clamp(ms, 0.f, 100.f);
    };

    switch (static_cast<Op>(qc.room_op)) {
    case Op::Enable: {
        // Alias /reverb/select: enable→room (2), disable→fdn (0). Reset the room
        // tails on entry (prev != 2), mirroring the ReverbSelect drain.
        const int which = qc.room_enable ? 2 : 0;
        const int prev  = active_reverb_.exchange(which, std::memory_order_relaxed);
        if (which == 2 && prev != 2) resetRoomState();
        break;
    }
    case Op::SetAll:
        applyT60(qc.room_t60);
        applySize(qc.room_sx, qc.room_sy, qc.room_sz);
        applyEarlyWidth(qc.room_early_width_deg);
        applyEarlyBalance(qc.room_early_balance01);
        applyClusterSend(qc.room_cluster_send01);
        applyClusterDiffusion(qc.room_cluster_diffusion01);
        applyClusterVolume(qc.room_cluster_volume_m3);
        applyEqEarly(qc.room_eq_early_hp, qc.room_eq_early_lp);
        applyLateHf(qc.room_late_hf_corner_hz, qc.room_late_hf_ratio01);
        applyEqLate(qc.room_eq_late_hp, qc.room_eq_late_lp);
        applyDistance(qc.room_dist_near_m, qc.room_dist_far_m, qc.room_dist_linearity01);
        applyEarlyGain(qc.room_early_gain_close_db, qc.room_early_gain_far_db);
        applyLateGain(qc.room_late_gain_close_db, qc.room_late_gain_far_db);
        applyPredelay(qc.room_early_predelay_ms);
        break;
    case Op::T60:              applyT60(qc.room_t60); break;
    case Op::Size:             applySize(qc.room_sx, qc.room_sy, qc.room_sz); break;
    case Op::EarlyWidth:       applyEarlyWidth(qc.room_early_width_deg); break;
    case Op::EarlyBalance:     applyEarlyBalance(qc.room_early_balance01); break;
    case Op::ClusterSend:      applyClusterSend(qc.room_cluster_send01); break;
    case Op::ClusterDiffusion: applyClusterDiffusion(qc.room_cluster_diffusion01); break;
    case Op::ClusterVolume:    applyClusterVolume(qc.room_cluster_volume_m3); break;
    case Op::EqEarly:          applyEqEarly(qc.room_eq_early_hp, qc.room_eq_early_lp); break;
    case Op::EqLate:           applyEqLate(qc.room_eq_late_hp, qc.room_eq_late_lp); break;
    case Op::LateHf:           applyLateHf(qc.room_late_hf_corner_hz, qc.room_late_hf_ratio01); break;
    case Op::Distance:         applyDistance(qc.room_dist_near_m, qc.room_dist_far_m, qc.room_dist_linearity01); break;
    case Op::EarlyGain:        applyEarlyGain(qc.room_early_gain_close_db, qc.room_early_gain_far_db); break;
    case Op::LateGain:         applyLateGain(qc.room_late_gain_close_db, qc.room_late_gain_far_db); break;
    case Op::Predelay:         applyPredelay(qc.room_early_predelay_ms); break;
    }
}

// ⑥e (late opp source-bias) — recompute the 8 late FDN line gain vectors for the
// current block. Each Hadamard line starts at its static cube corner {±1,±1,±1}/√3
// and is steered toward `opp` (the axis opposite the late source-energy centroid)
// by kLateCornerTowardOpposite=0.5, then blended toward uniform diffuse by
// `lateDiffuse01`. Byte-faithful to RoomEngine.cpp:567-583 (cube-corner ordering
// matches cubeCornerDirection). Frame: mmhoa x=right,y=up,z=front with
// az=atan2(x,z)/el=asin(y), same as the ⑥b corner mapping. RT-safe: vbap_gain_into
// uses stack scratch; blendVbapWithUniformDiffuse allocates nothing.
iae::Vec3 SpatialEngine::lateFdnLineDirection(int k, const iae::Vec3& opp) noexcept {
    static constexpr float kCorner[iae::RoomFdn::kOrder][3] = {
        { 1.f, 1.f, 1.f}, { 1.f, 1.f,-1.f}, { 1.f,-1.f, 1.f}, { 1.f,-1.f,-1.f},
        {-1.f, 1.f, 1.f}, {-1.f, 1.f,-1.f}, {-1.f,-1.f, 1.f}, {-1.f,-1.f,-1.f},
    };
    static constexpr float kLateCornerTowardOpposite = 0.5f;
    const float inv = 1.f / std::sqrt(3.f);
    const int kk = k & 7;
    const iae::Vec3 corner{ kCorner[kk][0] * inv,
                            kCorner[kk][1] * inv,
                            kCorner[kk][2] * inv };
    return iae::normalized(corner * (1.f - kLateCornerTowardOpposite)
                         + opp * kLateCornerTowardOpposite);
}

// ⑦ — apply one drained DecorrCtl command (audio thread, FIFO drain). Stores the
// clamped params into plain members read by the output loop later in the same
// audioBlock; the bank itself reconfigures per channel lazily (cfgHash) on the
// next process. RT-safe (no alloc; the bank's rings are prepare-allocated).
void SpatialEngine::applyDecorrCtl(const util::QueuedCmd& qc) noexcept {
    using Op = ipc::PayloadDecorrCtl::Op;
    switch (static_cast<Op>(qc.decorr_op)) {
    case Op::Enable: decorr_enabled_   = qc.decorr_enabled; break;
    case Op::Mix:    decorr_mix01_     = std::clamp(qc.decorr_mix01, 0.f, 1.f); break;
    case Op::Spread: decorr_spread_ms_ = std::clamp(qc.decorr_spread_ms, 0.f, 24.f); break;
    case Op::Ap:     decorr_ap_        = std::clamp(qc.decorr_ap, 0.02f, 0.92f); break;
    case Op::Stages: decorr_stages_    = std::clamp(qc.decorr_stages, 1, iae::SpeakerDecorrelationBank::kMaxStages); break;
    case Op::Seed:   decorr_seed_      = qc.decorr_seed; break;
    case Op::SetAll:
        decorr_enabled_   = qc.decorr_enabled;
        decorr_mix01_     = std::clamp(qc.decorr_mix01, 0.f, 1.f);
        decorr_spread_ms_ = std::clamp(qc.decorr_spread_ms, 0.f, 24.f);
        decorr_ap_        = std::clamp(qc.decorr_ap, 0.02f, 0.92f);
        decorr_stages_    = std::clamp(qc.decorr_stages, 1, iae::SpeakerDecorrelationBank::kMaxStages);
        decorr_seed_      = qc.decorr_seed;
        break;
    }
}

// Phase 2.5 — apply one drained SysBinauralEq command (audio thread, inside the
// FIFO drain). Enable toggles the master flag; Band clamps freq/gainDb/Q and
// recoeffs that band's L/R RBJ biquads in lockstep (shared coeffs, independent
// state). RT-safe: pure float math (RoomBiquad::setPeak), no allocation, no
// state reset (coeff-only = glitch-free live tuning). The param mirrors feed
// introspection (binauralEqGainDbForTest).
void SpatialEngine::applyBinauralEq(const util::QueuedCmd& qc) noexcept {
    using Op = ipc::PayloadSysBinauralEq::Op;
    switch (static_cast<Op>(qc.bin_eq_op)) {
    case Op::Enable:
        binaural_eq_active_.store(qc.bin_eq_enable, std::memory_order_relaxed);
        break;
    case Op::Band: {
        const int b = qc.bin_eq_band;
        if (b < 0 || b >= iae::kBinauralEqBands) break;  // ignore out-of-range band
        const size_t bi = static_cast<size_t>(b);
        // Clamp next to the DSP (same philosophy as applyRoomCtl). Keep the
        // corner strictly inside (0, Nyquist); the ±24 dB gain clamp guarantees
        // setPeak's A = sqrt(gain) stays well clear of its 1e-6 floor.
        const float fnyq = static_cast<float>(0.45 * sample_rate_);
        const float f = std::clamp(qc.bin_eq_freq_hz, 10.f, std::max(20.f, fnyq));
        const float g = std::clamp(qc.bin_eq_gain_db, -24.f, 24.f);
        const float q = std::clamp(qc.bin_eq_q, 0.1f, 10.f);
        bin_eq_freq_[bi]    = f;
        bin_eq_gain_db_[bi] = g;
        bin_eq_q_[bi]       = q;
        bin_eq_L_[bi].setPeak(sample_rate_, f, q, g);
        bin_eq_R_[bi].setPeak(sample_rate_, f, q, g);
        break;
    }
    }
}

void SpatialEngine::computeLateFdnGains(const iae::Vec3& opp, float lateDiffuse01,
                                        int n_spk) noexcept {
    for (int k = 0; k < iae::RoomFdn::kOrder; ++k) {
        const iae::Vec3 u = lateFdnLineDirection(k, opp);
        const float az = std::atan2(u.x, u.z);
        const float el = std::asin(std::clamp(u.y, -1.f, 1.f));
        fdn_line_gains_[static_cast<size_t>(k)].fill(0.f);
        render::AlgorithmAnalyticReference::vbap_gain_into(
            layout_, az, el,
            fdn_line_gains_[static_cast<size_t>(k)].data(), spe::MAX_SPEAKERS);
        iae::blendVbapWithUniformDiffuse(
            fdn_line_gains_[static_cast<size_t>(k)].data(), n_spk, lateDiffuse01);
    }
}

void SpatialEngine::audioBlock(const spe::audio_io::AudioBlock& block) {
    SPE_RT_NO_ALLOC_SCOPE();

    // FTZ/DAZ is per-thread; the control thread setting it in prepareToPlay does
    // NOT cover this audio thread. Set it here at the callback entry so reverb /
    // allpass / FDN tails never stall on denormals (10-100x slowdown / peak
    // spike). Cheap, idempotent, sticky for the thread. (v1.0 Phase 1.1)
    spe::util::enableDenormalFlush();

    if (block.num_frames > spe::MAX_BLOCK) {
        internal_xruns_.record_overrun();
        return;
    }

    // v0.9 Lane A (A-M1) — stamp block-entry AFTER the overrun guard so a
    // refused (oversized) block never enters the wall-time sample stream.
    // Clock read only (alloc/lock/syscall-free; vDSO on Linux).
    cpu_meter_.recordBlockStart();

    // C1.d — LTC chase tap. When chase is enabled and the backend supplies
    // an input ch 0, push that block's samples into the LtcChase ring.
    // Allocation-free (SpscRing<float>::push). Decoder runs on the control
    // thread via SpatialEngine::updateLtcChase().
    if (ltc_chase_enable_.load(std::memory_order_relaxed)
        && block.input_channels != nullptr
        && block.input_channel_count > 0
        && block.input_channels[0] != nullptr) {
        ltc_chase_.pushSamples(block.input_channels[0], block.num_frames);
    }

    // Drain OSC command FIFO directly into obj_cache_ (RT-safe, no seq issues)
    {
        // F4b: any successful pop dirties the block → triggers the post-drain
        // snapshot publish below. Coarse on purpose (A3): over-publish is
        // harmless; under-publish (a missed mutation → stale save) is the
        // dangerous mode and is eliminated by marking dirty on EVERY pop.
        bool cache_dirty = false;
        util::QueuedCmd qc;
        while (cmd_fifo_.pop(qc)) {
            cache_dirty = true;  // before the OOB continue, so no mutation is missed
            if (qc.obj_id >= static_cast<uint32_t>(MAX_OBJECTS)) continue;
            auto& c = obj_cache_[qc.obj_id];
            switch (qc.tag) {
            case ipc::CommandTag::ObjMove:
                c.az = qc.az_rad; c.el = qc.el_rad;
                c.dist = qc.dist_m; c.active = true;
                break;
            case ipc::CommandTag::ObjGain:
                c.gain_lin = qc.gain;
                break;
            case ipc::CommandTag::ObjActive:
                c.active = qc.active;
                break;
            case ipc::CommandTag::ObjAlgo:
                c.algo = qc.algo;
                break;
            case ipc::CommandTag::SysReset:
                obj_cache_.fill(ObjCache{});
                break;
            case ipc::CommandTag::NoiseType: {
                // v0.3.1: wire channel is 1-based YAML channel. Translate via
                // SpeakerLayout::channelToIndex() to the vector position; drop
                // silently on unmapped channels (channelToIndex returns -1).
                const int idx = layout_.channelToIndex(static_cast<int>(qc.noise_ch));
                if (idx >= 0 && static_cast<size_t>(idx) < noise_chans_.size()) {
                    auto& nc = noise_chans_[static_cast<size_t>(idx)];
                    if (qc.noise_mode != nc.mode) {  // fresh state on mode change
                        if (qc.noise_mode == 1) nc.pink_filt.reset();
                        if (qc.noise_mode == 2) nc.sweep_gen.reset();
                    }
                    nc.mode = qc.noise_mode;
                }
                break;
            }
            case ipc::CommandTag::NoiseGain: {
                const int idx = layout_.channelToIndex(static_cast<int>(qc.noise_ch));
                if (idx >= 0 && static_cast<size_t>(idx) < noise_chans_.size()) {
                    // -60 dB ≈ silent floor; convert dB → linear
                    const float g = (qc.noise_gain_db <= -60.f)
                        ? 0.f
                        : std::pow(10.f, qc.noise_gain_db / 20.f);
                    noise_chans_[static_cast<size_t>(idx)].gain_lin = g;
                }
                break;
            }
            case ipc::CommandTag::NoiseSource: {
                const int idx = layout_.channelToIndex(static_cast<int>(qc.noise_ch));
                if (idx >= 0 && static_cast<size_t>(idx) < noise_chans_.size()) {
                    noise_chans_[static_cast<size_t>(idx)].in_src = qc.noise_source;
                }
                break;
            }
            case ipc::CommandTag::TransportPlay:
                transport_play_.store(true, std::memory_order_relaxed);
                break;
            case ipc::CommandTag::TransportStop:
                transport_play_.store(false, std::memory_order_relaxed);
                break;
            case ipc::CommandTag::ReverbSelect: {
                const int which = static_cast<int>(qc.reverb_which);
                const int prev  = active_reverb_.exchange(which,
                                     std::memory_order_relaxed);
                // ⑥b/⑥d review (MEDIUM): the room late FDN and the per-object
                // early-reflection rings are frozen while another mode is active,
                // so on switching INTO room mode they would resume from a stale
                // tail. Clear them for a clean onset — RT-safe (fills + biquad
                // reset, no allocation). Shared with /room/enable via the helper.
                if (which == 2 && prev != 2) resetRoomState();
                break;
            }
            case ipc::CommandTag::RoomCtl:
                applyRoomCtl(qc);
                break;
            case ipc::CommandTag::DecorrCtl:
                applyDecorrCtl(qc);
                break;
            case ipc::CommandTag::SysBinauralEq:
                applyBinauralEq(qc);
                break;
            case ipc::CommandTag::SysAmbiOrder:
                ambisonic_.setOrder(static_cast<int>(qc.ambi_order));
                break;
            case ipc::CommandTag::SysAmbiDecoderType:
                ambisonic_.setDecoderType(static_cast<int>(qc.ambi_decoder_type));
                break;
            case ipc::CommandTag::SysLtcChase:
                ltc_chase_enable_.store(qc.ltc_chase_enable != 0,
                                        std::memory_order_relaxed);
                break;
            case ipc::CommandTag::OutputGain: {
                // v0.3.1: route by YAML channel via channel_to_idx_. Drop the
                // command silently on unmapped channels — audio-thread cannot
                // allocate/log without breaking RT contract.
                const int idx = layout_.channelToIndex(static_cast<int>(qc.output_ch));
                if (idx >= 0 && static_cast<size_t>(idx) < spk_gain_lin_.size()) {
                    spk_gain_lin_[static_cast<size_t>(idx)] =
                        std::pow(10.f, qc.output_value_db / 20.f);
                }
                break;
            }
            case ipc::CommandTag::OutputLimit: {
                const int idx = layout_.channelToIndex(static_cast<int>(qc.output_ch));
                if (idx >= 0 && static_cast<size_t>(idx) < spk_limiters_.size()) {
                    spk_limiters_[static_cast<size_t>(idx)].setThreshold(
                        std::pow(10.f, qc.output_value_db / 20.f));
                }
                break;
            }
            case ipc::CommandTag::ObjDsp:
                switch (qc.dsp_param) {
                case 0: c.eq_gain_db[0] = qc.dsp_value; break;
                case 1: c.eq_gain_db[1] = qc.dsp_value; break;
                case 2: c.eq_gain_db[2] = qc.dsp_value; break;
                case 3: c.eq_gain_db[3] = qc.dsp_value; break;
                case 4: c.user_delay_ms = qc.dsp_value; break;
                case 5: c.k_hf          = qc.dsp_value; break;
                case 6: c.reverb_send   = qc.dsp_value; break;
                case 7: c.width_rad     = qc.dsp_value; break;
                default: break;
                }
                break;
            // Phase C3 ADM-OSC v1.0 extended — all via obj_cache_ (ADR 0006)
            case ipc::CommandTag::ObjMute:
                c.active = qc.active; // muted → active=false
                break;
            case ipc::CommandTag::ObjActiveAdm:
                c.active = qc.active;
                break;
            case ipc::CommandTag::ObjXYZ:
                // Phase 3.2 — ADM-OSC Cartesian. The wire frame is
                // x=right, y=FRONT, z=UP (normalised [-1,1]); the engine frame is
                // x=right, y=UP, z=FRONT. So az is atan2(right, front)=atan2(x,y)
                // and el is asin(up/r)=asin(z/r) — i.e. a Y<->Z swap vs the naive
                // reading. This makes an ADM source given via xyz land at the SAME
                // engine az/el as the equivalent /aed (verified by the az golden +
                // the xyz golden test). Distance: the normalised vector length
                // scales to metres via the ADM Cartesian half-span.
                {
                    float r = std::sqrt(qc.xyz_x * qc.xyz_x +
                                        qc.xyz_y * qc.xyz_y +
                                        qc.xyz_z * qc.xyz_z);
                    if (r > 1e-6f) {
                        c.az   = std::atan2(qc.xyz_x, qc.xyz_y);  // right, front
                        c.el   = std::asin(qc.xyz_z / r);         // up
                        c.dist = r * spe::ipc::ADM_OSC_CARTESIAN_HALF_SPAN;
                    }
                    c.active = true;
                }
                break;
            case ipc::CommandTag::ObjWidth:
                c.width_rad = qc.width_rad;
                break;
            case ipc::CommandTag::ObjName:
                // Name stored in ObjCache if the field exists; otherwise no-op.
                // (ObjCache does not currently have a name field — this is a
                //  Phase C3 stub; the name is decoded and forwarded but not
                //  yet rendered. Phase C4 can surface it in /sys/state.)
                break;
            default: break;
            }
        }

        // F4b publish — post-drain (so SysReset's obj_cache_.fill() above is
        // captured), iff the block was dirty. RT-safe: one fixed O(MAX_OBJECTS)
        // copy into the writer-owned back buffer + a single CAS, no alloc/lock/
        // syscall, and only on dirty blocks. No extra RT-assert needed:
        // audioBlock already opened SPE_RT_NO_ALLOC_SCOPE() at entry, so this is
        // CI-verified no-alloc under -DSPATIAL_ENGINE_RT_ASSERTS.
        //
        // Reader-claim publish: pick a buffer that is NEITHER the currently-
        // published one NOR the one the reader has claimed busy, copy into it,
        // then release-publish its index. With 3 buffers at most two are
        // forbidden, so a free third always exists. Safety rests on the writer
        // observing the reader's `busy` claim within ~1 publish cadence (reader
        // copy << audio-block period); it is a liveness property, not a hard
        // by-construction bound (see the SpatialEngine.h note), and is gated by
        // soak_scene_save_race (AC9) under TSan.
        if (cache_dirty) {
            const int pub  = snap_published_idx_.load(std::memory_order_relaxed);
            const int busy = snap_reader_busy_idx_.load(std::memory_order_acquire);
            int w = 0;
            while (w == pub || w == busy) ++w;  // 0..2; at most two forbidden
            snap_buf_[static_cast<std::size_t>(w)] = obj_cache_;  // full consistent copy
            snap_published_idx_.store(w, std::memory_order_release);
        }
    }

    // Generate per-object sine tones (RT-safe: no alloc, uses std::sin)
    const float dt = 1.0f / static_cast<float>(sample_rate_);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        auto& scratch = dry_scratch_[static_cast<size_t>(i)];
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        if (!c.active) {
            std::fill(scratch.begin(), scratch.begin() + block.num_frames, 0.0f);
            continue;
        }
        // Unique frequency per object: 110, 165, 220, 275, 330 ... Hz
        float freq  = 110.0f * (1 + static_cast<float>(i) * 0.5f);
        float omega = 2.0f * static_cast<float>(M_PI) * freq * dt;
        float phase = osc_phases_[static_cast<size_t>(i)];
        for (int n = 0; n < block.num_frames; ++n) {
            scratch[static_cast<size_t>(n)] = 0.25f * std::sin(phase);
            phase += omega;
        }
        // Wrap phase to avoid float drift
        while (phase > 2.0f * static_cast<float>(M_PI)) phase -= 2.0f * static_cast<float>(M_PI);
        osc_phases_[static_cast<size_t>(i)] = phase;
    }

    // Per-object DSP chain (in-place on dry_scratch_) + accumulate reverb send
    std::fill(reverb_send_buf_.begin(),
              reverb_send_buf_.begin() + block.num_frames, 0.0f);
    // ⑥f — clear the dedicated room late bus (per-object lateMul-scaled send).
    std::fill(room_late_send_buf_.begin(),
              room_late_send_buf_.begin() + block.num_frames, 0.0f);

    // ⑥e (late opp source-bias) — accumulate the late-reverb source-energy
    // centroid for this block: each object weights its unit position by the
    // magnitude of its reverb send, split by WFS vs non-WFS for the diffuse
    // amount. Consumed below to steer the late FDN lines (RoomEngine.cpp:491-503).
    // Per-block locals → fresh each block (faithful to the reference reset).
    iae::Vec3 late_w_sum{};
    float     late_w_denom = 0.f;
    float     wet_wfs      = 0.f;
    float     wet_nonwfs   = 0.f;

    const float transport_gain = transport_play_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        if (!c.active) continue;

        spe::dsp::PerObjectChainParams p;
        p.dist_m         = c.dist;
        p.k_hf           = c.k_hf;
        p.reverb_send    = c.reverb_send;
        p.gain_lin       = c.gain_lin * transport_gain;
        p.user_delay_ms  = c.user_delay_ms;
        p.eq.gain_db[0]  = c.eq_gain_db[0];
        p.eq.gain_db[1]  = c.eq_gain_db[1];
        p.eq.gain_db[2]  = c.eq_gain_db[2];
        p.eq.gain_db[3]  = c.eq_gain_db[3];
        chains_[static_cast<size_t>(i)].setParams(p);

        auto& scratch = dry_scratch_[static_cast<size_t>(i)];
        float obj_send_abs = 0.f;
        // ⑥f — this object's late-bus distance multiplier (reference lateDistMul,
        // AudioEngine.cpp:830). Scales only the dedicated room late bus; the
        // shared reverb_send_buf_ (non-room FDN/IR) stays un-scaled.
        const float lateMul = iae::roomDistanceGainDbLinear(
            c.dist, room_dist_near_m_, room_dist_far_m_,
            room_late_gain_close_db_, room_late_gain_far_db_, room_dist_linearity01_);
        for (int n = 0; n < block.num_frames; ++n) {
            float reverb_tap = 0.f;
            scratch[static_cast<size_t>(n)] =
                chains_[static_cast<size_t>(i)].processSample(
                    scratch[static_cast<size_t>(n)], reverb_tap);
            reverb_send_buf_[static_cast<size_t>(n)] += reverb_tap;
            room_late_send_buf_[static_cast<size_t>(n)] += reverb_tap * lateMul;
            obj_send_abs += std::fabs(reverb_tap);
        }

        // ⑥e — weight this object's unit position by its late-send energy. The
        // position formula matches renderRoomEarly (mmhoa frame x=right,y=up,
        // z=front); srcAxis = normalized(pos) mirrors the reference p/dDirect.
        if (obj_send_abs > 1.0e-12f) {
            const float ce = std::cos(c.el);
            const iae::Vec3 pos{ c.dist * ce * std::sin(c.az),
                                 c.dist * std::sin(c.el),
                                 c.dist * ce * std::cos(c.az) };
            const iae::Vec3 srcAxis = iae::normalized(pos);
            late_w_sum   = late_w_sum + srcAxis * obj_send_abs;
            late_w_denom += obj_send_abs;
            if (c.algo == ipc::Algorithm::WFS) wet_wfs    += obj_send_abs;
            else                               wet_nonwfs += obj_send_abs;
        }
    }

    // Reverb: mono in-place. Copy send → wet, then process in place.
    std::copy(reverb_send_buf_.begin(),
              reverb_send_buf_.begin() + block.num_frames,
              reverb_wet_buf_.begin());
    {
        const int rev = active_reverb_.load(std::memory_order_relaxed);
        if (rev == 1 && ir_reverb_) {
            ir_reverb_->process(reverb_wet_buf_.data(), block.num_frames);
        } else if (rev == 2) {
            // Room (spatial): the late FDN runs in the spatial-distribution stage
            // below, reading the dry send (reverb_send_buf_) directly. The mono
            // reverb_wet_buf_ is left untouched (not distributed for this mode).
        } else {
            fdn_.process(reverb_wet_buf_.data(), block.num_frames);
        }
    }

    // v1.0 Phase 1.4b — per-stage audio-thread timing. steady_clock is vDSO on
    // Linux (cheap); 0 when a stage does not run this block. Folded into
    // obs_counters_ at block end for the 1 Hz /sys/metrics tick.
    double stage_render_us = 0.0, stage_room_us = 0.0,
           stage_decorr_us = 0.0, stage_binaural_us = 0.0;

    // Per-algorithm spatial render (each renderer reads dry_ptrs_,
    // sees only its own algorithm's objects via the masked spans below).
    if (render_ready_.load(std::memory_order_relaxed) && block.output_channel_count > 0) {
        const int n_spk = vbap_.numSpeakers();
        const auto _ts_render0 = std::chrono::steady_clock::now();

        // Phase 1.2 — per-algorithm active-object compaction. Count active
        // objects per algorithm and only fill/process/sum the renderers that
        // have >=1 active object. This is BIT-EXACT vs. always running all five:
        // every renderer produces all-zero output when it has 0 active objects
        // (VBAP/DBAP/VAP/WFS memset the bus then skip inactive objects; the
        // Ambisonic decode is a pure stateless matmul of zeroed SH buffers), and
        // none of them advance per-object state (gain ramps / WFS delay lines /
        // decoder) for INACTIVE objects — so a skipped renderer would have added
        // exactly +0.0f (a + 0.0f == a) and its ramp/delay state on the next
        // block is identical whether or not processBlock ran this block.
        //
        // v0.9 Lane C (hoist): the *_objs_ scratch arrays are engine members
        // (off the audio-thread stack). A running renderer's array is cleared
        // (fill) then populated with ITS active objects only; a skipped
        // renderer's array/scratch keeps stale data but is never read.
        int n_act[5] = {0, 0, 0, 0, 0};  // index = Algorithm enum value
        // n_act/ran are sized for algorithm enum values 0..4. If a new algorithm
        // is added, grow these arrays — otherwise the index below overflows.
        static_assert(static_cast<int>(ipc::Algorithm::VAP) == 4,
                      "n_act[5]/ran[5] assume Algorithm enum values 0..4");
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            const auto& c = obj_cache_[static_cast<size_t>(i)];
            if (!c.active) continue;
            ++n_act[static_cast<size_t>(static_cast<uint8_t>(c.algo))];
        }
        const bool run_vbap = n_act[static_cast<int>(ipc::Algorithm::VBAP)]      > 0;
        const bool run_wfs  = n_act[static_cast<int>(ipc::Algorithm::WFS)]       > 0;
        const bool run_dbap = n_act[static_cast<int>(ipc::Algorithm::DBAP)]      > 0;
        const bool run_ambi = n_act[static_cast<int>(ipc::Algorithm::Ambisonic)] > 0;
        const bool run_vap  = n_act[static_cast<int>(ipc::Algorithm::VAP)]       > 0;

        if (run_vbap) vbap_objs_.fill(render::ObjectState{});
        if (run_dbap) dbap_objs_.fill(render::ObjectState{});
        if (run_wfs)  wfs_objs_.fill(render::ObjectState{});
        if (run_ambi) ambisonic_objs_.fill(render::ObjectState{});
        if (run_vap)  vap_objs_.fill(render::ObjectState{});
        // Populate only ACTIVE objects (inactive slots stay default-inactive and
        // are skipped by every renderer — identical output to writing them).
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            const auto& c = obj_cache_[static_cast<size_t>(i)];
            if (!c.active) continue;
            const render::ObjectState s = {c.az, c.el, c.dist, c.active, c.width_rad};
            switch (c.algo) {
            case ipc::Algorithm::WFS:       wfs_objs_[i]       = s; break;
            case ipc::Algorithm::DBAP:      dbap_objs_[i]      = s; break;
            case ipc::Algorithm::Ambisonic: ambisonic_objs_[i] = s; break;
            case ipc::Algorithm::VAP:       vap_objs_[i]       = s; break;
            case ipc::Algorithm::VBAP:
            default:                        vbap_objs_[i]      = s; break;
            }
        }

        if (run_vbap) vbap_.processBlock(
            std::span<const render::ObjectState>(vbap_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            vbap_scratch_.data(), block.num_frames);
        if (run_dbap) dbap_.processBlock(
            std::span<const render::ObjectState>(dbap_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            dbap_scratch_.data(), block.num_frames);
        if (run_wfs) wfs_.processBlock(
            std::span<const render::ObjectState>(wfs_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            wfs_scratch_.data(), block.num_frames);
        if (run_ambi) ambisonic_.processBlock(
            std::span<const render::ObjectState>(ambisonic_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            ambisonic_scratch_.data(), block.num_frames);
        if (run_vap) vap_.processBlock(
            std::span<const render::ObjectState>(vap_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            vap_scratch_.data(), block.num_frames);

        // Sum only the scratches of renderers that ran, in the original
        // VBAP→DBAP→WFS→Ambisonic→VAP order (left-assoc, so the all-run case is
        // bit-identical to the prior fixed 5-way sum; skipped renderers' +0.0f
        // terms are dropped, which is exact).
        const int total = n_spk * block.num_frames;
        const float* ran[5];
        int n_ran = 0;
        if (run_vbap) ran[n_ran++] = vbap_scratch_.data();
        if (run_dbap) ran[n_ran++] = dbap_scratch_.data();
        if (run_wfs)  ran[n_ran++] = wfs_scratch_.data();
        if (run_ambi) ran[n_ran++] = ambisonic_scratch_.data();
        if (run_vap)  ran[n_ran++] = vap_scratch_.data();
        if (n_ran == 0) {
            std::fill(mix_buf_.begin(),
                      mix_buf_.begin() + total, 0.0f);
        } else {
            for (int idx = 0; idx < total; ++idx) {
                float acc = ran[0][idx];
                for (int r = 1; r < n_ran; ++r) acc += ran[r][idx];
                mix_buf_[static_cast<size_t>(idx)] = acc;
            }
        }

        const auto _ts_render1 = std::chrono::steady_clock::now();
        stage_render_us = std::chrono::duration_cast<std::chrono::nanoseconds>(
            _ts_render1 - _ts_render0).count() * 1e-3;

        const int rev_dist = active_reverb_.load(std::memory_order_relaxed);
        if (rev_dist == 2 && room_ready_) {
            // ⑥e (late opp source-bias) — steer this block's late FDN line
            // directions toward the axis opposite the source-energy centroid, and
            // set the diffuse amount from the WFS energy fraction. Byte-faithful to
            // RoomEngine.cpp:535-583. All speakers participate in mmhoa, so the
            // reference degenerate (spatialCount<1) path cannot trigger here.
            float lateDiffuse = iae::kLateDiffuseMin;
            const float wetTot = wet_wfs + wet_nonwfs;
            if (wetTot > 1.0e-9f)
                lateDiffuse = iae::kLateDiffuseMin
                    + (wet_wfs / wetTot) * (iae::kLateDiffuseMax - iae::kLateDiffuseMin);
            iae::Vec3 opp{ 0.f, 1.f, 0.f };
            if (late_w_denom > 1.0e-9f) {
                iae::Vec3 avg = late_w_sum * (1.f / late_w_denom);
                avg = iae::normalized(avg);
                opp = iae::normalized(avg * (-1.f));
            }
            computeLateFdnGains(opp, lateDiffuse, n_spk);

            // ⑥b Room (spatial): run the late FDN on the mono send, then fan each
            // of the 8 line taps across the bus via its (opp-biased) gain vectors.
            // kLatePerLineGain matches the reference (RoomEngine.cpp:691).
            constexpr float kLatePerLineGain = 0.068f;
            // ⑥e-4-A Phase-5 — run the mono late send through the late-bus
            // absorption EQ (HP→LP) before the FDN. Byte-faithful to
            // RoomEngine.cpp:650-658 (lateBusHp.processSample → lateBusLp). The
            // early and cluster paths read their own un-late-EQ'd taps, so this
            // shapes ONLY the late FDN tail. ⑥f — the source is the dedicated
            // room late bus (per-object lateMul-scaled), not the shared send.
            for (int n = 0; n < block.num_frames; ++n) {
                const float xl = late_eq_hp_.processSample(
                    room_late_send_buf_[static_cast<size_t>(n)]);
                late_in_buf_[static_cast<size_t>(n)] = late_eq_lp_.processSample(xl);
            }
            room_fdn_.process(late_in_buf_.data(), block.num_frames,
                              room_lines_.data());
            for (int k = 0; k < iae::RoomFdn::kOrder; ++k) {
                const float* line =
                    room_lines_.data() + static_cast<size_t>(k) * block.num_frames;
                const float* g = fdn_line_gains_[static_cast<size_t>(k)].data();
                for (int n = 0; n < block.num_frames; ++n) {
                    const float t = line[n] * kLatePerLineGain;
                    if (std::abs(t) < 1.0e-9f) continue;
                    for (int s = 0; s < n_spk; ++s)
                        mix_buf_[static_cast<size_t>(n * n_spk + s)] += t * g[s];
                }
            }
            // ⑥d — per-object Shoebox early reflections onto the same bus. This
            // also fills cluster_bus_ with each object's xdel*cSend (⑥e-2).
            renderRoomEarly(n_spk, block.num_frames);

            // ⑥e-2 cluster — run the shared cluster bus through its absorption EQ
            // (HP120→LP10000) and the 6-tap feedforward diffusion line, then fan
            // the mono cluster output across the array via the opp-biased clusterU
            // gains (slight +up component). Byte-faithful to RoomEngine.cpp:
            // 553-565 (clusterU + diffuseGains) and :594-647 (bus EQ, process,
            // per-speaker distribution). RT-safe: cluster line + buffers are
            // prepare-allocated; EQ/VBAP/blend use stack scratch.
            for (int n = 0; n < block.num_frames; ++n) {
                const float xc = cluster_eq_hp_.processSample(
                    cluster_bus_[static_cast<size_t>(n)]);
                cluster_bus_[static_cast<size_t>(n)] = cluster_eq_lp_.processSample(xc);
            }
            room_cluster_.process(cluster_bus_.data(), block.num_frames,
                                  cluster_out_.data());
            const iae::Vec3 clusterU = iae::normalized(iae::Vec3{
                opp.x * 0.93f, opp.y * 0.93f + 0.35f * 0.07f, opp.z * 0.93f });
            const float caz = std::atan2(clusterU.x, clusterU.z);
            const float cel = std::asin(std::clamp(clusterU.y, -1.f, 1.f));
            cluster_gains_.fill(0.f);
            render::AlgorithmAnalyticReference::vbap_gain_into(
                layout_, caz, cel, cluster_gains_.data(), spe::MAX_SPEAKERS);
            iae::blendVbapWithUniformDiffuse(cluster_gains_.data(), n_spk, lateDiffuse);
            for (int n = 0; n < block.num_frames; ++n) {
                const float outC = cluster_out_[static_cast<size_t>(n)];
                if (std::abs(outC) < 1.0e-9f) continue;
                for (int s = 0; s < n_spk; ++s)
                    mix_buf_[static_cast<size_t>(n * n_spk + s)] +=
                        outC * cluster_gains_[static_cast<size_t>(s)];
            }
        } else {
            // Distribute reverb wet uniformly across all speakers (energy-preserving)
            const float reverb_per_spk = 1.0f / std::sqrt(static_cast<float>(n_spk));
            for (int n = 0; n < block.num_frames; ++n) {
                const float wet = reverb_wet_buf_[static_cast<size_t>(n)] * reverb_per_spk;
                for (int s = 0; s < n_spk; ++s) {
                    mix_buf_[static_cast<size_t>(n * n_spk + s)] += wet;
                }
            }
        }

        stage_room_us = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - _ts_render1).count() * 1e-3;

        // Deinterleave mix_buf_ → planar output_channels (speaker bus)
        // Apply per-speaker delay (DelayLine) and gain during deinterleave.
        const int out_ch = std::min(block.output_channel_count, n_spk);
        for (int spk = 0; spk < out_ch; ++spk) {
            if (block.output_channels && block.output_channels[spk]) {
                const float g = (spk < static_cast<int>(spk_gain_lin_.size()))
                    ? spk_gain_lin_[static_cast<size_t>(spk)] : 1.f;
                const float d = (spk < static_cast<int>(spk_delay_samples_.size()))
                    ? spk_delay_samples_[static_cast<size_t>(spk)] : 0.f;
                for (int n = 0; n < block.num_frames; ++n) {
                    float s = mix_buf_[static_cast<size_t>(n * n_spk + spk)] * g;
                    if (spk < static_cast<int>(spk_delays_.size())) {
                        s = spk_delays_[static_cast<size_t>(spk)].processSample(s, d);
                    }
                    block.output_channels[spk][n] = s;
                }
            }
        }

        // ⑦ — per-speaker decorrelation on the output bus, after the per-speaker
        // gain/delay deinterleave (faithful to the reference, which decorrelates
        // after speaker gains). In place on each planar channel; the bank no-ops
        // when disabled / mix≈0. RT-safe (rings prepare-allocated, lazy reconfig).
        if (decorr_enabled_ && decorr_mix01_ > 1.0e-5f) {
            const auto _ts_decorr0 = std::chrono::steady_clock::now();
            for (int spk = 0; spk < out_ch; ++spk) {
                if (!block.output_channels || !block.output_channels[spk]) continue;
                decorr_bank_.processChannel(spk, block.output_channels[spk],
                                            block.num_frames, decorr_enabled_,
                                            decorr_mix01_, decorr_stages_,
                                            decorr_spread_ms_, decorr_ap_,
                                            decorr_seed_);
            }
            stage_decorr_us = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - _ts_decorr0).count() * 1e-3;
        }

        // Per-channel noise generator (array verification): adds white/pink/
        // sweep calibration signal scaled by gain_lin to the speaker bus.
        // Bypasses VBAP/reverb; lets an engineer confirm each speaker's wiring.
        for (int spk = 0; spk < out_ch; ++spk) {
            if (spk >= static_cast<int>(noise_chans_.size())) break;
            auto& nc = noise_chans_[static_cast<size_t>(spk)];
            if (nc.gain_lin <= 0.f) continue;
            if (!block.output_channels || !block.output_channels[spk]) continue;

            constexpr float kNoiseScale = 1.0f / 2147483648.f;  // int32 → ±1 float
            // mode 3 = passthrough: route input channel in_src to this speaker
            // (engineer wiring check). Resolve the input pointer once per block;
            // null when no input backend / out-of-range source → emits silence.
            const float* in_ptr = nullptr;
            if (nc.mode == 3 && block.input_channels && nc.in_src >= 0 &&
                nc.in_src < block.input_channel_count) {
                in_ptr = block.input_channels[nc.in_src];
            }
            for (int n = 0; n < block.num_frames; ++n) {
                // xorshift32 RNG (RT-safe, deterministic)
                nc.rng ^= nc.rng << 13;
                nc.rng ^= nc.rng >> 17;
                nc.rng ^= nc.rng << 5;
                const float white = static_cast<float>(static_cast<int32_t>(nc.rng)) * kNoiseScale;
                // mode 0 = white, 1 = pink (canonical Kellet −3 dB/oct, see
                // dsp/PinkNoise.h), 2 = log-sweep 20→20k (see dsp/LogSweep.h),
                // 3 = input passthrough (in_ptr, silence if unavailable).
                float sample;
                if      (nc.mode == 1) sample = nc.pink_filt.processSample(white);
                else if (nc.mode == 2) sample = nc.sweep_gen.processSample(sample_rate_);
                else if (nc.mode == 3) sample = in_ptr ? in_ptr[n] : 0.f;
                else                   sample = white;
                block.output_channels[spk][n] += sample * nc.gain_lin * transport_gain;
            }
        }

        // Per-channel limiter (applied after all summing: spatial + reverb + noise)
        for (int spk = 0; spk < out_ch; ++spk) {
            if (!block.output_channels || !block.output_channels[spk]) continue;
            if (spk >= static_cast<int>(spk_limiters_.size())) break;
            auto& lim = spk_limiters_[static_cast<size_t>(spk)];
            for (int n = 0; n < block.num_frames; ++n) {
                block.output_channels[spk][n] = lim.processSample(block.output_channels[spk][n]);
            }
        }

        // v0.5 P3: B1 per-object HRTF summation. Always render into the
        // internal binaural_l_buf_/binaural_r_buf_ when an HRTF is loaded;
        // VST3 wiring (and the legacy speaker-bus-tail path below) read those
        // buffers via binauralL()/binauralR().
        // v0.5 P4 fix (M2): gate the entire binaural processing block on
        // binaural_enabled_. Pre-P4 the B1 path also ignored this flag; under
        // B2 the expensive 16ch SH+24-conv fan-out would otherwise run while
        // the user thinks binaural is off. Buffers stay zeroed when disabled.
        const auto _ts_binaural0 = std::chrono::steady_clock::now();
        const bool binaural_enabled_now =
            binaural_enabled_.load(std::memory_order_acquire);
        if (binaural_ok_ && binaural_enabled_now) {
            std::fill(binaural_l_buf_.begin(),
                      binaural_l_buf_.begin() + block.num_frames, 0.f);
            std::fill(binaural_r_buf_.begin(),
                      binaural_r_buf_.begin() + block.num_frames, 0.f);

            if (binaural_.hasHrtf()) {
                // C1 fix: snapshot effectiveMode() exactly once per block via
                // BinauralMonitor's xfade arm path. A second read inside
                // processBlockB2 used to race with an OSC mode flip mid-block,
                // producing a silent gap. The snapshot is the dispatch
                // authority.
                //
                // v0.5.1 Q2 (A3): observeAndArmXfade() returns the per-block
                // ramp descriptor. When step.active, we render BOTH branches
                // (outgoing + incoming) into their dedicated scratch buffers
                // and envelope-mix into binaural_*_buf_. When !step.active,
                // we render only step.steady's branch as before.
                const auto step = binaural_.observeAndArmXfade();

                // Phase 2.6b — binaural head tracking. Read the head pose once
                // per block (relaxed atomics, degrees → radians) and apply it
                // to each object's direction before the B1 HRTF lookup below.
                // Single read here (not per-object, not per-branch) keeps the
                // pose coherent across the whole block and the crossfade pair.
                static constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;
                const float head_yaw_rad   = head_yaw_deg_.load(std::memory_order_relaxed)   * kDeg2Rad;
                const float head_pitch_rad = head_pitch_deg_.load(std::memory_order_relaxed) * kDeg2Rad;
                const float head_roll_rad  = head_roll_deg_.load(std::memory_order_relaxed)  * kDeg2Rad;

                // Phase 2.1 — binaural HRTF prefeed low-pass. Filter each active
                // object's dry signal ONCE per block into bin_prefeed_; the B1
                // and B2 branches both read bin_prefeed_ as the HRTF input. Doing
                // it here (before render_branch, which runs twice during an HRTF
                // crossfade) keeps the one-pole state advancing exactly once per
                // block. Coeff a = 1 - exp(-2π·fc/sr) (BinauralMonitorChain.cpp:
                // 107-109); the cutoff is read once per block (relaxed atomic)
                // and clamped to (0, Nyquist) — a corner ≥ Nyquist ⇒ a ≈ 1 ⇒
                // passthrough. The active-object predicate matches both branches.
                {
                    const float fnyq = static_cast<float>(0.499 * sample_rate_);
                    const float fc = std::clamp(
                        bin_prefeed_cutoff_hz_.load(std::memory_order_relaxed),
                        1.f, fnyq);
                    const float a = 1.f - std::exp(
                        -2.f * static_cast<float>(M_PI) * fc
                        / static_cast<float>(sample_rate_));
                    for (int i = 0; i < MAX_OBJECTS; ++i) {
                        const auto& c = obj_cache_[static_cast<std::size_t>(i)];
                        if (!c.active || c.gain_lin < 1e-6f) continue;
                        const float* dry =
                            dry_scratch_[static_cast<std::size_t>(i)].data();
                        float* pf = bin_prefeed_[static_cast<std::size_t>(i)].data();
                        float z = bin_prefeed_lp_[static_cast<std::size_t>(i)];
                        for (int n = 0; n < block.num_frames; ++n) {
                            z += a * (dry[n] - z);
                            pf[n] = z;
                        }
                        bin_prefeed_lp_[static_cast<std::size_t>(i)] = z;
                    }
                }

                // Lambda: render one branch (Direct or AmbiVS) into dstL/dstR.
                // No-alloc, audio-thread safe. Branch selection is by enum.
                auto render_branch = [&](output::BinauralMode mode,
                                         float* dstL, float* dstR) noexcept {
                    if (mode == output::BinauralMode::AmbiVS) {
                        // v0.5 P4: B2 AmbiVS path. Encode each active object
                        // into 3rd-order ACN-ordered SH (16 channels), sum
                        // into b2_sh_scratch_, then decode→24-VS→HRIR→L/R.
                        for (int k = 0; k < 16; ++k) {
                            std::fill(b2_sh_scratch_[static_cast<std::size_t>(k)].begin(),
                                      b2_sh_scratch_[static_cast<std::size_t>(k)].begin()
                                      + block.num_frames, 0.f);
                        }
                        for (int i = 0; i < MAX_OBJECTS; ++i) {
                            const auto& c = obj_cache_[static_cast<std::size_t>(i)];
                            if (!c.active || c.gain_lin < 1e-6f) continue;
                            const auto coeffs =
                                spe::ambi::AmbisonicEncoder::encode_3rd_order(c.az, c.el);
                            const float g = c.gain_lin * transport_gain;
                            // Phase 2.1 — HRTF input is the prefeed-LP'd dry.
                            const float* dry =
                                bin_prefeed_[static_cast<std::size_t>(i)].data();
                            for (int k = 0; k < 16; ++k) {
                                const float ck = coeffs[static_cast<std::size_t>(k)] * g;
                                if (ck == 0.f) continue;
                                float* sh = b2_sh_scratch_[static_cast<std::size_t>(k)].data();
                                for (int n = 0; n < block.num_frames; ++n) {
                                    sh[n] += ck * dry[n];
                                }
                            }
                        }
                        // v0.6 #5 — measure wall-clock B2 processing time
                        // for the runtime sticky-underrun auto-demote detector.
                        // steady_clock::now() is a vDSO call on modern Linux
                        // (~30 ns, no syscall, no alloc — RT-safe).
                        //
                        // v0.6 P1-4 kill-switch + D-M2 vDSO gate.
                        //
                        // P1-4: once the sticky demote has fired,
                        // processBlockB2() may still run briefly while the
                        // crossfade unwinds toward Direct, but there is no
                        // value in measuring or reporting the cost further
                        // (recordB2BlockTiming early-returns on the demoted
                        // flag anyway).
                        //
                        // D-M2: on a platform where the initialize()-time
                        // probe found steady_clock::now() slow (i.e., it
                        // falls back to a syscall on this host), the
                        // brackets themselves would be the dominant cost
                        // and would push every B2 block over budget — a
                        // self-fulfilling demote prophecy. The heartbeat
                        // IO thread has already emitted
                        // /sys/binaural_warning ,s "rt_timing_unavailable"
                        // by the time the audio loop runs, so the host
                        // knows demote detection is disabled.
                        //
                        // Both gates collapse to a no-op-if-not-needed
                        // pattern: B2 still renders, just without runtime
                        // self-monitoring.
                        if (!binaural_.isRuntimeDemoted()
                            && binaural_.isSteadyClockFast()) {
                            const auto _b2_t0 = std::chrono::steady_clock::now();
                            binaural_.processBlockB2(b2_sh_ptrs_.data(),
                                                     /*order=*/3,
                                                     block.num_frames,
                                                     dstL, dstR);
                            const auto _b2_t1 = std::chrono::steady_clock::now();
                            const long long _b2_ns =
                                std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    _b2_t1 - _b2_t0).count();
                            binaural_.recordB2BlockTiming(
                                block.num_frames,
                                static_cast<float>(sample_rate_), _b2_ns);
                        } else {
                            binaural_.processBlockB2(b2_sh_ptrs_.data(),
                                                     /*order=*/3,
                                                     block.num_frames,
                                                     dstL, dstR);
                        }
                    } else {
                        // B1 per-object HRTF sum.
                        std::fill(dstL, dstL + block.num_frames, 0.f);
                        std::fill(dstR, dstR + block.num_frames, 0.f);
                        for (int i = 0; i < MAX_OBJECTS; ++i) {
                            const auto& c = obj_cache_[static_cast<std::size_t>(i)];
                            if (!c.active || c.gain_lin < 1e-6f) continue;

                            // Phase 2.6b — rotate the object direction into the
                            // head frame BEFORE the HRTF lookup. Pure stack math
                            // (no alloc); identity when the pose is zero. L/R
                            // sign locked in rotate_engine_dir_by_head (2.6a):
                            // head yaw +30° shifts a front source to the RIGHT.
                            const auto [az_h, el_h] = coords::rotate_engine_dir_by_head(
                                c.az, c.el, head_yaw_rad, head_pitch_rad, head_roll_rad);

                            // Update direction (RT-safe: KD-tree lookup +
                            // loadInto + GainRamp setup; no alloc).
                            binaural_.setDirection(i, az_h, el_h);

                            // Convolve into the per-object scratch tmp_L/R.
                            // Phase 2.1 — HRTF input is the prefeed-LP'd dry.
                            binaural_.processBlockForObject(
                                i,
                                bin_prefeed_[static_cast<std::size_t>(i)].data(),
                                block.num_frames,
                                bin_tmp_L_.data(),
                                bin_tmp_R_.data());

                            // Sum with per-object gain.
                            const float g = c.gain_lin * transport_gain;
                            for (int n = 0; n < block.num_frames; ++n) {
                                dstL[n] += g * bin_tmp_L_[static_cast<std::size_t>(n)];
                                dstR[n] += g * bin_tmp_R_[static_cast<std::size_t>(n)];
                            }
                        }
                    }
                };

                if (step.active) {
                    // Ramp in flight — render both branches into dedicated
                    // scratch buffers, then envelope-mix into binaural_*_buf_.
                    render_branch(step.outgoing,
                                  bin_xfade_out_L_.data(),
                                  bin_xfade_out_R_.data());
                    render_branch(step.incoming,
                                  bin_xfade_in_L_.data(),
                                  bin_xfade_in_R_.data());

                    const float* env_in =
                        binaural_.xfadeIncomingEnvelope(step.total_blocks);
                    const float* env_out =
                        binaural_.xfadeOutgoingEnvelope(step.total_blocks);
                    if (env_in && env_out) {
                        // Envelopes are sized per BinauralMonitor::block_size_
                        // (set at initialize() time via Config.blockSize).
                        // Index = block_index * monitor_block_size + sample.
                        const int mon_bs = binaural_.blockSize();
                        const int base   = step.block_index * mon_bs;
                        for (int n = 0; n < block.num_frames; ++n) {
                            const float gi =
                                env_in[static_cast<std::size_t>(base + n)];
                            const float go =
                                env_out[static_cast<std::size_t>(base + n)];
                            binaural_l_buf_[static_cast<std::size_t>(n)] =
                                gi * bin_xfade_in_L_[static_cast<std::size_t>(n)]
                              + go * bin_xfade_out_L_[static_cast<std::size_t>(n)];
                            binaural_r_buf_[static_cast<std::size_t>(n)] =
                                gi * bin_xfade_in_R_[static_cast<std::size_t>(n)]
                              + go * bin_xfade_out_R_[static_cast<std::size_t>(n)];
                        }
                    } else {
                        // Envelope unavailable — fall back to the incoming
                        // branch directly (better than silence).
                        std::copy(bin_xfade_in_L_.begin(),
                                  bin_xfade_in_L_.begin() + block.num_frames,
                                  binaural_l_buf_.begin());
                        std::copy(bin_xfade_in_R_.begin(),
                                  bin_xfade_in_R_.begin() + block.num_frames,
                                  binaural_r_buf_.begin());
                    }
                } else {
                    // Steady state — render the single steady branch directly
                    // into the bus buffer (no ramp mixing).
                    render_branch(step.steady,
                                  binaural_l_buf_.data(),
                                  binaural_r_buf_.data());
                }

                // Decrement xfade counter for this block (no-op when steady).
                binaural_.finalizeXfadeBlock();

                // Phase 2.4 — binaural monitor stereo delay ring on the final
                // L/R bus, BEFORE the EQ (reference order HRTF→delay→EQ→limit,
                // BinauralMonitorChain.cpp:132-154). tap = binauralDelayMs (read
                // once/block); 0 ms = passthrough. RT-safe: ring pre-allocated
                // at prepare, modulo indexing only (alloc 0).
                {
                    const int cap = static_cast<int>(bin_delay_L_.size());
                    if (cap >= 4) {
                        int ds = static_cast<int>(
                            bin_delay_ms_.load(std::memory_order_relaxed)
                            * 0.001f * static_cast<float>(sample_rate_) + 0.5f);
                        ds = std::clamp(ds, 0, cap - 2);
                        if (ds > 0) {
                            for (int n = 0; n < block.num_frames; ++n) {
                                const int wp = (bin_delay_write_ + n) % cap;
                                const int rp = (wp - ds + cap) % cap;
                                const float outL = bin_delay_L_[static_cast<std::size_t>(rp)];
                                const float outR = bin_delay_R_[static_cast<std::size_t>(rp)];
                                bin_delay_L_[static_cast<std::size_t>(wp)] =
                                    binaural_l_buf_[static_cast<std::size_t>(n)];
                                bin_delay_R_[static_cast<std::size_t>(wp)] =
                                    binaural_r_buf_[static_cast<std::size_t>(n)];
                                binaural_l_buf_[static_cast<std::size_t>(n)] = outL;
                                binaural_r_buf_[static_cast<std::size_t>(n)] = outR;
                            }
                        }
                        bin_delay_write_ = (bin_delay_write_ + block.num_frames) % cap;
                    }
                }

                // Phase 2.5 — binaural monitor 5-band peak EQ on the final L/R
                // bus, BEFORE the limiter (reference order: EQ → gain/limit,
                // BinauralMonitorChain.cpp:190-208). Shared by B1 and B2 (both
                // write binaural_*_buf_). Gated on the master flag; flat bands
                // (0 dB) are a unity passthrough so an enabled-but-untuned EQ is
                // a no-op. RT-safe: RoomBiquad::processSample is pure float math.
                if (binaural_eq_active_.load(std::memory_order_relaxed)) {
                    for (int n = 0; n < block.num_frames; ++n) {
                        const std::size_t ni = static_cast<std::size_t>(n);
                        float l = binaural_l_buf_[ni];
                        float r = binaural_r_buf_[ni];
                        for (int b = 0; b < iae::kBinauralEqBands; ++b) {
                            const std::size_t bi = static_cast<std::size_t>(b);
                            l = bin_eq_L_[bi].processSample(l);
                            r = bin_eq_R_[bi].processSample(r);
                        }
                        binaural_l_buf_[ni] = l;
                        binaural_r_buf_[ni] = r;
                    }
                }

                // Apply per-channel limiter to prevent clipping under heavy load
                // (shared by B1 and B2 since both paths write to binaural_*_buf_).
                for (int n = 0; n < block.num_frames; ++n) {
                    binaural_l_buf_[static_cast<std::size_t>(n)] =
                        binaural_lim_L_.processSample(binaural_l_buf_[static_cast<std::size_t>(n)]);
                    binaural_r_buf_[static_cast<std::size_t>(n)] =
                        binaural_lim_R_.processSample(binaural_r_buf_[static_cast<std::size_t>(n)]);
                }
            }
            // else: no .speh loaded — buffers stay zeroed.
        } else if (binaural_ok_) {
            // binaural_enabled_=0: zero the binaural buffers so downstream
            // readers (VST3 bus 1, legacy speaker-bus-tail) see silence
            // without paying for any DSP.
            //
            // v0.5.1 hotfix (code-reviewer MAJOR): keep the xfade monitor's
            // prev_effective_mode_ advancing in lock-step even while binaural
            // is disabled. Without this, a setRequestedMode() flip during the
            // disabled span would cause the first re-enabled block to detect
            // effective != prev_effective_mode_ and ARM an unwanted ramp —
            // producing a brief dual-branch envelope artifact on re-enable
            // even though nothing was actually being rendered while disabled.
            // The per-block observeAndArmXfade + finalizeXfadeBlock pair
            // discards its returned XfadeStep (no rendering occurs in this
            // branch) and is virtually free; any ramp that "armed" while
            // disabled completes within total_blocks consumed empty blocks,
            // collapsing prev_effective_mode_ to the current effective mode
            // before any audio actually flows again.
            if (binaural_.hasHrtf()) {
                (void)binaural_.observeAndArmXfade();
                binaural_.finalizeXfadeBlock();
            }

            std::fill(binaural_l_buf_.begin(),
                      binaural_l_buf_.begin() + block.num_frames, 0.f);
            std::fill(binaural_r_buf_.begin(),
                      binaural_r_buf_.begin() + block.num_frames, 0.f);

            // Legacy speaker-bus-tail wiring: if the caller exposes channels
            // [n_spk, n_spk+1] in the output bus (NullBackend ctest path),
            // mirror the binaural buffers there too. VST3 routes the same
            // buffers to bus 1 via SpatialEngine::binauralL()/binauralR().
            if (block.output_channel_count >= n_spk + 2 &&
                block.output_channels &&
                block.output_channels[n_spk] &&
                block.output_channels[n_spk + 1])
            {
                std::copy(binaural_l_buf_.begin(),
                          binaural_l_buf_.begin() + block.num_frames,
                          block.output_channels[n_spk]);
                std::copy(binaural_r_buf_.begin(),
                          binaural_r_buf_.begin() + block.num_frames,
                          block.output_channels[n_spk + 1]);
            }
        }
        stage_binaural_us = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - _ts_binaural0).count() * 1e-3;
    }

    blocks_processed_.fetch_add(1, std::memory_order_relaxed);

    util::TraceEvent ev;
    ev.timestamp_ns = block.hw_timestamp_ns;
    ev.kind         = 1;
    ev.payload_a    = static_cast<std::uint32_t>(block.num_frames);
    ev.payload_b    = static_cast<std::uint32_t>(block.output_channel_count);
    trace_.push(ev);

    // v0.9 Lane A (A-M1) — close the per-block wall measurement at the single
    // normal fall-through exit, fold it into the EWMA/peak/P² estimators, and
    // publish the scalar results into the single-owner ObservabilityCounters.
    // O(1) + relaxed scalar atomic stores only — no alloc/lock/syscall.
    // Use the engine's canonical sample_rate_ (set in prepareToPlay) for the
    // block budget, matching the rest of audioBlock (NIT-2). Under all current
    // backends block.sample_rate == sample_rate_; the member is authoritative.
    cpu_meter_.recordBlockEnd(block.num_frames, sample_rate_);
    obs_counters_.cpu_pct_audio_thread.store(cpu_meter_.cpuPct(),
                                             std::memory_order_relaxed);
    obs_counters_.per_block_time_p99_us.store(cpu_meter_.p99Us(),
                                              std::memory_order_relaxed);
    // v1.0 Phase 1.4b — publish per-stage timings (last block, microseconds).
    // 0 for any stage that did not run this block. Relaxed scalar stores only.
    obs_counters_.stage_render_us.store(
        static_cast<std::uint32_t>(stage_render_us + 0.5), std::memory_order_relaxed);
    obs_counters_.stage_room_us.store(
        static_cast<std::uint32_t>(stage_room_us + 0.5), std::memory_order_relaxed);
    obs_counters_.stage_decorr_us.store(
        static_cast<std::uint32_t>(stage_decorr_us + 0.5), std::memory_order_relaxed);
    obs_counters_.stage_binaural_us.store(
        static_cast<std::uint32_t>(stage_binaural_us + 0.5), std::memory_order_relaxed);
}

// B-M3 — control-tick SOFA swap apply. Runs on the ~1 Hz control tick
// (same thread + cadence as applyPendingAmbiDecoderChange). The audio thread
// never touches pending_binaural_sofa_path_ or pending_binaural_sofa_flag_.
void SpatialEngine::applyPendingBinauralSofa()
{
    if (!pending_binaural_sofa_flag_.load(std::memory_order_acquire))
        return;
    pending_binaural_sofa_flag_.store(false, std::memory_order_relaxed);
    const std::string path = pending_binaural_sofa_path_;
    if (path.empty()) return;
    // loadPendingSofa + applyPendingSofaChange are both control-thread only
    // (defined in BinauralMonitor, B-M2). They perform the alloc+build off
    // the audio thread and publish via lock-free double-buffer.
    binaural_.loadPendingSofa(path);
    binaural_.applyPendingSofaChange();
}

// Phase 4.3 Inc 2b — XDG/HOME-derived default layout-library directory. Used
// only as a fallback when setLayoutLibraryDir() was never called (production
// wires an explicit path from the bin; tests pass a tmp dir). Control-thread.
static std::string defaultLayoutLibraryDir()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && xdg[0] != '\0') {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home ? home : "/tmp") + "/.config";
    }
    return base + "/spatial_engine/layouts";
}

// Phase 4.3 Inc 2b — control-tick layout-slot apply. Runs on the ~1 Hz control
// tick (same thread + cadence as applyPendingBinauralSofa). This function is the
// SOLE owner of layout_library_: it lazily constructs it and performs ALL slot
// file I/O (save/load/clear) + the sendReply emissions here. The audio thread
// never touches pending_layout_op_ or the LayoutLibrary. Replies use the ,iiss
// shape (slot, status, detail1, detail2) via the existing IIS sendReply overload.
void SpatialEngine::applyPendingLayoutSlotOp()
{
    if (!pending_layout_op_flag_.load(std::memory_order_acquire))
        return;
    pending_layout_op_flag_.store(false, std::memory_order_relaxed);
    const ipc::PayloadLayoutSlot op = pending_layout_op_;  // copy out (control thread)

    if (!layout_library_) {
        const std::string dir =
            layout_library_dir_.empty() ? defaultLayoutLibraryDir() : layout_library_dir_;
        layout_library_ = std::make_unique<geometry::LayoutLibrary>(dir);
    }
    geometry::LayoutLibrary& lib = *layout_library_;

    using Op = ipc::PayloadLayoutSlot::Op;
    switch (op.op) {
    case Op::Save: {
        const bool ok = lib.save(op.slot, currentLayout(), op.label);
        osc_backend_.sendReply("/layout/slot/save", ",iiss",
                               op.slot, ok ? 1 : 0, op.label.c_str(), "");
        break;
    }
    case Op::Load: {
        // Inc 2b: validate + (notionally) stage ONLY — NO live layout swap. The
        // running layout is left untouched; live application is deferred to Inc 4.
        // We decode + invoke LayoutLibrary::load() to validate the slot and report
        // success (with the loaded layout name) or the parse-error string.
        geometry::LayoutResult res = lib.load(op.slot);
        const bool ok = geometry::is_ok(res);
        const std::string detail = ok ? std::get<geometry::SpeakerLayout>(res).name
                                      : std::get<std::string>(res);
        osc_backend_.sendReply("/layout/slot/load", ",iiss",
                               op.slot, ok ? 1 : 0, detail.c_str(), "");
        break;
    }
    case Op::Clear: {
        const bool ok = lib.clear(op.slot);
        osc_backend_.sendReply("/layout/slot/clear", ",iiss",
                               op.slot, ok ? 1 : 0, "", "");
        break;
    }
    case Op::List: {
        // One reply per occupied slot (slot, occupied_count, label, ""), then a
        // summary terminator with slot=-1 carrying the count (so a client knows
        // the listing is complete even when zero slots are occupied).
        const int count = lib.occupiedCount();
        for (int s = 0; s < geometry::LayoutLibrary::kSlotCount; ++s) {
            if (!lib.occupied(s)) continue;
            const std::string lbl = lib.label(s);
            osc_backend_.sendReply("/layout/slot/list", ",iiss",
                                   s, count, lbl.c_str(), "");
        }
        osc_backend_.sendReply("/layout/slot/list", ",iiss", -1, count, "", "");
        break;
    }
    case Op::Current: {
        const int count = lib.occupiedCount();
        if (op.slot < 0) {
            // Summary form: -1 + total occupied count.
            osc_backend_.sendReply("/layout/slot/current", ",iiss",
                                   -1, count, "", "");
        } else {
            const bool occ = lib.occupied(op.slot);
            const std::string lbl = lib.label(op.slot);
            osc_backend_.sendReply("/layout/slot/current", ",iiss",
                                   op.slot, occ ? 1 : 0, lbl.c_str(), "");
        }
        break;
    }
    }
}

// v0.5 P4.1 (A6) / v0.5.1 Q1 — moved from header to avoid per-TU duplication.
float SpatialEngine::triggerBinauralProbe()
{
    const float throughput = binaural_.runThroughputProbe();
    const char* code = binaural_.probeWarningCode();
    if (code && code[0] != '\0') {
        osc_backend_.sendReply("/sys/binaural_warning", ",sf",
                               code, throughput);
    }
    return throughput;
}

// v0.5.1 Q1 — moved from header to avoid per-TU duplication.
void SpatialEngine::injectProbeThroughputAndEmit(float throughput_rt)
{
    // Make sure B2 is requested so the injected throughput maps to a
    // real fallback decision in BinauralMonitor.
    binaural_.setRequestedMode(output::BinauralMode::AmbiVS);
    binaural_.injectProbeThroughputForTest(throughput_rt);
    const char* code = binaural_.probeWarningCode();
    if (code && code[0] != '\0') {
        osc_backend_.sendReply("/sys/binaural_warning", ",sf",
                               code, binaural_.probeThroughput());
    }
}

// ADR 0018 D-5 — external-player heartbeat staleness check. Control/IO thread
// only (never the audio thread): no allocation beyond the small stack buffer,
// and the sendReply enqueue is the same lock-free path used by every other
// /sys/ telemetry emission. Emits /sys/warning ,iis 0 0 "player_heartbeat_stale"
// "<seconds>" at most once per kPlayerStaleWarnIntervalMs window when the last
// external ping is older than kPlayerHeartbeatStaleMs.
bool SpatialEngine::checkPlayerHeartbeatStale(int64_t now_unix_ms) noexcept
{
    const int64_t last = last_player_ping_unix_ms_.load(std::memory_order_relaxed);
    if (last == 0) {
        return false;  // no external ping ever seen → nothing to watch
    }

    const int64_t age_ms = now_unix_ms - last;
    if (age_ms < kPlayerHeartbeatStaleMs) {
        return false;  // fresh enough
    }

    // Rate-limit: at most once per 30 s window. The latch is cleared on the
    // next external ping (control-thread HbPing path), which re-arms a future
    // warning after a resume-then-stale cycle.
    if (player_stale_latched_.load(std::memory_order_relaxed) &&
        (now_unix_ms - last_stale_warning_unix_ms_) < kPlayerStaleWarnIntervalMs) {
        return false;
    }

    last_stale_warning_unix_ms_ = now_unix_ms;
    player_stale_latched_.store(true, std::memory_order_relaxed);

    char seconds_str[24];
    std::snprintf(seconds_str, sizeof(seconds_str), "%lld",
                  static_cast<long long>(age_ms / 1000));
    osc_backend_.sendReply("/sys/warning", ",iis", 0, 0,
                           "player_heartbeat_stale", seconds_str);
    return true;
}

}  // namespace spe::core

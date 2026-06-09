// core/src/core/SpatialEngine.cpp

#include "ambi/AmbisonicEncoder.h"
#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "reverb/ReverbEngine.h"
#include "util/RtAssertNoAlloc.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
                    static constexpr float kRad2Deg =
                        180.f / 3.14159265358979323846f;
                    static constexpr float kMaxDist = 20.f;
                    osc_backend_.echoPlane().markAed(
                        p->obj_id, p->az_rad * kRad2Deg,
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
                    qc.noise_pink = p->pink;
                }
                break;
            case ipc::CommandTag::NoiseGain:
                if (auto* p = std::get_if<ipc::PayloadNoiseGain>(&cmd.payload)) {
                    qc.noise_ch     = p->channel;
                    qc.noise_gain_db = p->gain_db;
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
                    // C7 echo: param 7 (Width) shares c.width_rad with ObjWidth
                    // (apply case 7 == ObjWidth's field), so route it to
                    // markWidth → /adm/obj/N/width (the canonical width
                    // address). params 0..6 echo on /adm/obj/N/dsp. A param-7
                    // write produces NO /adm/obj/N/dsp packet.
                    if (p->param == ipc::PayloadObjDsp::Param::Width)
                        osc_backend_.echoPlane().markWidth(p->obj_id, p->value);
                    else
                        osc_backend_.echoPlane().markDsp(
                            p->obj_id, static_cast<uint8_t>(p->param), p->value);
                }
                break;
            case ipc::CommandTag::ObjInput:
                // A3 — input→object routing. Mirror ObjDsp's translate: copy the
                // route (src_ch / gain) into the QueuedCmd for the RT drain.
                // F-A3-echo: deferred (no live per-change echo plane mark; the
                // C6 /sys/state_request resync dump carries routing instead).
                if (auto* p = std::get_if<ipc::PayloadObjInput>(&cmd.payload)) {
                    qc.obj_id        = p->obj_id;
                    qc.input_src_ch  = p->src_ch;
                    qc.input_gain    = p->gain;
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
                        pending_binaural_sofa_flag_.store(true, std::memory_order_relaxed);
                        state_model_.setPendingSofaName(p->name);
                    }
                    // Out-of-catalog name → safe no-op (no crash).
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
            case ipc::CommandTag::SysStateRequest: {
                // C6 — full-state UDP-loss resync. Runs on the OSC IO thread.
                // No audio FIFO enqueue (early-return group). Requires a prior
                // echo /sys/handshake; a non-subscriber request is a no-op.
                if (!osc_backend_.echoPlane().hasSubscribers()) return;
                using clock = std::chrono::steady_clock;
                const int64_t now_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        clock::now().time_since_epoch())
                        .count();
                // REV2 amendment #4: this GLOBAL debounce is correct ONLY while
                // the dump is BROADCAST (one serviced request refreshes every
                // subscriber). If a targeted-unicast emit is ever adopted, this
                // MUST become per-subscriber or it silently suppresses another
                // client's resync.
                if (now_ms - last_state_request_ms_ < kStateRequestDebounceMs)
                    return;  // Decision 6
                last_state_request_ms_ = now_ms;
                // REV4: include_dsp_only=true → pure-DSP inactive objects are
                // dumped so the client reconciles EXACTLY to obj_cache_.
                // /scene/save keeps the default false (byte-identical).
                // Mutex-serialized inside snapshotObjects().
                snapshotObjects(state_dump_buf_, /*include_dsp_only=*/true);
                osc_backend_.echoPlane().emitStateDump(
                    state_dump_buf_, now_ms, osc_backend_.udpFdForEcho());
                return;
            }
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
{
    // C6 (Decision 3) — pre-reserve the resync dump buffer so steady-state
    // /sys/state_request handling allocates nothing (not the audio thread).
    state_dump_buf_.reserve(MAX_OBJECTS);
}

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
void SpatialEngine::snapshotObjects(std::vector<ipc::ObjectSnapshot>& out,
                                    bool include_dsp_only) const {
    // C6 (REV2 amendment #1) — serialize the two non-RT readers (scene-save @
    // control loop + state_request @ OSC IO). The audio writer never locks.
    std::lock_guard<std::mutex> lk(state_snapshot_mtx_);
    // AC8 — algo enum ↔ ObjectSnapshot.algorithm (int) round-trip is a plain
    // static_cast in both directions; assert each variant survives compile-time.
    static_assert(static_cast<int>(ipc::Algorithm::VBAP)      == 0, "VBAP enum value");
    static_assert(static_cast<int>(ipc::Algorithm::WFS)       == 1, "WFS enum value");
    static_assert(static_cast<int>(ipc::Algorithm::DBAP)      == 2, "DBAP enum value");
    static_assert(static_cast<int>(ipc::Algorithm::Ambisonic) == 3, "Ambisonic enum value");
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
        // C6 (REV4): when include_dsp_only, also catch objects whose ONLY
        // non-default state is a per-object DSP param (EQ band / user delay /
        // k_hf) so the resync dump reconciles EXACTLY to obj_cache_. The clause
        // is gated on the flag, so /scene/save (default false) is unchanged.
        const bool touched =
            c.active || c.az != 0.f || c.el != 0.f || c.dist != 1.f ||
            c.gain_lin != 1.f || c.reverb_send != 0.f || c.width_rad != 0.f ||
            c.algo != ipc::Algorithm::VBAP ||
            (include_dsp_only &&
             (c.k_hf != 0.5f || c.user_delay_ms != 0.f ||
              c.eq_gain_db[0] != 0.f || c.eq_gain_db[1] != 0.f ||
              c.eq_gain_db[2] != 0.f || c.eq_gain_db[3] != 0.f ||
              // A3 — a pure-routing object (only a non-default input route) is
              // dumped on resync so the dump reconciles EXACTLY to obj_cache_;
              // gated on include_dsp_only so /scene/save stays unchanged.
              c.input_src_ch != -1 || c.input_gain != 1.f));
        if (!touched) continue;
        out.push_back(ipc::ObjectSnapshot{ /*id*/ i, c.az, c.el, c.dist,
                                           static_cast<int>(c.algo), c.gain_lin,
                                           /*muted*/ !c.active, c.width_rad, c.reverb_send,
                                           c.k_hf, c.user_delay_ms,
                                           {c.eq_gain_db[0], c.eq_gain_db[1],
                                            c.eq_gain_db[2], c.eq_gain_db[3]},
                                           c.input_src_ch, c.input_gain });
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
        const int n_spk = vbap_.numSpeakers();
        const size_t total = static_cast<size_t>(n_spk) * max_block_size;
        mix_buf_.assign(total, 0.f);
        vbap_scratch_.assign(total, 0.f);
        dbap_scratch_.assign(total, 0.f);
        wfs_scratch_.assign(total, 0.f);
        ambisonic_scratch_.assign(total, 0.f);
        // Per-channel noise generator state (one entry per speaker output).
        // Size > 1 avoids OOB if a /noise/{ch}/* command arrives before layout.
        noise_chans_.assign(static_cast<size_t>(std::max(n_spk, 1)), NoiseChan{});

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

void SpatialEngine::audioBlock(const spe::audio_io::AudioBlock& block) {
    SPE_RT_NO_ALLOC_SCOPE();

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
            case ipc::CommandTag::ObjInput:
                // A3 — apply the input→object route. Direct int32←int32 assign
                // (no narrowing cast); RT-safe (no alloc/lock/syscall).
                c.input_src_ch = qc.input_src_ch;
                c.input_gain   = qc.input_gain;
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
                    noise_chans_[static_cast<size_t>(idx)].pink = qc.noise_pink;
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
            case ipc::CommandTag::TransportPlay:
                transport_play_.store(true, std::memory_order_relaxed);
                break;
            case ipc::CommandTag::TransportStop:
                transport_play_.store(false, std::memory_order_relaxed);
                break;
            case ipc::CommandTag::ReverbSelect:
                active_reverb_.store(static_cast<int>(qc.reverb_which),
                                     std::memory_order_relaxed);
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
                // Store Cartesian; renderers use spherical (az/el/dist).
                // Convert: x=right y=up z=forward → az/el via atan2.
                {
                    float r = std::sqrt(qc.xyz_x * qc.xyz_x +
                                        qc.xyz_y * qc.xyz_y +
                                        qc.xyz_z * qc.xyz_z);
                    if (r > 1e-6f) {
                        c.az   = std::atan2(qc.xyz_x, qc.xyz_z);
                        c.el   = std::asin(qc.xyz_y / r);
                        c.dist = r;
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

    // Per-object dry signal source (RT-safe: no alloc). Default = internal sine
    // tones; when object-source=input is selected AND the backend supplies input
    // channels, route an input channel → object i so a real musical source
    // (e.g. a stem over the shm ring) flows through the per-object DSP + panner.
    //
    // A3 — per-object input→object routing: object i reads input channel
    //   src = (c.input_src_ch >= 0) ? c.input_src_ch : i
    // (the sentinel -1 keeps the legacy 1:1 mapping), scaled by c.input_gain (a
    // linear input trim, block-stepped). The default route (-1, 1.0) makes
    // in[n]*1.0f byte-identical to the old std::copy. An out-of-range/null src
    // (e.g. a static misconfig) falls through to the object's sine test tone AND
    // silently ignores input_gain — exactly the legacy fallback semantics. Many-
    // to-one fan-out is just independent reads of one input pointer. RT-safe:
    // bounds-checked index + a multiply loop, no alloc/lock/syscall.
    const bool use_input =
        object_source_input_.load(std::memory_order_relaxed)
        && block.input_channels != nullptr
        && block.input_channel_count > 0;
    const float dt = 1.0f / static_cast<float>(sample_rate_);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        auto& scratch = dry_scratch_[static_cast<size_t>(i)];
        const auto& c = obj_cache_[static_cast<size_t>(i)];
        if (!c.active) {
            std::fill(scratch.begin(), scratch.begin() + block.num_frames, 0.0f);
            continue;
        }
        const int src = (c.input_src_ch >= 0) ? c.input_src_ch : i;
        if (use_input && src < block.input_channel_count
                && block.input_channels[src] != nullptr) {
            // Real source: scaled copy of input channel `src` into object i's dry
            // buf. Default route (src=i, gain=1) ⇒ in[n]*1.0f == in[n] (the old
            // std::copy, byte-identical). g is block-stepped (changes only at the
            // drain), consistent with the ramp_samples=0 per-object gain.
            const float  g  = c.input_gain;
            const float* in = block.input_channels[src];
            for (int n = 0; n < block.num_frames; ++n)
                scratch[static_cast<size_t>(n)] = in[n] * g;
            continue;
        }
        // Internal sine tone — unique frequency per object: 110, 165, 220, 275 … Hz
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
        for (int n = 0; n < block.num_frames; ++n) {
            float reverb_tap = 0.f;
            scratch[static_cast<size_t>(n)] =
                chains_[static_cast<size_t>(i)].processSample(
                    scratch[static_cast<size_t>(n)], reverb_tap);
            reverb_send_buf_[static_cast<size_t>(n)] += reverb_tap;
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
        } else {
            fdn_.process(reverb_wet_buf_.data(), block.num_frames);
        }
    }

    // Per-algorithm spatial render (each renderer reads dry_ptrs_,
    // sees only its own algorithm's objects via the masked spans below).
    if (render_ready_.load(std::memory_order_relaxed) && block.output_channel_count > 0) {
        const int n_spk = vbap_.numSpeakers();

        // v0.9 Lane C (hoist): scratch arrays are now engine members
        // (vbap_objs_/dbap_objs_/wfs_objs_/ambisonic_objs_) rather than stack
        // locals — RT-identical, just off the audio-thread stack. Each must be
        // re-zeroed per block because every object is written into exactly ONE
        // algorithm array (the prior stack locals were zero-initialised fresh
        // each block).
        vbap_objs_.fill(render::ObjectState{});
        dbap_objs_.fill(render::ObjectState{});
        wfs_objs_.fill(render::ObjectState{});
        ambisonic_objs_.fill(render::ObjectState{});
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            const auto& c = obj_cache_[static_cast<size_t>(i)];
            const render::ObjectState s = {c.az, c.el, c.dist, c.active, c.width_rad};
            switch (c.algo) {
            case ipc::Algorithm::WFS:       wfs_objs_[i]       = s; break;
            case ipc::Algorithm::DBAP:      dbap_objs_[i]      = s; break;
            case ipc::Algorithm::Ambisonic: ambisonic_objs_[i] = s; break;
            case ipc::Algorithm::VBAP:
            default:                        vbap_objs_[i]      = s; break;
            }
        }

        vbap_.processBlock(
            std::span<const render::ObjectState>(vbap_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            vbap_scratch_.data(), block.num_frames);
        dbap_.processBlock(
            std::span<const render::ObjectState>(dbap_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            dbap_scratch_.data(), block.num_frames);
        wfs_.processBlock(
            std::span<const render::ObjectState>(wfs_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            wfs_scratch_.data(), block.num_frames);
        ambisonic_.processBlock(
            std::span<const render::ObjectState>(ambisonic_objs_.data(), MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs_.data(), MAX_OBJECTS),
            ambisonic_scratch_.data(), block.num_frames);

        // Sum per-algorithm scratches into mix_buf_
        const int total = n_spk * block.num_frames;
        for (int idx = 0; idx < total; ++idx) {
            mix_buf_[static_cast<size_t>(idx)] =
                vbap_scratch_[static_cast<size_t>(idx)] +
                dbap_scratch_[static_cast<size_t>(idx)] +
                wfs_scratch_ [static_cast<size_t>(idx)] +
                ambisonic_scratch_[static_cast<size_t>(idx)];
        }

        // Distribute reverb wet uniformly across all speakers (energy-preserving)
        const float reverb_per_spk = 1.0f / std::sqrt(static_cast<float>(n_spk));
        for (int n = 0; n < block.num_frames; ++n) {
            const float wet = reverb_wet_buf_[static_cast<size_t>(n)] * reverb_per_spk;
            for (int s = 0; s < n_spk; ++s) {
                mix_buf_[static_cast<size_t>(n * n_spk + s)] += wet;
            }
        }

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

        // Per-channel noise generator (array verification): adds white/pink
        // noise scaled by gain_lin to the speaker bus. Bypasses VBAP/reverb;
        // intended for engineer to confirm physical wiring of each speaker.
        for (int spk = 0; spk < out_ch; ++spk) {
            if (spk >= static_cast<int>(noise_chans_.size())) break;
            auto& nc = noise_chans_[static_cast<size_t>(spk)];
            if (nc.gain_lin <= 0.f) continue;
            if (!block.output_channels || !block.output_channels[spk]) continue;

            constexpr float kNoiseScale = 1.0f / 2147483648.f;  // int32 → ±1 float
            constexpr float kPinkAlpha  = 0.05f;                // gentle LP for "pink"
            for (int n = 0; n < block.num_frames; ++n) {
                // xorshift32 RNG (RT-safe, deterministic)
                nc.rng ^= nc.rng << 13;
                nc.rng ^= nc.rng >> 17;
                nc.rng ^= nc.rng << 5;
                const float white = static_cast<float>(static_cast<int32_t>(nc.rng)) * kNoiseScale;
                float sample;
                if (nc.pink) {
                    nc.pink_state = nc.pink_state * (1.f - kPinkAlpha) + white * kPinkAlpha;
                    sample = nc.pink_state * 4.f;  // pink output is quieter; compensate
                } else {
                    sample = white;
                }
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
                            const float* dry =
                                dry_scratch_[static_cast<std::size_t>(i)].data();
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

                            // Update direction (RT-safe: KD-tree lookup +
                            // loadInto + GainRamp setup; no alloc).
                            binaural_.setDirection(i, c.az, c.el);

                            // Convolve into the per-object scratch tmp_L/R.
                            binaural_.processBlockForObject(
                                i,
                                dry_scratch_[static_cast<std::size_t>(i)].data(),
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
}

// B-M3 — control-tick SOFA swap apply. Runs on the ~1 Hz control tick
// (same thread + cadence as applyPendingAmbiDecoderChange). The audio thread
// never touches pending_binaural_sofa_path_ or pending_binaural_sofa_flag_.
void SpatialEngine::applyPendingBinauralSofa()
{
    if (!pending_binaural_sofa_flag_.load(std::memory_order_relaxed))
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

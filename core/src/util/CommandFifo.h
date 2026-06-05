// core/src/util/CommandFifo.h
// SPSC ring buffer for engine commands (OSC thread → audio thread).
// Stores a POD subset to avoid std::string in the hot path.
#pragma once
#include "ipc/Command.h"
#include <atomic>
#include <array>

namespace spe::util {

struct QueuedCmd {
    ipc::CommandTag tag  = ipc::CommandTag::Unknown;
    uint32_t obj_id      = 0;
    float    az_rad      = 0.f;
    float    el_rad      = 0.f;
    float    dist_m      = 1.f;
    float    gain        = 1.f;
    bool     active      = false;
    ipc::Algorithm algo  = ipc::Algorithm::VBAP;
    // Noise generator (NoiseType/NoiseGain)
    uint32_t noise_ch    = 0;
    bool     noise_pink  = false;
    float    noise_gain_db = -60.f;
    // ObjDsp parameter setter (EQ band gain dB / delay ms / k_hf 0..1 / reverb send 0..1)
    uint8_t  dsp_param   = 0;  // 0..3 EQ band, 4 delay_ms, 5 k_hf, 6 reverb_send
    float    dsp_value   = 0.f;
    // ReverbSelect: 0 = fdn, 1 = ir
    uint8_t  reverb_which = 0;
    // OutputGain / OutputLimit
    uint32_t output_ch       = 0;
    float    output_value_db = 0.f;
    // SysAmbiOrder: 1, 2, or 3
    uint8_t  ambi_order      = 1;
    // SysAmbiDecoderType: 0=PINV,1=MAX_RE,2=ALLRAD,3=EPAD,4=IN_PHASE
    uint8_t  ambi_decoder_type = 0;
    // SysLtcChase (C1.d): 0 = chase disabled, 1 = chase enabled.
    // Kept POD (int32_t) so QueuedCmd remains trivially copyable on the
    // audio thread; no std::string anywhere.
    int32_t  ltc_chase_enable = 0;
    // Phase C3 ADM-OSC v1.0 extended fields
    float    xyz_x     = 0.f;   // ObjXYZ Cartesian x
    float    xyz_y     = 0.f;   // ObjXYZ Cartesian y
    float    xyz_z     = 0.f;   // ObjXYZ Cartesian z
    float    width_rad = 0.f;   // ObjWidth source width in radians
    char     obj_name[32] = {}; // ObjName label (truncated, null-terminated)
    // ⑥e-4 Dreamscape room control (RoomCtl). room_op selects which field(s)
    // the audio-thread drain applies; SetAll applies all of them atomically in
    // one drain iteration. Mirrors ipc::PayloadRoomCtl::Op. POD — no string in
    // the hot path. Defaults are the engine's reference room defaults.
    uint8_t  room_op = 0;       // PayloadRoomCtl::Op
    bool     room_enable = false;
    float    room_t60 = 1.2f;
    float    room_sx = 6.f, room_sy = 5.f, room_sz = 3.f;
    float    room_early_width_deg = 45.f;
    float    room_early_balance01 = 0.45f;
    float    room_cluster_send01 = 0.4f;
    float    room_cluster_diffusion01 = 0.48f;
    float    room_cluster_volume_m3 = 630.f;
    float    room_eq_early_hp = 120.f, room_eq_early_lp = 10000.f;
    float    room_late_hf_corner_hz = 6200.f, room_late_hf_ratio01 = 0.62f;
    float    room_eq_late_hp = 45.f, room_eq_late_lp = 16000.f;
    float    room_dist_near_m = 0.5f, room_dist_far_m = 24.f, room_dist_linearity01 = 0.35f;
    float    room_early_gain_close_db = -10.f, room_early_gain_far_db = -18.f;
    float    room_late_gain_close_db = -12.f, room_late_gain_far_db = 0.f;
    float    room_early_predelay_ms = 20.f;
};

// SPSC ring buffer. T defaults to QueuedCmd so the existing audio-path
// cmd_fifo_ usage (CommandFifo<> / CommandFifo<N>) is byte-identical. The v0.9
// Lane E (E-M3) control-plane mailboxes instantiate CommandFifo<N, ipc::Command>
// to carry full decoded Commands across the UDP↔control thread boundary while
// keeping the single-producer/single-consumer invariant (fix 1a).
template<int N = 1024, typename T = QueuedCmd>
class CommandFifo {
    static_assert((N & (N-1)) == 0, "N must be power of two");
public:
    // Producer thread: push one element. Returns false if full.
    bool push(const T& cmd) noexcept {
        int h = head_.load(std::memory_order_relaxed);
        int t = tail_.load(std::memory_order_acquire);
        if (h - t >= N) return false;
        slots_[h & (N-1)] = cmd;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread: pop one element. Returns false if empty.
    bool pop(T& out) noexcept {
        int t = tail_.load(std::memory_order_relaxed);
        int h = head_.load(std::memory_order_acquire);
        if (h == t) return false;
        out = slots_[t & (N-1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    int size() const noexcept {
        return head_.load(std::memory_order_acquire)
             - tail_.load(std::memory_order_acquire);
    }

private:
    std::array<T, N> slots_{};
    std::atomic<int> head_{0};
    std::atomic<int> tail_{0};
};

} // namespace spe::util

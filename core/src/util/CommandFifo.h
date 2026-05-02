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
};

template<int N = 1024>
class CommandFifo {
    static_assert((N & (N-1)) == 0, "N must be power of two");
public:
    // Producer (OSC thread): push one command. Returns false if full.
    bool push(const QueuedCmd& cmd) noexcept {
        int h = head_.load(std::memory_order_relaxed);
        int t = tail_.load(std::memory_order_acquire);
        if (h - t >= N) return false;
        slots_[h & (N-1)] = cmd;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer (audio thread): pop one command. Returns false if empty.
    bool pop(QueuedCmd& out) noexcept {
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
    std::array<QueuedCmd, N> slots_{};
    std::atomic<int> head_{0};
    std::atomic<int> tail_{0};
};

} // namespace spe::util

// core/src/util/CpuMeter.h
//
// v0.9 Lane A (A-M1) — per-block audio-thread CPU/latency meter.
//
// Principle 1 (RT hot-path): recordBlockStart()/recordBlockEnd() are called
// from the audio thread. They must be allocation-free, lock-free, and
// syscall-free. The only work they do is:
//   • read steady_clock (vDSO on Linux — no syscall, no alloc),
//   • update O(1) scalar estimators (EWMA mean, peak tracker, P² p99),
//   • relaxed store of three scalar atomics that the control thread loads.
// There is NO array, NO reservoir, NO sort, NO double-buffer.
//
// Concurrency contract (review CONCERN-1): the "no data race" claim applies to
// the PUBLISHED scalar atomics (cpu_pct_q_/peak_pct_q_/p99_us_q_), which the
// control thread loads relaxed while the audio thread stores them relaxed —
// single-value, no torn-read window. The estimator INTERNALS (ewma/peak/P²
// markers below) are plain audio-thread-local scalars, NOT atomic. They are
// safe ONLY because recordBlockStart/End run exclusively on the audio thread
// and reset() runs on the control thread under the backend prepare/callback
// exclusion contract — i.e. reset() MUST NOT overlap recordBlockStart/End
// (prepareToPlay is called while the audio callback is stopped). If that
// precondition is violated the internals race. See plan §3 RT guards + ADR
// DD-B (B1, scalar estimator).
//
// CPU% = block_wall_us / block_budget_us × 100, where
//   block_budget_us = num_frames / sample_rate × 1e6.
// The mean is an EWMA (α = 0.1); a separate peak tracker keeps the worst.
// p99 is a SCALAR running estimate via the P² algorithm (Jain & Chlamtac
// 1985): five markers maintained in O(1) per sample, no stored samples.
//
// reset() clears all estimator state. It MUST be called whenever the block
// budget changes (prepareToPlay / sample-rate / block-size change) so stale
// samples taken under a different budget cannot poison the mean/peak/p99.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace spe::util {

class CpuMeter {
public:
    // Audio thread: stamp the block-entry time. Clock read only.
    inline void recordBlockStart() noexcept {
        block_start_ = Clock::now();
    }

    // Audio thread: compute this block's wall time, fold it into the EWMA
    // mean / peak / P² p99, and publish the scalar results. O(1), alloc-free.
    inline void recordBlockEnd(int num_frames, double sample_rate) noexcept {
        const Clock::time_point end = Clock::now();
        const auto wall_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - block_start_)
                .count();
        const double wall_us = static_cast<double>(wall_ns) * 1e-3;

        // Budget for this block in microseconds. Guard against a zero/garbage
        // sample rate so we never divide by zero on a pre-prepare block.
        double cpu_pct = 0.0;
        if (sample_rate > 0.0 && num_frames > 0) {
            const double budget_us =
                static_cast<double>(num_frames) / sample_rate * 1e6;
            if (budget_us > 0.0) {
                cpu_pct = wall_us / budget_us * 100.0;
            }
        }

        // EWMA mean (α = 0.1). Seed on the first sample so the average is not
        // dragged from zero.
        if (!seeded_) {
            ewma_cpu_pct_ = cpu_pct;
            peak_cpu_pct_ = cpu_pct;
            initP2(wall_us);
            seeded_ = true;
        } else {
            ewma_cpu_pct_ = kAlpha * cpu_pct + (1.0 - kAlpha) * ewma_cpu_pct_;
            if (cpu_pct > peak_cpu_pct_) peak_cpu_pct_ = cpu_pct;
            updateP2(wall_us);
        }

        // Publish scalar results for the control thread (relaxed; single-value
        // atomics, no torn-read window).
        cpu_pct_q_.store(toPct(ewma_cpu_pct_), std::memory_order_relaxed);
        peak_pct_q_.store(toPct(peak_cpu_pct_), std::memory_order_relaxed);
        p99_us_q_.store(p99Estimate(), std::memory_order_relaxed);
    }

    // Control thread: scalar loads. Race-free (single relaxed atomic each).
    std::uint32_t cpuPct()  const noexcept { return cpu_pct_q_.load(std::memory_order_relaxed); }
    std::uint32_t peakPct() const noexcept { return peak_pct_q_.load(std::memory_order_relaxed); }
    std::uint32_t p99Us()   const noexcept { return p99_us_q_.load(std::memory_order_relaxed); }

    // Control thread: clear all estimator state. Call on prepareToPlay /
    // sample-rate / block-size change so a new budget starts clean.
    void reset() noexcept {
        seeded_       = false;
        ewma_cpu_pct_ = 0.0;
        peak_cpu_pct_ = 0.0;
        for (int i = 0; i < 5; ++i) { q_[i] = 0.0; n_[i] = 0; np_[i] = 0.0; }
        count_ = 0;
        cpu_pct_q_.store(0, std::memory_order_relaxed);
        peak_pct_q_.store(0, std::memory_order_relaxed);
        p99_us_q_.store(0, std::memory_order_relaxed);
    }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr double kAlpha = 0.1;
    static constexpr double kP     = 0.99;  // target quantile

    static inline std::uint32_t toPct(double v) noexcept {
        if (v < 0.0) v = 0.0;
        if (v > 100.0) v = 100.0;
        return static_cast<std::uint32_t>(v + 0.5);
    }

    inline std::uint32_t p99Estimate() const noexcept {
        // Before the P² markers are warm (< 5 samples) we return the LAST
        // inserted sample (q_[count_-1], pre-sort warmup) — NOT a running max;
        // once warm, marker 3 (0-based) is the p99 height.
        double est = (count_ >= 5) ? q_[3]
                                   : (count_ > 0 ? q_[count_ - 1] : 0.0);
        if (est < 0.0) est = 0.0;
        return static_cast<std::uint32_t>(est + 0.5);
    }

    // ── P² scalar quantile estimator (Jain & Chlamtac 1985) ────────────────
    // q_  : marker heights, n_ : marker positions, np_ : desired positions.
    // The first 5 samples seed the markers (sorted insert); thereafter each
    // sample updates the five markers in O(1).
    inline void initP2(double x) noexcept {
        // count_ tracks how many seed samples we have inserted (0..5).
        q_[count_] = x;
        ++count_;
        if (count_ == 5) {
            // Sort the 5 seeds ascending (tiny fixed insertion sort).
            for (int i = 1; i < 5; ++i) {
                double key = q_[i];
                int j = i - 1;
                while (j >= 0 && q_[j] > key) { q_[j + 1] = q_[j]; --j; }
                q_[j + 1] = key;
            }
            for (int i = 0; i < 5; ++i) n_[i] = i + 1;
            np_[0] = 1.0;
            np_[1] = 1.0 + 2.0 * kP;
            np_[2] = 1.0 + 4.0 * kP;
            np_[3] = 3.0 + 2.0 * kP;
            np_[4] = 5.0;
            dn_[0] = 0.0;
            dn_[1] = kP / 2.0;
            dn_[2] = kP;
            dn_[3] = (1.0 + kP) / 2.0;
            dn_[4] = 1.0;
        }
    }

    inline void updateP2(double x) noexcept {
        if (count_ < 5) { initP2(x); return; }

        // 1. Find cell k and adjust extreme markers.
        int k;
        if (x < q_[0]) { q_[0] = x; k = 0; }
        else if (x >= q_[4]) { q_[4] = x; k = 3; }
        else {
            k = 0;
            for (int i = 1; i < 5; ++i) { if (x < q_[i]) { k = i - 1; break; } }
        }

        // 2. Increment positions of markers above k and desired positions.
        for (int i = k + 1; i < 5; ++i) ++n_[i];
        for (int i = 0; i < 5; ++i) np_[i] += dn_[i];

        // 3. Adjust interior markers 1..3 via the P² parabolic/linear rule.
        for (int i = 1; i <= 3; ++i) {
            const double d = np_[i] - static_cast<double>(n_[i]);
            if ((d >= 1.0 && (n_[i + 1] - n_[i]) > 1) ||
                (d <= -1.0 && (n_[i - 1] - n_[i]) < -1)) {
                const int dsign = (d >= 0.0) ? 1 : -1;
                const double qp = parabolic(i, dsign);
                if (q_[i - 1] < qp && qp < q_[i + 1]) {
                    q_[i] = qp;
                } else {
                    q_[i] = linear(i, dsign);
                }
                n_[i] += dsign;
            }
        }
    }

    inline double parabolic(int i, int dsign) const noexcept {
        const double d = static_cast<double>(dsign);
        const double a =
            static_cast<double>(n_[i] - n_[i - 1]);
        const double b =
            static_cast<double>(n_[i + 1] - n_[i]);
        return q_[i] + d / static_cast<double>(n_[i + 1] - n_[i - 1]) *
               ((a + d) * (q_[i + 1] - q_[i]) / b +
                (b - d) * (q_[i] - q_[i - 1]) / a);
    }

    inline double linear(int i, int dsign) const noexcept {
        const int j = i + dsign;
        return q_[i] + dsign *
               (q_[j] - q_[i]) / static_cast<double>(n_[j] - n_[i]);
    }

    // EWMA / peak state (audio-thread-only scalars).
    bool   seeded_       = false;
    double ewma_cpu_pct_ = 0.0;
    double peak_cpu_pct_ = 0.0;

    // P² markers (audio-thread-only scalars; NO array of stored samples — this
    // is a fixed 5-marker estimator, O(1) per block).
    double q_[5]  = {0, 0, 0, 0, 0};   // marker heights (quantile estimates)
    int    n_[5]  = {0, 0, 0, 0, 0};   // marker positions
    double np_[5] = {0, 0, 0, 0, 0};   // desired marker positions
    double dn_[5] = {0, 0, 0, 0, 0};   // desired-position increments
    int    count_ = 0;                 // samples seen (capped logically at warm)

    Clock::time_point block_start_{};

    // Published scalar results (cross-thread; single relaxed atomics).
    std::atomic<std::uint32_t> cpu_pct_q_{0};
    std::atomic<std::uint32_t> peak_pct_q_{0};
    std::atomic<std::uint32_t> p99_us_q_{0};
};

}  // namespace spe::util

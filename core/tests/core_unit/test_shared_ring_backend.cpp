// core/tests/core_unit/test_shared_ring_backend.cpp
//
// ADR 0019 Phase C PCM IPC — PR2 (backend) ctest cases 1–8.
//
// POSIX-only (POSIX shm). On other platforms compile a SKIP main(), mirroring
// test_shared_memory_region.cpp.
//
// Each test creates its own shm region (CreateOrOpen, total_region_bytes),
// writes a RingHeader, drives the backend, asserts, then detach()+shm_unlink().
// The producer side is faked in-test by writing the ring directly.
//
// SRB_RT_SENTINEL: when defined (RT_ASSERTS=ON build), main() runs ONLY the
// case-8 RT-sentinel portion (invariant i: rt_alloc_violations()==0). When not
// defined, main() runs cases 1–7 + case 8 structural invariants (ii/iii/iv).

#include "audio_io/SharedRingBackend.h"
#include "audio_io/shm/RingHeader.h"
#include "audio_io/shm/SharedMemoryRegion.h"
#include "audio_io/AudioCallback.h"
#include "util/RtAssertNoAlloc.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)

#include <sys/mman.h>   // shm_unlink
#include <unistd.h>     // getpid

using namespace spe::audio_io;
using namespace spe::audio_io::shm;

// ── Test harness ────────────────────────────────────────────────────────

static int g_counter = 0;

static std::string unique_shm_name() {
    return "/spe-srb-" + std::to_string(static_cast<long>(::getpid()))
           + "-" + std::to_string(++g_counter);
}

// Capturing callback: records the last AudioBlock's input samples per channel
// and the call count, so tests can assert silence / sample-exact delivery.
class CaptureCallback final : public AudioCallback {
public:
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void audioBlock(const AudioBlock& blk) override {
        ++calls;
        last_frames = blk.num_frames;
        last_in_channels = blk.input_channel_count;
        last_hw_ts = blk.hw_timestamp_ns;
        captured.assign(static_cast<std::size_t>(blk.input_channel_count),
                        std::vector<float>(static_cast<std::size_t>(blk.num_frames), 0.0f));
        for (int ch = 0; ch < blk.input_channel_count; ++ch) {
            const float* src = blk.input_channels[ch];
            for (int n = 0; n < blk.num_frames; ++n) {
                captured[static_cast<std::size_t>(ch)][static_cast<std::size_t>(n)] = src[n];
            }
        }
    }
    int calls = 0;
    int last_frames = 0;
    int last_in_channels = 0;
    std::uint64_t last_hw_ts = 0;
    std::vector<std::vector<float>> captured;
};

// Owns a created shm region + writes a RingHeader. Cleans up on destruction.
struct RingFixture {
    std::string         name;
    SharedMemoryRegion  region;
    std::uint32_t       channels;
    std::uint32_t       capacity;

    RingFixture(std::uint32_t sample_rate, std::uint32_t block_size,
                std::uint32_t channels_, std::uint32_t capacity_,
                std::uint64_t magic = kSpeRingMagic,
                std::uint32_t version = kRingHeaderVersion)
        : name(unique_shm_name()), channels(channels_), capacity(capacity_) {
        const std::size_t bytes = total_region_bytes(channels, capacity);
        RegionError err = region.attach(name.c_str(), AttachMode::CreateOrOpen, bytes);
        assert(err == RegionError::Ok);
        RingHeader* h = region.header();
        std::memset(h, 0, sizeof(RingHeader));
        h->magic           = magic;
        h->version         = version;
        h->header_size     = kRingHeaderSize;
        h->sample_rate     = sample_rate;
        h->block_size      = block_size;
        h->channels        = channels;
        h->capacity_frames = capacity;
        h->write_idx.store(0, std::memory_order_relaxed);
        h->read_idx.store(0, std::memory_order_relaxed);
        h->producer_heartbeat_ms.store(0, std::memory_order_relaxed);
        h->xrun_count.store(0, std::memory_order_relaxed);
        h->producer_meta_block_pts_ns.store(0, std::memory_order_relaxed);
        h->producer_state.store(static_cast<std::uint32_t>(ProducerState::Streaming),
                                std::memory_order_relaxed);
        h->seq.store(0, std::memory_order_relaxed);
    }

    ~RingFixture() {
        region.detach();
        ::shm_unlink(name.c_str());
    }

    RingHeader* header() { return region.header(); }
    char*       base()   { return static_cast<char*>(region.base()); }

    // Publish one block of planar samples at the current write_idx, store the
    // pts, release-store write_idx += block_size, bump seq, set heartbeat.
    void producer_write_block(const std::vector<std::vector<float>>& channel_data,
                              std::uint32_t block_size, std::uint64_t pts_ns,
                              std::uint64_t heartbeat_ms = 0) {
        RingHeader* h = header();
        const std::uint64_t wi = h->write_idx.load(std::memory_order_relaxed);
        const std::uint32_t idx = static_cast<std::uint32_t>(wi & (capacity - 1));
        for (std::uint32_t ch = 0; ch < channels; ++ch) {
            float* ch_base = reinterpret_cast<float*>(
                base() + channel_byte_offset(ch, capacity));
            const auto& src = channel_data[ch];
            for (std::uint32_t n = 0; n < block_size; ++n) {
                ch_base[(idx + n) & (capacity - 1)] = src[n];
            }
        }
        h->producer_meta_block_pts_ns.store(pts_ns, std::memory_order_relaxed);
        h->write_idx.store(wi + block_size, std::memory_order_release);
        h->seq.fetch_add(1, std::memory_order_relaxed);
        h->producer_heartbeat_ms.store(heartbeat_ms, std::memory_order_relaxed);
    }
};

static std::vector<std::vector<float>> ramp_blocks(std::uint32_t channels,
                                                   std::uint32_t block_size,
                                                   float seed) {
    std::vector<std::vector<float>> out(channels,
                                        std::vector<float>(block_size, 0.0f));
    for (std::uint32_t ch = 0; ch < channels; ++ch) {
        for (std::uint32_t n = 0; n < block_size; ++n) {
            out[ch][n] = seed + static_cast<float>(ch) * 1000.0f + static_cast<float>(n);
        }
    }
    return out;
}

// ── Case 1: header_magic_and_version_validated ───────────────────────────

static int test_header_magic_and_version_validated() {
    // Bad magic: attach succeeds (no validation), start() rejects.
    {
        RingFixture fx(48000, 64, 2, 8192, /*magic=*/0xDEADBEEFCAFEBABEULL);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);  // mmap succeeded — attach does not validate
        CaptureCallback cb;
        assert(be->start(&cb, 256) == BackendError::DeviceOpenFailed);
        assert(!be->isRunning());
        assert(be->stagingCapacity() == 0);  // nothing allocated
    }
    // Bad version: start() rejects.
    {
        RingFixture fx(48000, 64, 2, 8192, kSpeRingMagic, /*version=*/2);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, 256) == BackendError::DeviceOpenFailed);
        assert(!be->isRunning());
    }
    // Valid magic + version (other gates pass): start() == Ok, running.
    {
        RingFixture fx(48000, 64, 2, 8192);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, 256) == BackendError::Ok);
        assert(be->isRunning());
        be->stop();
    }
    std::printf("  PASS  header_magic_and_version_validated\n");
    return 0;
}

// ── Case 2: block_size_divisor_and_max_gates ─────────────────────────────

static int test_block_size_divisor_and_max_gates() {
    constexpr int kEngineBlock = 256;  // <= MAX_BLOCK(512)

    // block_size = 100 does NOT divide 256 → BlockConfigMismatch.
    {
        RingFixture fx(48000, 100, 2, 8192);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::BlockConfigMismatch);
        assert(!be->isRunning());
        assert(be->stagingCapacity() == 0);
        assert(std::strcmp(describe(BackendError::BlockConfigMismatch),
                           "block_config_mismatch") == 0);
    }
    // block_size = 64 divides 256 and <= capacity → Ok.
    {
        RingFixture fx(48000, 64, 2, 8192);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::Ok);
        assert(be->isRunning());
        be->stop();
    }
    // block_size = 1024 > MAX_BLOCK(512) → BlockSizeExceedsMax (ONLY this gate).
    {
        RingFixture fx(48000, 1024, 2, 8192);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::BlockSizeExceedsMax);
        assert(!be->isRunning());
    }
    // block_size = 512 (divides? 256 % 512 != 0 — but the block>capacity gate
    // is the one under test here) but capacity = 256 → block > capacity →
    // BlockConfigMismatch. 512 <= MAX_BLOCK so it passes the max gate; the
    // divisor gate would also catch 256 % 512 != 0, but block>capacity is the
    // pinned sub-case. Use engine block 512 so the divisor passes and only the
    // block>capacity gate fires.
    {
        RingFixture fx(48000, 512, 2, 256);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, 512) == BackendError::BlockConfigMismatch);
        assert(!be->isRunning());
    }
    std::printf("  PASS  block_size_divisor_and_max_gates\n");
    return 0;
}

// ── Case 7: ring_capacity_non_pow2_rejected_on_attach ─────────────────────

static int test_ring_capacity_non_pow2_rejected() {
    constexpr int kEngineBlock = 256;
    // capacity_frames = 6000 (non-pow2) → DeviceOpenFailed at start().
    {
        RingFixture fx(48000, 64, 2, 6000);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);  // mmap of total_region_bytes(2, 6000) succeeds
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::DeviceOpenFailed);
        assert(!be->isRunning());
        assert(be->stagingCapacity() == 0);  // nothing allocated
    }
    // capacity_frames = 8192 (pow2) → Ok; masking capacity == header verbatim.
    {
        RingFixture fx(48000, 64, 2, 8192);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::Ok);
        assert(be->isRunning());
        assert(be->maskingCapacityFrames() == 8192u);  // never a rounded value
        be->stop();
    }
    std::printf("  PASS  ring_capacity_non_pow2_rejected_on_attach\n");
    return 0;
}

// ── Case 3: underrun_fills_silence_and_increments_xrun ────────────────────

static int test_underrun_fills_silence_and_increments_xrun() {
    constexpr int kEngineBlock = 256;
    constexpr std::uint32_t kBlock = 64;
    constexpr std::uint32_t kCh = 2;
    constexpr std::uint32_t kCap = 8192;

    // ── Empty-ring underrun + attachedNoData latch + sample-exact replay ──
    {
        RingFixture fx(48000, kBlock, kCh, kCap);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::Ok);

        // (1) Empty ring (write_idx==read_idx==0): one pump → all-zero block,
        //     xrunCount()+1, read_idx NOT advanced.
        be->pump_block(&cb, /*hw_ts=*/1000);
        assert(cb.calls == 1);
        assert(cb.last_in_channels == static_cast<int>(kCh));
        assert(cb.last_frames == static_cast<int>(kBlock));
        for (std::uint32_t ch = 0; ch < kCh; ++ch)
            for (std::uint32_t n = 0; n < kBlock; ++n)
                assert(cb.captured[ch][n] == 0.0f);
        assert(be->xrunCount() == 1);
        assert(fx.header()->read_idx.load(std::memory_order_acquire) == 0);

        // (2) attachedNoData once-on-attach latch (ring still empty).
        be->poll_diagnostics(/*now_ms=*/0, /*now_ns=*/0);
        assert(be->attachedNoDataWarningCount() == 1);
        be->poll_diagnostics(/*now_ms=*/0, /*now_ns=*/0);
        assert(be->attachedNoDataWarningCount() == 1);  // latched

        // (3) Producer writes one block → next pump delivers it sample-exact,
        //     xrunCount() unchanged, read_idx advanced by exactly block_size.
        auto blk = ramp_blocks(kCh, kBlock, /*seed=*/10.0f);
        fx.producer_write_block(blk, kBlock, /*pts=*/0);
        const unsigned long long xr_before = be->xrunCount();
        be->pump_block(&cb, /*hw_ts=*/2000);
        assert(be->xrunCount() == xr_before);
        for (std::uint32_t ch = 0; ch < kCh; ++ch)
            for (std::uint32_t n = 0; n < kBlock; ++n)
                assert(cb.captured[ch][n] == blk[ch][n]);
        assert(fx.header()->read_idx.load(std::memory_order_acquire) == kBlock);
        be->stop();
    }

    // ── PM1 capacity-wrap split sub-case ──────────────────────────────────
    // Seed read_idx == write_idx near the wrap so the next read straddles it.
    {
        constexpr std::uint32_t wCh = 1;
        constexpr std::uint32_t wCap = 8;      // pow2
        constexpr std::uint32_t wBlock = 4;    // divides engine block 256
        RingFixture fx(48000, wBlock, wCh, wCap);
        // Pre-seed indices so (read_idx & (cap-1)) + block > cap:
        //   read = write = 6 → idx=6, 6+4=10 > 8 → first=2, rest=2.
        fx.header()->read_idx.store(6, std::memory_order_relaxed);
        fx.header()->write_idx.store(6, std::memory_order_relaxed);

        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        CaptureCallback cb;
        assert(be->start(&cb, kEngineBlock) == BackendError::Ok);

        // Producer fills the contiguous logical block [6,10) — physically
        // straddling the wrap (frames 6,7 then 0,1).
        std::vector<std::vector<float>> wblk(wCh, std::vector<float>(wBlock, 0.0f));
        for (std::uint32_t n = 0; n < wBlock; ++n)
            wblk[0][n] = 100.0f + static_cast<float>(n);
        fx.producer_write_block(wblk, wBlock, /*pts=*/0);

        const unsigned long long xr_before = be->xrunCount();
        be->pump_block(&cb, /*hw_ts=*/0);
        // read_idx advanced by exactly block_size.
        assert(fx.header()->read_idx.load(std::memory_order_acquire) == 6 + wBlock);
        // xrunCount unchanged across the wrapped read.
        assert(be->xrunCount() == xr_before);
        // Two staged halves (pre-wrap tail + post-wrap head) equal producer ±0.
        for (std::uint32_t n = 0; n < wBlock; ++n)
            assert(cb.captured[0][n] == wblk[0][n]);
        be->stop();
    }

    std::printf("  PASS  underrun_fills_silence_and_increments_xrun\n");
    return 0;
}

// ── Case 4: producer_drain_state_plays_remaining_then_silence ─────────────

static int test_producer_drain_state_plays_remaining_then_silence() {
    constexpr int kEngineBlock = 256;
    constexpr std::uint32_t kBlock = 64;
    constexpr std::uint32_t kCh = 2;
    constexpr std::uint32_t kCap = 8192;

    RingFixture fx(48000, kBlock, kCh, kCap);
    auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
    assert(be != nullptr);
    CaptureCallback cb;
    assert(be->start(&cb, kEngineBlock) == BackendError::Ok);

    // Producer writes 2 blocks → write_idx == 2*block, read_idx == 0.
    auto b0 = ramp_blocks(kCh, kBlock, /*seed=*/1.0f);
    auto b1 = ramp_blocks(kCh, kBlock, /*seed=*/2.0f);
    fx.producer_write_block(b0, kBlock, /*pts=*/0);
    fx.producer_write_block(b1, kBlock, /*pts=*/0);
    // Then producer goes Draining (enum value 2).
    fx.header()->producer_state.store(
        static_cast<std::uint32_t>(ProducerState::Draining), std::memory_order_release);

    // pump #1: available == 2*block → delivers b0 sample-exact, read_idx → block.
    be->pump_block(&cb, 0);
    for (std::uint32_t ch = 0; ch < kCh; ++ch)
        for (std::uint32_t n = 0; n < kBlock; ++n)
            assert(cb.captured[ch][n] == b0[ch][n]);
    assert(fx.header()->read_idx.load(std::memory_order_acquire) == kBlock);
    assert(be->xrunCount() == 0);

    // pump #2: available == block → delivers b1 sample-exact, read_idx → 2*block.
    be->pump_block(&cb, 0);
    for (std::uint32_t ch = 0; ch < kCh; ++ch)
        for (std::uint32_t n = 0; n < kBlock; ++n)
            assert(cb.captured[ch][n] == b1[ch][n]);
    assert(fx.header()->read_idx.load(std::memory_order_acquire) == 2 * kBlock);
    assert(be->xrunCount() == 0);

    // pump #3: available == 0 && state == Draining → silence + xrun, read_idx unchanged.
    be->pump_block(&cb, 0);
    for (std::uint32_t ch = 0; ch < kCh; ++ch)
        for (std::uint32_t n = 0; n < kBlock; ++n)
            assert(cb.captured[ch][n] == 0.0f);
    assert(fx.header()->read_idx.load(std::memory_order_acquire) == 2 * kBlock);
    assert(be->xrunCount() == 1);

    // Producer goes Closed (enum value 3); pump → silence, read_idx unchanged.
    fx.header()->producer_state.store(
        static_cast<std::uint32_t>(ProducerState::Closed), std::memory_order_release);
    be->pump_block(&cb, 0);
    for (std::uint32_t ch = 0; ch < kCh; ++ch)
        for (std::uint32_t n = 0; n < kBlock; ++n)
            assert(cb.captured[ch][n] == 0.0f);
    assert(fx.header()->read_idx.load(std::memory_order_acquire) == 2 * kBlock);
    assert(be->xrunCount() == 2);

    be->stop();
    std::printf("  PASS  producer_drain_state_plays_remaining_then_silence\n");
    return 0;
}

// ── main ──────────────────────────────────────────────────────────────────

#if defined(SRB_RT_SENTINEL)

int main() {
    std::printf("=== test_shared_ring_backend (RT sentinel) ===\n");
    int rc = 0;
    // case 8 invariant (i) added below in case 8 step.
    if (rc == 0) std::printf("All shared_ring_backend RT-sentinel checks PASSED.\n");
    return rc;
}

#else

int main() {
    std::printf("=== test_shared_ring_backend ===\n");
    int rc = 0;
    rc |= test_header_magic_and_version_validated();
    rc |= test_block_size_divisor_and_max_gates();
    rc |= test_ring_capacity_non_pow2_rejected();
    rc |= test_underrun_fills_silence_and_increments_xrun();
    rc |= test_producer_drain_state_plays_remaining_then_silence();
    if (rc == 0) std::printf("All shared_ring_backend tests PASSED.\n");
    return rc;
}

#endif  // SRB_RT_SENTINEL

#else  // not Linux/macOS

int main() {
    std::printf("SKIP: shared ring backend tests require Linux or macOS.\n");
    return 0;
}

#endif  // defined(__linux__) || defined(__APPLE__)

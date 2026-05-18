// vst3/tests/test_vst3_intra_plugin_spsc_drain.cpp
// S3 (C4) — A.6 coverage: UDP-thread push → audio-thread pop, 1000 iterations.
//
// Validates:
//   1. 1000 AudioCommands pushed from a worker thread all reach the consumer.
//   2. RT_ASSERT_NO_ALLOC (malloc probe) holds across all 1000 pop() calls.
//   3. UDP push side is allowed to allocate (probe is OFF during push).
//   4. No data corruption: tag and seq fields match across producer/consumer.
//
// rt_alloc_probe.hpp must be included first (defines malloc/free overrides).
#include "rt_alloc_probe.hpp"

#include "AudioCommand.h"
#include "util/SpscRing.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

static int failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",              \
                         msg, __FILE__, __LINE__);                   \
            ++failures;                                              \
        }                                                            \
    } while (0)

int main()
{
    using namespace spe::vst3;
    using namespace spe::util;
    using namespace std::chrono_literals;

    // -----------------------------------------------------------------------
    // Negative controls: confirm probe fires before main loop.
    // v0.5.2 #2: under ASan the strong-symbol malloc override is skipped
    // (incompatible with ASan interceptors — see rt_alloc_probe.hpp).
    // Degrade these probes to SKIPPED-ASAN; the non-ASan ctest is the
    // authoritative RT-alloc gate.
    // -----------------------------------------------------------------------
#ifdef __SANITIZE_ADDRESS__
    std::printf("PASS probe_observes_malloc (SKIPPED-ASAN)\n");
    std::printf("PASS probe_observes_calloc (SKIPPED-ASAN)\n");
    // This test uses a `failures` counter (CHECK macro); SKIPPED-ASAN
    // is a no-op for the counter.
#else
    {
        g_rt_guard_active = true;
        g_alloc_count     = 0;
        void* p = std::malloc(64);
        g_rt_guard_active = false;
        std::free(p);
        CHECK(g_alloc_count == 1, "probe_observes_malloc");
    }
    {
        g_rt_guard_active = true;
        g_alloc_count     = 0;
        void* p = std::calloc(1, 64);
        g_rt_guard_active = false;
        std::free(p);
        CHECK(g_alloc_count == 1, "probe_observes_calloc");
    }
#endif

    // -----------------------------------------------------------------------
    // Test: 1000-iter SPSC drain, alloc == 0 on pop side.
    // -----------------------------------------------------------------------
    constexpr int kIters = 1000;

    SpscRing<AudioCommand, 1024> ring;

    std::atomic<bool> producer_done{false};
    std::atomic<int>  push_count{0};

    // Producer thread (simulates UDP thread — allowed to allocate).
    std::thread producer([&]() {
        for (int i = 0; i < kIters; ++i) {
            AudioCommand ac{};
            ac.tag = spe::ipc::CommandTag::ObjMove;
            ac.seq = static_cast<uint32_t>(i);
            ac.payload.obj_move.obj_id = static_cast<uint32_t>(i);
            ac.payload.obj_move.az_rad = static_cast<float>(i) * 0.001f;

            // Retry until ring has space (should be instant in practice).
            while (!ring.push(ac)) {
                std::this_thread::yield();
            }
            push_count.fetch_add(1, std::memory_order_relaxed);
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer (simulates audio thread) — probe is ACTIVE during pop().
    int received = 0;
    size_t alloc_total = 0;
    bool tag_ok  = true;
    bool seq_ok  = true;
    int last_seq = -1;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (received < kIters) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "TIMEOUT: only %d/%d items received\n",
                         received, kIters);
            break;
        }

        AudioCommand ac{};

        // Arm probe around pop() — must be allocation-free.
        g_rt_guard_active = true;
        g_alloc_count     = 0;
        bool got = ring.pop(ac);
        g_rt_guard_active = false;
        alloc_total += g_alloc_count;

        if (got) {
            if (ac.tag != spe::ipc::CommandTag::ObjMove) tag_ok = false;
            if (static_cast<int>(ac.seq) != last_seq + 1)  seq_ok = false;
            last_seq = static_cast<int>(ac.seq);
            ++received;
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();

    CHECK(received == kIters,     "all_1000_commands_delivered");
    CHECK(alloc_total == 0,       "rt_safety_alloc_zero_1000_pops");
    CHECK(tag_ok,                 "tag_integrity_preserved");
    CHECK(seq_ok,                 "sequence_order_preserved");

    std::printf("test_vst3_intra_plugin_spsc_drain: %s (%d recv, alloc_total=%zu)\n",
                failures == 0 ? "PASS" : "FAIL", received, alloc_total);
    // ASan workaround: Steinberg SDK static dtor raises glibc SIGABRT
    // (munmap_chunk: invalid pointer) before ASan's exit handler runs.
    // quick_exit(0) skips static destruction; per-allocation ASan tracking
    // during the test body is unaffected. See docs/CI_QUARANTINE.md.
    std::quick_exit(failures == 0 ? 0 : 1);
}

// test_osc_outbound_multi_producer.cpp
//
// v0.5.1 hotfix (code-MINOR — multi-producer SPSC ring safety).
//
// sendReply() is documented control-thread-only but in practice runs from
// the control thread (probe/handshake), audio thread (process()), and IO
// thread (1 Hz heartbeat). The pre-fix claim-then-publish pattern leaked
// races; the post-fix CAS reservation + per-slot ready flag must produce
// EXACT accounting across N producer threads × M iterations.
//
// Acceptance:
//   * outboundSent() + outboundDrops() == TotalEnqueueAttempts (exactly,
//     no double-counted or lost replies).
//   * Every successfully-sent slot's payload survived intact (we tag each
//     slot's int payload with the producer's thread-id and a sequence,
//     then verify the recipient counters add up).
//   * sent >= 50% of attempts (producer-side sleeps ensure the drain catches
//     up between sends; without sleep this test only exercises the initial
//     ring fill, not sustained multi-producer contention).
//   * Per-producer FIFO ordering: iteration numbers received per producer
//     are non-decreasing (proves no slot corruption or reordering).
//
// Producer-side sleeps (kProducerSleepMs) ensure the drain catches up between
// send waves, so subsequent iterations actually contend on the CAS rather than
// all hitting a full ring. Without sleep, observed enqueued≈15, drops≈2985.
//
// We use the int-typed sendReply overload (",i") so each producer's payload
// is a compact 32-bit tag = producer_id * kIters + iter. A loopback client
// recvfrom()s replies, decodes the tag, bins by producer ID, tracks the last
// seen iter per producer, and asserts non-decreasing order (FIFO invariant).

#include "ipc/OSCBackend.h"
#include "ipc/Command.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

constexpr int kProducers = 3;
constexpr int kIters     = 1000;
constexpr int kReplyTimeoutMs = 200;
// Per-iteration producer sleep: allows the drain thread to consume queued
// slots between sends, exercising sustained multi-producer CAS contention
// rather than just the initial ring-fill wave.
constexpr int kProducerSleepMs = 2;

// Bind a UDP socket to 127.0.0.1 on an ephemeral port. Returns (fd, port).
struct LoopbackSocket {
    int      fd   = -1;
    uint16_t port = 0;
};

LoopbackSocket bindLoopback() {
    LoopbackSocket s;
    s.fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s.fd < 0) return s;
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (::bind(s.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s.fd);
        s.fd = -1;
        return s;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(s.fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    s.port = ntohs(addr.sin_port);
    // 100 ms recv timeout — the client thread loops on timeout to drain.
    struct timeval tv{0, 100 * 1000};
    ::setsockopt(s.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

// Build /sys/handshake ,i <schema> packet.
std::vector<uint8_t> buildHandshakePacket(int32_t schema) {
    std::vector<uint8_t> pkt;
    auto pushPadded = [&](const char* s) {
        size_t len = std::strlen(s) + 1;
        for (size_t i = 0; i < len; ++i) pkt.push_back(static_cast<uint8_t>(s[i]));
        while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    pushPadded("/sys/handshake");
    pushPadded(",i");
    const uint32_t u = static_cast<uint32_t>(schema);
    pkt.push_back((u >> 24) & 0xFF);
    pkt.push_back((u >> 16) & 0xFF);
    pkt.push_back((u >>  8) & 0xFF);
    pkt.push_back((u >>  0) & 0xFF);
    return pkt;
}

// Decode an int32 OSC arg from a /sys/binaural_status ,i <int> reply.
// Returns true on success; tag stored in *out.
bool decodeIntReply(const uint8_t* buf, ssize_t n, int32_t* out) {
    if (n < 12) return false;
    // Address is null-terminated, 4-byte aligned.
    size_t off = 0;
    while (off < static_cast<size_t>(n) && buf[off] != 0) ++off;
    if (off == static_cast<size_t>(n)) return false;
    // Skip nulls + padding to 4-byte boundary.
    while (off < static_cast<size_t>(n) && (off % 4 != 0 || buf[off] == 0)) {
        if (buf[off] != 0) break;
        ++off;
    }
    // Type tag string starts with ','.
    if (off >= static_cast<size_t>(n) || buf[off] != ',') return false;
    size_t tstart = off;
    while (off < static_cast<size_t>(n) && buf[off] != 0) ++off;
    if (off == static_cast<size_t>(n)) return false;
    // Verify ",i".
    if (off - tstart < 2 || buf[tstart + 1] != 'i') return false;
    // Skip nulls + padding to 4-byte boundary.
    while (off < static_cast<size_t>(n) && off % 4 != 0) ++off;
    if (off + 4 > static_cast<size_t>(n)) return false;
    const uint32_t u =
        (static_cast<uint32_t>(buf[off + 0]) << 24) |
        (static_cast<uint32_t>(buf[off + 1]) << 16) |
        (static_cast<uint32_t>(buf[off + 2]) <<  8) |
        (static_cast<uint32_t>(buf[off + 3])      );
    *out = static_cast<int32_t>(u);
    return true;
}

} // namespace

int main()
{
    // 1. Engine OSCBackend on ephemeral port.
    LoopbackSocket scout = bindLoopback();
    const uint16_t engine_port = scout.port;
    ::close(scout.fd);

    auto sink = [](const spe::ipc::Command&) {};
    spe::ipc::OSCBackend backend(sink, static_cast<int>(engine_port));
    backend.start();

    // 2. Client socket — receives all replies.
    LoopbackSocket client = bindLoopback();
    if (client.fd < 0) {
        std::fprintf(stderr, "FAIL: client bind failed\n");
        return 1;
    }

    // Give listener thread a beat.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // 3. Send a handshake so backend captures last_peer_endpoint_.
    auto pkt = buildHandshakePacket(1);
    struct sockaddr_in engine_addr{};
    engine_addr.sin_family      = AF_INET;
    engine_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    engine_addr.sin_port        = htons(engine_port);
    ::sendto(client.fd, pkt.data(), pkt.size(), 0,
             reinterpret_cast<struct sockaddr*>(&engine_addr), sizeof(engine_addr));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.hasPeerEndpoint()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!backend.hasPeerEndpoint()) {
        std::fprintf(stderr, "FAIL: backend never captured peer endpoint\n");
        backend.stop();
        ::close(client.fd);
        return 1;
    }

    // 4. Receiver thread — drains replies, bins by encoded producer ID.
    //    Also tracks per-producer last-seen iter to verify FIFO ordering.
    std::atomic<bool> receiver_running{true};
    std::atomic<int> received_total{0};
    std::vector<std::atomic<int>> per_producer_counts(kProducers);
    for (auto& c : per_producer_counts) c.store(0);
    std::atomic<int> corrupted_count{0};
    std::atomic<int> out_of_range_count{0};
    // Last iter seen per producer; -1 = none seen yet. Protected by receiver
    // thread (single consumer), read by main after join.
    std::vector<int> per_producer_last_iter(kProducers, -1);
    std::atomic<int> fifo_violation_count{0};

    std::thread receiver([&]() {
        uint8_t rxbuf[256];
        struct sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        while (receiver_running.load(std::memory_order_acquire)) {
            ssize_t n = ::recvfrom(client.fd, rxbuf, sizeof(rxbuf), 0,
                                   reinterpret_cast<struct sockaddr*>(&from), &fromlen);
            if (n <= 0) continue;
            int32_t tag = 0;
            if (!decodeIntReply(rxbuf, n, &tag)) {
                corrupted_count.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            // tag = producer_id * kIters + iter; producer_id ∈ [0, kProducers).
            const int producer_id = tag / kIters;
            const int iter        = tag % kIters;
            if (producer_id < 0 || producer_id >= kProducers ||
                iter < 0 || iter >= kIters) {
                out_of_range_count.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            // FIFO check: iter must be >= last seen iter for this producer.
            const int last = per_producer_last_iter[static_cast<std::size_t>(producer_id)];
            if (last >= 0 && iter < last) {
                fifo_violation_count.fetch_add(1, std::memory_order_relaxed);
            }
            per_producer_last_iter[static_cast<std::size_t>(producer_id)] = iter;
            per_producer_counts[static_cast<std::size_t>(producer_id)]
                .fetch_add(1, std::memory_order_relaxed);
            received_total.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // 5. Launch kProducers threads, each calling sendReply 1000 times.
    //    A kProducerSleepMs sleep between sends lets the drain thread consume
    //    queued slots, ensuring subsequent iterations contend on the CAS
    //    rather than all hitting a full ring (which would give enqueued≈15).
    std::vector<std::thread> producers;
    std::atomic<int> total_attempts{0};
    std::atomic<int> total_enqueued{0};
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < kIters; ++i) {
                const int32_t tag = p * kIters + i;
                total_attempts.fetch_add(1, std::memory_order_relaxed);
                if (backend.sendReply("/sys/binaural_status", ",i", tag)) {
                    total_enqueued.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(kProducerSleepMs));
            }
        });
    }
    for (auto& t : producers) t.join();

    // 6. Wait for the drain to finish + receiver to collect.
    //    sendReply may have dropped some due to ring-full; outbound_sent_ +
    //    outbound_drops_ should still equal total_attempts exactly.
    //
    //    Drain loop runs every 5ms; total_enqueued can be up to 3000 / 16 ≈
    //    188 drain cycles, ~1 second worst case. Give it 5s headroom.
    // kProducers × kIters × kProducerSleepMs ≈ 6 s; add 4 s headroom.
    auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < wait_deadline) {
        const std::uint64_t sent = backend.outboundSent();
        const std::uint64_t drops = backend.outboundDrops();
        if (sent + drops >= static_cast<std::uint64_t>(total_attempts.load())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Give the receiver a moment to drain the OS UDP queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop receiver.
    receiver_running.store(false, std::memory_order_release);
    // Shut down client socket so recvfrom unblocks immediately.
    ::shutdown(client.fd, SHUT_RDWR);
    receiver.join();
    ::close(client.fd);

    backend.stop();

    // ── Assertions ──────────────────────────────────────────────────────
    const std::uint64_t sent      = backend.outboundSent();
    const std::uint64_t drops     = backend.outboundDrops();
    const int           attempts  = total_attempts.load();
    const int           enq_ok    = total_enqueued.load();

    std::printf("[multi_producer] attempts=%d enqueued=%d sent=%llu drops=%llu "
                "received=%d corrupted=%d out_of_range=%d fifo_violations=%d\n",
                attempts, enq_ok,
                static_cast<unsigned long long>(sent),
                static_cast<unsigned long long>(drops),
                received_total.load(),
                corrupted_count.load(),
                out_of_range_count.load(),
                fifo_violation_count.load());

    if (attempts != kProducers * kIters) {
        std::fprintf(stderr,
            "FAIL attempts %d != expected %d\n", attempts, kProducers * kIters);
        return 1;
    }
    // Each attempt either succeeds (enq_ok ∋ ring-claim success) or drops
    // (ring full / encode fail / no peer). sent + drops counts the total
    // enqueue accounting from the BACKEND side. They must agree exactly.
    if (static_cast<std::uint64_t>(attempts) != sent + drops) {
        std::fprintf(stderr,
            "FAIL accounting mismatch: attempts=%d, sent+drops=%llu\n",
            attempts,
            static_cast<unsigned long long>(sent + drops));
        return 1;
    }
    if (corrupted_count.load() != 0) {
        std::fprintf(stderr, "FAIL %d corrupted slots observed\n",
                     corrupted_count.load());
        return 1;
    }
    if (out_of_range_count.load() != 0) {
        std::fprintf(stderr, "FAIL %d out-of-range tags observed\n",
                     out_of_range_count.load());
        return 1;
    }
    if (fifo_violation_count.load() != 0) {
        std::fprintf(stderr, "FAIL %d FIFO ordering violations observed "
                     "(per-producer iter sequence was not non-decreasing)\n",
                     fifo_violation_count.load());
        return 1;
    }
    // Every producer's per-bin count must be <= kIters (no double-count).
    for (int p = 0; p < kProducers; ++p) {
        const int c = per_producer_counts[static_cast<std::size_t>(p)].load();
        if (c > kIters) {
            std::fprintf(stderr,
                "FAIL producer %d delivered %d > %d\n", p, c, kIters);
            return 1;
        }
    }
    // With producer-side sleeps, the drain catches up between sends and
    // sent should be >= 50% of total attempts (sustained contention, not
    // just initial ring fill). This is a stronger signal than the no-sleep
    // case where enqueued≈15 and drops≈2985.
    if (static_cast<std::uint64_t>(attempts) / 2 > sent) {
        std::fprintf(stderr,
            "FAIL sent=%llu is < 50%% of attempts=%d — "
            "producer sleeps may be too short or drain too slow\n",
            static_cast<unsigned long long>(sent), attempts);
        return 1;
    }
    // Sanity: at least SOME replies arrived. We can't insist on every reply
    // arriving (UDP loopback can drop under load + ring drops) but in
    // practice a 16-slot ring + 2 ms drain cadence + 1000-iter producer
    // delivers most successful enqueues. Require at least 50% of enqueued
    // count to have round-tripped.
    if (received_total.load() < enq_ok / 2) {
        std::fprintf(stderr,
            "FAIL received=%d is < enq_ok/2 (%d)\n",
            received_total.load(), enq_ok / 2);
        return 1;
    }

    std::printf("PASS test_osc_outbound_multi_producer "
                "(%d producers × %d iters, sent+drops=%llu exact)\n",
                kProducers, kIters,
                static_cast<unsigned long long>(sent + drops));
    return 0;
}

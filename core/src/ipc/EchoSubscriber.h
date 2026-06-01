// core/src/ipc/EchoSubscriber.h
// M5.1 -- ADM-OSC echo plane (port 9102).
//
// EchoPlane manages a registry of up to kMaxEchoSubscribers outbound UDP
// destinations that receive a re-emission of each inbound ADM-OSC object /
// transport message once per OSC-IO-thread "tick".
//
// Design (from M5 plan SS2):
//   - Addresses echoed: /adm/obj/N/{aed,xyz,gain,mute,active,width,name}
//     and /transport/{play,stop}.
//   - Coalesce: same (obj_id, addr-enum) within one flush() call -> latest.
//     Dirty-bit map: kEchoMaxObjects objects x 7 addresses (v0.9 Lane C:
//     kEchoMaxObjects derives from spe::MAX_OBJECTS — 64 → 448 bits / 128 →
//     896 bits).
//   - Values: stored per (obj, addr) so flush() can re-emit without needing
//     the audio-thread obj_cache_. Echo emits wire-level idempotent values
//     (inbound values, not engine-internal spherical conversions).
//   - Rate guard: 5000 pkts/s ceiling per subscriber. Excess -> drop + one
//     /sys/warning ,iis 0 0 "echo_rate_limited" "dropped=N" per second on
//     the 9101 control plane via send_fd.
//   - Subscriber TTL: no /hb/ping within 30 s -> silent eviction.
//
// Thread model: ALL methods are called on the OSC IO thread (the UDP receive
// thread inside OSCBackend). mark*() is called from the OSC sink lambda;
// flush() is called at end of each sink invocation. No audio-thread access.

#pragma once

#include "core/Constants.h"
#include "ipc/Command.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

namespace spe::ipc {

// Number of echo address types per object.
static constexpr int kEchoAddrCount = 7;

enum class EchoAddr : uint8_t {
    Aed    = 0,
    Xyz    = 1,
    Gain   = 2,
    Mute   = 3,
    Active = 4,
    Width  = 5,
    Name   = 6,
};

static constexpr std::size_t kMaxEchoSubscribers = 4;
// v0.9 Lane C: echo cache + dirty-bit map cap derives from the canonical cap.
static constexpr std::size_t kEchoMaxObjects =
    static_cast<std::size_t>(spe::MAX_OBJECTS);
static constexpr int         kEchoRateLimit      = 5000;
static constexpr int64_t     kEchoSubscriberTtlMs = 30000;

// Per-object cached inbound values for echo.
struct EchoObjCache {
    float az_deg   = 0.f;
    float el_deg   = 0.f;
    float dist_norm = 0.f;
    float x = 0.f, y = 0.f, z = 0.f;
    float gain      = 1.f;
    int   muted     = 0;
    int   active    = 0;
    float width_rad = 0.f;
    char  name[32]  = {};
};

// Dirty-bit map: bit[obj][addr] set when a pending echo exists.
struct EchoDirtyMap {
    static constexpr std::size_t kBitsPerObj   = kEchoAddrCount;
    static constexpr std::size_t kTotalObjBits = kEchoMaxObjects * kBitsPerObj;
    static constexpr std::size_t kObjBytes     = (kTotalObjBits + 7) / 8;

    uint8_t obj_bits[kObjBytes] = {};
    bool    transport_play      = false;
    bool    transport_stop      = false;

    void clear() noexcept {
        std::memset(obj_bits, 0, sizeof(obj_bits));
        transport_play = false;
        transport_stop = false;
    }

    void mark(uint32_t obj_id, EchoAddr addr) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        const std::size_t bit = static_cast<std::size_t>(obj_id) * kBitsPerObj +
                                static_cast<std::size_t>(addr);
        obj_bits[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
    }

    bool test(uint32_t obj_id, EchoAddr addr) const noexcept {
        if (obj_id >= kEchoMaxObjects) return false;
        const std::size_t bit = static_cast<std::size_t>(obj_id) * kBitsPerObj +
                                static_cast<std::size_t>(addr);
        return (obj_bits[bit / 8] & static_cast<uint8_t>(1u << (bit % 8))) != 0;
    }
};

// Echo subscriber descriptor.
struct EchoSubscriberEntry {
    struct sockaddr_in dest {};
    char    tag[64]             = {};
    int64_t attach_time_ms      = 0;
    int64_t last_hb_ms          = 0;
    int64_t rate_window_start_ms = 0;
    int     tokens_this_second  = 0;
    int     dropped_count       = 0;
    bool    active              = false;
};

// EchoPlane: subscriber registry + dirty-bit coalesce + rate guard.
// All methods: OSC IO thread only.
class EchoPlane {
public:
    EchoPlane()  = default;
    ~EchoPlane() = default;

    // Enable echo plane (called from OSCBackend::start()).
    void open() noexcept { open_ = true; }
    // Disable and evict all subscribers (called from OSCBackend::stop()).
    void close() noexcept {
        open_ = false;
        for (auto& e : entries_) e.active = false;
        dirty_.clear();
    }

    // Register a subscriber (called from SysHandshake handler on IO thread).
    // peer_ip_net: IPv4 in network byte order.
    // echo_port: destination port in host byte order.
    bool addSubscriber(uint32_t peer_ip_net, uint16_t echo_port, const char* tag,
                       int64_t now_ms) noexcept;

    // Update heartbeat timestamp for subscribers from peer_ip.
    void touchSubscriberHb(uint32_t peer_ip_net, int64_t now_ms) noexcept;

    // Evict stale subscribers (> 30 s without /hb/ping).
    void evictStale(int64_t now_ms) noexcept;

    // Mark + store inbound values. Called from OSC sink on IO thread.
    // These store the value in obj_cache_[] AND set the dirty bit.
    void markAed(uint32_t obj_id, float az_deg, float el_deg,
                 float dist_norm) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        auto& c       = obj_cache_[obj_id];
        c.az_deg      = az_deg;
        c.el_deg      = el_deg;
        c.dist_norm   = dist_norm;
        dirty_.mark(obj_id, EchoAddr::Aed);
    }
    void markXyz(uint32_t obj_id, float x, float y, float z) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        auto& c  = obj_cache_[obj_id];
        c.x = x; c.y = y; c.z = z;
        dirty_.mark(obj_id, EchoAddr::Xyz);
    }
    void markGain(uint32_t obj_id, float gain) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        obj_cache_[obj_id].gain = gain;
        dirty_.mark(obj_id, EchoAddr::Gain);
    }
    void markMute(uint32_t obj_id, int muted) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        obj_cache_[obj_id].muted = muted;
        dirty_.mark(obj_id, EchoAddr::Mute);
    }
    void markActive(uint32_t obj_id, int active) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        obj_cache_[obj_id].active = active;
        dirty_.mark(obj_id, EchoAddr::Active);
    }
    void markWidth(uint32_t obj_id, float width_rad) noexcept {
        if (obj_id >= kEchoMaxObjects) return;
        obj_cache_[obj_id].width_rad = width_rad;
        dirty_.mark(obj_id, EchoAddr::Width);
    }
    void markName(uint32_t obj_id, const char* name) noexcept {
        if (obj_id >= kEchoMaxObjects || !name) return;
        const std::size_t n = std::strlen(name) < 31 ? std::strlen(name) : 31;
        std::memcpy(obj_cache_[obj_id].name, name, n);
        obj_cache_[obj_id].name[n] = '\0';
        dirty_.mark(obj_id, EchoAddr::Name);
    }
    void markTransportPlay() noexcept { dirty_.transport_play = true; }
    void markTransportStop() noexcept { dirty_.transport_stop = true; }

    // Flush all dirty bits to subscribers. Call once per OSC sink invocation.
    // send_fd: OSCBackend's UDP socket fd (or -1 in tests for no-socket path).
    // now_ms: current wall-clock milliseconds.
    void flush(int64_t now_ms, int send_fd) noexcept;

    // Accessors for tests.
    std::size_t subscriberCount() const noexcept {
        std::size_t n = 0;
        for (const auto& e : entries_)
            if (e.active) ++n;
        return n;
    }
    bool hasSubscribers() const noexcept { return subscriberCount() > 0; }
    bool isOpen() const noexcept { return open_; }

    const EchoSubscriberEntry& entryAt(std::size_t i) const noexcept {
        return entries_[i];
    }

    // Expected subscriber tag for echo registration.
    static constexpr const char* kEchoSubscriberTag =
        "echo_subscriber=adm_object_stream";

private:
    bool open_ = false;
    std::array<EchoSubscriberEntry, kMaxEchoSubscribers> entries_ {};
    std::array<EchoObjCache, kEchoMaxObjects>            obj_cache_ {};
    EchoDirtyMap                                          dirty_ {};

    // Build one OSC packet and sendto() each rate-allowed subscriber.
    // Returns packet byte count (0 = encode failure).
    std::size_t buildAndSend(const char* addr, const char* types,
                             const float* floats, int n_floats, const int* ints,
                             int n_ints, const char* str, int64_t now_ms,
                             int send_fd) noexcept;

    // Rate guard. Returns true if sub may send one more packet this window.
    bool allowSend(EchoSubscriberEntry& sub, int64_t now_ms,
                   int send_fd) noexcept;
};

}  // namespace spe::ipc

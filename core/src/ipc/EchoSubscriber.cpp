// core/src/ipc/EchoSubscriber.cpp
// M5.1 -- ADM-OSC echo plane implementation.

#include "ipc/EchoSubscriber.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>

namespace spe::ipc {

// ---- OSC wire helpers -------------------------------------------------------

namespace {

inline void wU32(uint8_t* dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v >> 24);
    dst[1] = static_cast<uint8_t>(v >> 16);
    dst[2] = static_cast<uint8_t>(v >> 8);
    dst[3] = static_cast<uint8_t>(v);
}

inline void wF32(uint8_t* dst, float v) noexcept {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    wU32(dst, u);
}

// Append null-padded OSC string to buf[off]. Returns new offset or 0 on overflow.
std::size_t appendOscStr(uint8_t* buf, std::size_t cap, std::size_t off,
                         const char* s) noexcept {
    const std::size_t slen  = s ? std::strlen(s) : 0;
    const std::size_t total = (slen + 4) & ~std::size_t(3);
    if (off + total > cap) return 0;
    if (slen) std::memcpy(buf + off, s, slen);
    std::memset(buf + off + slen, 0, total - slen);
    return off + total;
}

}  // namespace

// ---- Subscriber registry ----------------------------------------------------

bool EchoPlane::addSubscriber(uint32_t peer_ip_net, uint16_t echo_port,
                               const char* tag, int64_t now_ms) noexcept {
    if (!open_) return false;
    if (echo_port < 1024) return false;

    // Refresh if already registered.
    for (auto& e : entries_) {
        if (e.active && e.dest.sin_addr.s_addr == peer_ip_net &&
            ntohs(e.dest.sin_port) == echo_port) {
            e.last_hb_ms     = now_ms;
            e.attach_time_ms = now_ms;
            return true;
        }
    }

    // Find free slot.
    for (auto& e : entries_) {
        if (!e.active) {
            std::memset(&e, 0, sizeof(e));
            e.dest.sin_family      = AF_INET;
            e.dest.sin_addr.s_addr = peer_ip_net;
            e.dest.sin_port        = htons(echo_port);
            if (tag) {
                const std::size_t n = std::strlen(tag) < 63 ? std::strlen(tag) : 63;
                std::memcpy(e.tag, tag, n);
                e.tag[n] = '\0';
            }
            e.attach_time_ms       = now_ms;
            e.last_hb_ms           = now_ms;
            e.rate_window_start_ms = now_ms;
            e.active               = true;
            return true;
        }
    }
    return false;  // full
}

void EchoPlane::touchSubscriberHb(uint32_t peer_ip_net,
                                   int64_t now_ms) noexcept {
    for (auto& e : entries_) {
        if (e.active && e.dest.sin_addr.s_addr == peer_ip_net) {
            e.last_hb_ms = now_ms;
        }
    }
}

void EchoPlane::evictStale(int64_t now_ms) noexcept {
    for (auto& e : entries_) {
        if (e.active && (now_ms - e.last_hb_ms) > kEchoSubscriberTtlMs) {
            e.active = false;
        }
    }
}

// ---- Rate guard -------------------------------------------------------------

bool EchoPlane::allowSend(EchoSubscriberEntry& sub, int64_t now_ms,
                           int send_fd) noexcept {
    if (now_ms - sub.rate_window_start_ms >= 1000) {
        // Emit /sys/warning if we dropped packets in the last window.
        if (sub.dropped_count > 0 && send_fd >= 0) {
            char detail[32];
            std::snprintf(detail, sizeof(detail), "dropped=%d",
                          sub.dropped_count);

            uint8_t buf[256];
            std::size_t off = 0;
            off = appendOscStr(buf, sizeof(buf), off, "/sys/warning");
            if (off) off = appendOscStr(buf, sizeof(buf), off, ",iis");
            if (off && off + 8 <= sizeof(buf)) {
                wU32(buf + off, 0u);
                off += 4;
                wU32(buf + off, 0u);
                off += 4;
            }
            if (off) off = appendOscStr(buf, sizeof(buf), off, "echo_rate_limited");
            if (off) off = appendOscStr(buf, sizeof(buf), off, detail);

            if (off > 0) {
                // Send to subscriber IP on control port 9101.
                struct sockaddr_in ctrl = sub.dest;
                ctrl.sin_port = htons(9101);
                ::sendto(send_fd, buf, static_cast<int>(off), 0,
                         reinterpret_cast<const struct sockaddr*>(&ctrl),
                         sizeof(ctrl));
            }
        }
        sub.rate_window_start_ms = now_ms;
        sub.tokens_this_second   = 0;
        sub.dropped_count        = 0;
    }

    if (sub.tokens_this_second >= kEchoRateLimit) {
        ++sub.dropped_count;
        return false;
    }
    ++sub.tokens_this_second;
    return true;
}

// ---- Packet builder + fan-out -----------------------------------------------

std::size_t EchoPlane::buildAndSend(const char* addr, const char* types,
                                     const float* floats, int n_floats,
                                     const int* ints, int n_ints,
                                     const char* str, int64_t now_ms,
                                     int send_fd) noexcept {
    uint8_t buf[256];
    std::size_t off = 0;

    off = appendOscStr(buf, sizeof(buf), off, addr);
    if (!off) return 0;
    off = appendOscStr(buf, sizeof(buf), off, types);
    if (!off) return 0;

    int fi = 0, ii = 0;
    for (const char* t = types + 1; *t; ++t) {
        if (*t == 'f') {
            if (fi >= n_floats || off + 4 > sizeof(buf)) return 0;
            wF32(buf + off, floats[fi++]);
            off += 4;
        } else if (*t == 'i') {
            if (ii >= n_ints || off + 4 > sizeof(buf)) return 0;
            wU32(buf + off, static_cast<uint32_t>(ints[ii++]));
            off += 4;
        } else if (*t == 's') {
            off = appendOscStr(buf, sizeof(buf), off, str ? str : "");
            if (!off) return 0;
        }
    }

    if (send_fd < 0) return off;  // test path: built but not sent

    for (auto& sub : entries_) {
        if (!sub.active) continue;
        if (!allowSend(sub, now_ms, send_fd)) continue;
        ::sendto(send_fd, buf, static_cast<int>(off), 0,
                 reinterpret_cast<const struct sockaddr*>(&sub.dest),
                 sizeof(sub.dest));
    }
    return off;
}

// ---- flush ------------------------------------------------------------------

void EchoPlane::flush(int64_t now_ms, int send_fd) noexcept {
    if (!hasSubscribers()) {
        dirty_.clear();
        return;
    }

    for (std::size_t obj = 0; obj < kEchoMaxObjects; ++obj) {
        const uint32_t oid = static_cast<uint32_t>(obj);
        const auto&    c   = obj_cache_[obj];

        if (dirty_.test(oid, EchoAddr::Aed)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/aed", oid);
            const float vals[3] = {c.az_deg, c.el_deg, c.dist_norm};
            buildAndSend(addr, ",fff", vals, 3, nullptr, 0, nullptr, now_ms,
                         send_fd);
        }
        if (dirty_.test(oid, EchoAddr::Xyz)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/xyz", oid);
            const float vals[3] = {c.x, c.y, c.z};
            buildAndSend(addr, ",fff", vals, 3, nullptr, 0, nullptr, now_ms,
                         send_fd);
        }
        if (dirty_.test(oid, EchoAddr::Gain)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/gain", oid);
            buildAndSend(addr, ",f", &c.gain, 1, nullptr, 0, nullptr, now_ms,
                         send_fd);
        }
        if (dirty_.test(oid, EchoAddr::Mute)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/mute", oid);
            buildAndSend(addr, ",i", nullptr, 0, &c.muted, 1, nullptr, now_ms,
                         send_fd);
        }
        if (dirty_.test(oid, EchoAddr::Active)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/active", oid);
            buildAndSend(addr, ",i", nullptr, 0, &c.active, 1, nullptr, now_ms,
                         send_fd);
        }
        if (dirty_.test(oid, EchoAddr::Width)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/width", oid);
            buildAndSend(addr, ",f", &c.width_rad, 1, nullptr, 0, nullptr,
                         now_ms, send_fd);
        }
        if (dirty_.test(oid, EchoAddr::Name)) {
            char addr[32];
            std::snprintf(addr, sizeof(addr), "/adm/obj/%u/name", oid);
            buildAndSend(addr, ",s", nullptr, 0, nullptr, 0, c.name, now_ms,
                         send_fd);
        }
    }

    if (dirty_.transport_play) {
        buildAndSend("/transport/play", ",", nullptr, 0, nullptr, 0, nullptr,
                     now_ms, send_fd);
    }
    if (dirty_.transport_stop) {
        buildAndSend("/transport/stop", ",", nullptr, 0, nullptr, 0, nullptr,
                     now_ms, send_fd);
    }

    dirty_.clear();
}

}  // namespace spe::ipc

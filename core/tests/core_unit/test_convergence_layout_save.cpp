// test_convergence_layout_save.cpp
// Dreamscape convergence — Phase 4.3 inc 1: speaker-layout YAML serializer.
//
// Verifies geometry::save_layout round-trips exactly through load_layout:
//   1. load(save(L)) reproduces version/name/regularity and every speaker's
//      channel/x/y/z/delay_ms/gain_db bit-for-bit (max_digits10 precision).
//   2. channel→index mapping is rebuilt identically.
//   3. save to an unwritable path fails gracefully (false, no throw).

#include "geometry/LayoutLoader.h"
#include "geometry/SpeakerLayout.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

using namespace spe::geometry;

static SpeakerLayout make_layout() {
    SpeakerLayout l;
    l.version = "1.0";
    l.name = "rt_test";
    l.regularity = Regularity::CIRCULAR;
    auto add = [&](int ch, float x, float y, float z, float d, float g) {
        Speaker s; s.channel = ch; s.x = x; s.y = y; s.z = z; s.delay_ms = d; s.gain_db = g;
        l.channel_to_idx_[(size_t) ch] = (int16_t) l.speakers.size();
        l.speakers.push_back(s);
    };
    l.channel_to_idx_.fill((int16_t) -1);
    add(1, -0.7071068f, 0.f,  0.7071068f, 0.f,   0.f);
    add(2,  0.7071068f, 0.f,  0.7071068f, 1.25f, -3.5f);   // with delay + gain
    add(5,  0.123456f,  0.9f, -0.42f,     0.f,   2.0f);    // non-contiguous channel
    return l;
}

int main() {
    const std::string path = "/tmp/spe_layout_save_rt.yaml";
    SpeakerLayout orig = make_layout();

    CHECK(save_layout(orig, path), "save_layout succeeds");

    auto res = load_layout(path);
    CHECK(is_ok(res), "reload parses");
    if (!is_ok(res)) { std::printf("[RESULT] FAIL (load)\n"); return 1; }
    const auto& rt = std::get<SpeakerLayout>(res);

    CHECK(rt.version == orig.version, "version round-trips");
    CHECK(rt.name == orig.name, "name round-trips");
    CHECK(rt.regularity == orig.regularity, "regularity round-trips");
    CHECK(rt.speakers.size() == orig.speakers.size(), "speaker count round-trips");

    bool exact = rt.speakers.size() == orig.speakers.size();
    for (size_t i = 0; i < rt.speakers.size() && i < orig.speakers.size(); ++i) {
        const auto& a = orig.speakers[i];
        const auto& b = rt.speakers[i];
        // max_digits10 → bit-exact float reload
        const bool ok = a.channel == b.channel && a.x == b.x && a.y == b.y &&
                        a.z == b.z && a.delay_ms == b.delay_ms && a.gain_db == b.gain_db;
        if (!ok) { exact = false; std::fprintf(stderr, "FAIL: speaker %zu mismatch\n", i); ++failures; }
        // channel→index mapping rebuilt identically
        CHECK(rt.channelToIndex(a.channel) == (int) i, "channel->index mapping preserved");
    }
    CHECK(exact, "all speaker fields bit-exact");
    std::printf("[%s] round-trip %zu speakers (incl. delay/gain, non-contiguous channels)\n",
                exact ? "PASS" : "FAIL", rt.speakers.size());

    // Unwritable path → graceful false (directory does not exist).
    const bool bad = save_layout(orig, "/no/such/dir/layout.yaml");
    CHECK(!bad, "save to unwritable path returns false");
    std::printf("[%s] unwritable path -> false (no throw)\n", !bad ? "PASS" : "FAIL");

    if (failures) { std::printf("[RESULT] FAIL (%d)\n", failures); return 1; }
    std::printf("[RESULT] PASS\n");
    return 0;
}

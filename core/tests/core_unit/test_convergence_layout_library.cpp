// test_convergence_layout_library.cpp
// Dreamscape convergence — Phase 4.3 inc 2a: 50-slot speaker-layout library.
//
// Verifies geometry::LayoutLibrary (pure control-thread file I/O):
//   1. save → occupied + label + load round-trips the layout.
//   2. bad slot indices rejected; loading an empty slot errors gracefully.
//   3. clear empties the slot.
//   4. persistence: a fresh LayoutLibrary over the same dir rescans occupancy
//      + labels from disk.

#include "geometry/LayoutLibrary.h"
#include "geometry/SpeakerLayout.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

using namespace spe::geometry;

static SpeakerLayout make_layout(const char* name, int n) {
    SpeakerLayout l;
    l.version = "1.0"; l.name = name; l.regularity = Regularity::CIRCULAR;
    l.channel_to_idx_.fill((int16_t) -1);
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i + 1;
        s.x = 0.1f * i; s.y = 0.f; s.z = 1.f; s.delay_ms = 0.f; s.gain_db = 0.f;
        l.channel_to_idx_[(size_t)(i + 1)] = (int16_t) l.speakers.size();
        l.speakers.push_back(s);
    }
    return l;
}

int main() {
    const std::string dir = "/tmp/spe_layout_lib_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);          // clean slate

    {
        LayoutLibrary lib(dir);
        CHECK(lib.occupiedCount() == 0, "fresh library empty");

        CHECK(lib.save(3, make_layout("StudioA", 4), "Studio A"), "save slot 3");
        CHECK(lib.occupied(3), "slot 3 occupied");
        CHECK(lib.label(3) == "Studio A", "slot 3 label");
        CHECK(lib.occupiedCount() == 1, "one slot occupied");

        auto r = lib.load(3);
        CHECK(is_ok(r), "load slot 3 parses");
        if (is_ok(r)) {
            const auto& L = std::get<SpeakerLayout>(r);
            CHECK(L.speakers.size() == 4, "loaded 4 speakers");
            CHECK(L.name == "StudioA", "loaded name");
        }
        std::printf("[%s] save/occupied/label/load slot 3\n",
                    (lib.occupied(3) && lib.label(3) == "Studio A" && is_ok(r)) ? "PASS" : "FAIL");

        // bad slots
        CHECK(!lib.save(-1, make_layout("x", 1), "x"), "save slot -1 rejected");
        CHECK(!lib.save(LayoutLibrary::kSlotCount, make_layout("x", 1), "x"), "save slot 50 rejected");
        CHECK(!is_ok(lib.load(7)), "load empty slot 7 errors");
        CHECK(!is_ok(lib.load(99)), "load out-of-range slot errors");
        std::printf("[%s] bad-slot + empty-slot rejected gracefully\n",
                    (!is_ok(lib.load(7)) && !is_ok(lib.load(99))) ? "PASS" : "FAIL");

        // clear
        CHECK(lib.clear(3), "clear slot 3");
        CHECK(!lib.occupied(3), "slot 3 now empty");

        // re-save for the persistence check below
        CHECK(lib.save(12, make_layout("Dome", 8), "Dome 7.1"), "save slot 12");
    }

    // persistence: a fresh library over the same dir rescans from disk
    {
        LayoutLibrary lib2(dir);
        CHECK(lib2.occupied(12), "slot 12 persists across re-open");
        CHECK(lib2.label(12) == "Dome 7.1", "slot 12 label persists");
        CHECK(!lib2.occupied(3), "cleared slot 3 stays empty");
        CHECK(lib2.occupiedCount() == 1, "exactly one occupied after reopen");
        std::printf("[%s] persistence: slot 12 + label survive re-open, slot 3 stays cleared\n",
                    (lib2.occupied(12) && lib2.label(12) == "Dome 7.1" && !lib2.occupied(3)) ? "PASS" : "FAIL");
    }

    std::filesystem::remove_all(dir, ec);
    if (failures) { std::printf("[RESULT] FAIL (%d)\n", failures); return 1; }
    std::printf("[RESULT] PASS\n");
    return 0;
}

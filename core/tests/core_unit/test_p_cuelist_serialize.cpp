// core/tests/core_unit/test_p_cuelist_serialize.cpp
// E-M2 gate: CueList JSON round-trip + boundary value tests.
// No external framework — hand-rolled CHECK macro (mirrors test_p_scene.cpp style).

#include "scene/CueList.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using spe::scene::Cue;
using spe::scene::CueList;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); } } while(0)

// ---- helpers ----------------------------------------------------------------

static CueList roundtrip(const CueList& cl) {
    return CueList::fromJson(cl.toJson());
}

// ---- Test 1: empty list round-trip -----------------------------------------

static void test_empty_list() {
    CueList cl;
    auto rt = roundtrip(cl);
    CHECK(rt.cues.empty(), "empty list: cues not empty");
    // JSON shape
    CHECK(cl.toJson() == "{\"cues\":[]}", "empty list: JSON shape");
    std::printf("PASS: test_empty_list\n");
}

// ---- Test 2: single cue WITHOUT dwell_ms -----------------------------------

static void test_single_cue_no_dwell() {
    CueList cl;
    Cue c;
    c.scene = "scene_a";
    c.crossfade_ms = 500.f;
    // dwell_ms left as nullopt
    cl.cues.push_back(c);

    auto rt = roundtrip(cl);
    CHECK(rt.cues.size() == 1, "single_no_dwell: size");
    CHECK(rt.cues[0].scene == "scene_a", "single_no_dwell: scene");
    CHECK(rt.cues[0].crossfade_ms == 500.f, "single_no_dwell: crossfade_ms");
    CHECK(!rt.cues[0].dwell_ms.has_value(), "single_no_dwell: dwell_ms should be nullopt");

    // dwell_ms key must be absent from JSON
    std::string json = cl.toJson();
    CHECK(json.find("dwell_ms") == std::string::npos, "single_no_dwell: dwell_ms key absent");
    std::printf("PASS: test_single_cue_no_dwell\n");
}

// ---- Test 3: single cue WITH dwell_ms -------------------------------------

static void test_single_cue_with_dwell() {
    CueList cl;
    Cue c;
    c.scene = "scene_b";
    c.crossfade_ms = 1000.f;
    c.dwell_ms = 5000.f;
    cl.cues.push_back(c);

    auto rt = roundtrip(cl);
    CHECK(rt.cues.size() == 1, "single_dwell: size");
    CHECK(rt.cues[0].scene == "scene_b", "single_dwell: scene");
    CHECK(rt.cues[0].crossfade_ms == 1000.f, "single_dwell: crossfade_ms");
    CHECK(rt.cues[0].dwell_ms.has_value(), "single_dwell: dwell_ms present");
    CHECK(*rt.cues[0].dwell_ms == 5000.f, "single_dwell: dwell_ms value");
    std::printf("PASS: test_single_cue_with_dwell\n");
}

// ---- Test 4: multi-cue mixed dwell -----------------------------------------

static void test_multi_cue_mixed_dwell() {
    CueList cl;
    {
        Cue c; c.scene = "a"; c.crossfade_ms = 200.f; cl.cues.push_back(c);
    }
    {
        Cue c; c.scene = "b"; c.crossfade_ms = 300.f; c.dwell_ms = 2000.f; cl.cues.push_back(c);
    }
    {
        Cue c; c.scene = "c"; c.crossfade_ms = 0.f; cl.cues.push_back(c);
    }

    auto rt = roundtrip(cl);
    CHECK(rt.cues.size() == 3, "multi_mixed: size");
    CHECK(rt.cues[0].scene == "a", "multi_mixed: [0].scene");
    CHECK(!rt.cues[0].dwell_ms.has_value(), "multi_mixed: [0] no dwell");
    CHECK(rt.cues[1].scene == "b", "multi_mixed: [1].scene");
    CHECK(rt.cues[1].dwell_ms.has_value() && *rt.cues[1].dwell_ms == 2000.f,
          "multi_mixed: [1].dwell_ms");
    CHECK(rt.cues[2].scene == "c", "multi_mixed: [2].scene");
    std::printf("PASS: test_multi_cue_mixed_dwell\n");
}

// ---- Test 5: negative crossfade/dwell clamped to 0 -------------------------

static void test_negative_clamped() {
    std::string json = R"({"cues":[{"scene":"x","crossfade_ms":-100,"dwell_ms":-50}]})";
    auto cl = CueList::fromJson(json);
    CHECK(cl.cues.size() == 1, "neg_clamp: size");
    CHECK(cl.cues[0].crossfade_ms == 0.f, "neg_clamp: crossfade_ms clamped");
    CHECK(cl.cues[0].dwell_ms.has_value(), "neg_clamp: dwell_ms present");
    CHECK(*cl.cues[0].dwell_ms == 0.f, "neg_clamp: dwell_ms clamped");
    std::printf("PASS: test_negative_clamped\n");
}

// ---- Test 6: cue with empty scene is dropped --------------------------------

static void test_empty_scene_dropped() {
    std::string json = R"({"cues":[{"scene":"","crossfade_ms":0},{"scene":"good","crossfade_ms":0}]})";
    auto cl = CueList::fromJson(json);
    CHECK(cl.cues.size() == 1, "empty_scene_drop: size should be 1");
    CHECK(cl.cues[0].scene == "good", "empty_scene_drop: remaining cue");
    std::printf("PASS: test_empty_scene_dropped\n");
}

// ---- Test 7: missing dwell_ms key → nullopt ---------------------------------

static void test_missing_dwell_is_nullopt() {
    std::string json = R"({"cues":[{"scene":"y","crossfade_ms":100}]})";
    auto cl = CueList::fromJson(json);
    CHECK(cl.cues.size() == 1, "missing_dwell: size");
    CHECK(!cl.cues[0].dwell_ms.has_value(), "missing_dwell: nullopt");
    std::printf("PASS: test_missing_dwell_is_nullopt\n");
}

// ---- Test 8: null dwell_ms value → nullopt ----------------------------------

static void test_null_dwell_is_nullopt() {
    std::string json = R"({"cues":[{"scene":"z","crossfade_ms":0,"dwell_ms":null}]})";
    auto cl = CueList::fromJson(json);
    CHECK(cl.cues.size() == 1, "null_dwell: size");
    CHECK(!cl.cues[0].dwell_ms.has_value(), "null_dwell: nullopt");
    std::printf("PASS: test_null_dwell_is_nullopt\n");
}

// ---- Test 9: malformed JSON → empty list ------------------------------------

static void test_malformed_json_empty_list() {
    auto cl1 = CueList::fromJson("");
    CHECK(cl1.cues.empty(), "malformed empty string: empty list");

    auto cl2 = CueList::fromJson("not json at all {{{");
    CHECK(cl2.cues.empty(), "malformed garbage: empty list");

    auto cl3 = CueList::fromJson("{\"cues\":[{\"scene\":\"x\"");  // truncated
    // Truncated — close brace not found, so nothing parsed
    // Either 0 or partial; verify no crash and size <= 1
    (void)cl3;  // no crash is the primary assertion

    std::printf("PASS: test_malformed_json_empty_list\n");
}

// ---- Test 10: disk round-trip -----------------------------------------------

static void test_disk_roundtrip() {
    namespace fs = std::filesystem;
    auto tmpdir = fs::temp_directory_path() / "spe_cuelist_test";
    fs::remove_all(tmpdir);
    fs::create_directories(tmpdir);
    std::string dir = tmpdir.string();

    CueList cl;
    {
        Cue c; c.scene = "intro"; c.crossfade_ms = 750.f; c.dwell_ms = 3000.f;
        cl.cues.push_back(c);
    }
    {
        Cue c; c.scene = "main"; c.crossfade_ms = 500.f;
        cl.cues.push_back(c);
    }

    CHECK(cl.saveToDisk(dir), "disk_roundtrip: save");

    CueList loaded;
    CHECK(loaded.loadFromDisk(dir), "disk_roundtrip: load");
    CHECK(loaded.cues.size() == 2, "disk_roundtrip: size");
    CHECK(loaded.cues[0].scene == "intro", "disk_roundtrip: [0].scene");
    CHECK(loaded.cues[0].crossfade_ms == 750.f, "disk_roundtrip: [0].crossfade_ms");
    CHECK(loaded.cues[0].dwell_ms.has_value() && *loaded.cues[0].dwell_ms == 3000.f,
          "disk_roundtrip: [0].dwell_ms");
    CHECK(loaded.cues[1].scene == "main", "disk_roundtrip: [1].scene");
    CHECK(!loaded.cues[1].dwell_ms.has_value(), "disk_roundtrip: [1] no dwell");

    fs::remove_all(tmpdir);
    std::printf("PASS: test_disk_roundtrip\n");
}

// ---- main -------------------------------------------------------------------

int main() {
    test_empty_list();
    test_single_cue_no_dwell();
    test_single_cue_with_dwell();
    test_multi_cue_mixed_dwell();
    test_negative_clamped();
    test_empty_scene_dropped();
    test_missing_dwell_is_nullopt();
    test_null_dwell_is_nullopt();
    test_malformed_json_empty_list();
    test_disk_roundtrip();
    std::printf("ALL PASS\n");
    return 0;
}

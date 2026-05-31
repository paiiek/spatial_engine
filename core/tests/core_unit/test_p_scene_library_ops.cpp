// core/tests/core_unit/test_p_scene_library_ops.cpp
// v0.9 Lane E (E-M1): SceneController library-management ops
// (rename / duplicate / delete / meta) + D3 index-corruption rescan recovery.
#include "ipc/SceneController.h"
#include "ipc/SceneSnapshot.h"
#include "ipc/Command.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace spe::ipc;

// ---- helpers ----------------------------------------------------------------

static void CHECK(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

namespace fs = std::filesystem;

static fs::path freshDir(const char* leaf) {
    auto d = fs::temp_directory_path() / leaf;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// Save a scene by name through the controller's SceneSave path.
static void saveScene(SceneController& ctrl, const char* name) {
    Command c;
    c.tag = CommandTag::SceneSave;
    PayloadSceneSave p{};
    std::strncpy(p.name, name, sizeof(p.name) - 1);
    c.payload = p;
    CHECK(ctrl.handleCommand(c), "saveScene: handled");
}

static bool indexHas(const SceneController& ctrl, const std::string& name) {
    for (const auto& e : ctrl.index().scenes)
        if (e.name == name) return true;
    return false;
}

static const SceneIndexEntry* indexEntry(const SceneController& ctrl, const std::string& name) {
    for (const auto& e : ctrl.index().scenes)
        if (e.name == name) return &e;
    return nullptr;
}

static bool listHas(SceneController& ctrl, const std::string& name) {
    Command c;
    c.tag = CommandTag::SceneList;
    c.payload = PayloadSceneList{};
    ctrl.handleCommand(c);
    for (const auto& s : ctrl.lastSceneList())
        if (s == name) return true;
    return false;
}

// ---- Test 1: save → rename → list (old gone, new present) -------------------

static void test_rename() {
    auto dir = freshDir("spe_scene_libops_rename");
    SceneController ctrl(dir.string());
    saveScene(ctrl, "alpha");

    CHECK(ctrl.rename("alpha", "beta"), "rename: succeeds");
    CHECK(!fs::exists(dir / "alpha.json"), "rename: old file gone");
    CHECK(fs::exists(dir / "beta.json"),   "rename: new file present");
    CHECK(!listHas(ctrl, "alpha"), "rename: alpha not in list");
    CHECK(listHas(ctrl, "beta"),   "rename: beta in list");
    CHECK(!indexHas(ctrl, "alpha"), "rename: alpha not in index");
    CHECK(indexHas(ctrl, "beta"),   "rename: beta in index");

    // Reject: dst exists.
    saveScene(ctrl, "gamma");
    CHECK(!ctrl.rename("beta", "gamma"), "rename: reject dst exists");
    // Reject: src missing.
    CHECK(!ctrl.rename("nope", "delta"), "rename: reject src missing");

    fs::remove_all(dir);
    std::printf("PASS: test_rename\n");
}

// ---- Test 2: duplicate (both present, distinct created_unix) -----------------

static void test_duplicate() {
    auto dir = freshDir("spe_scene_libops_dup");
    SceneController ctrl(dir.string());
    saveScene(ctrl, "src");

    // Give src some meta to verify it is cloned.
    CHECK(ctrl.setMeta("src", "{\"tags\":[\"a\",\"b\"],\"note\":\"hello\"}"),
          "duplicate: setMeta on src");

    // Force a distinct created_unix by stamping the source entry into the past.
    // (created_unix is wall-clock seconds; duplicate() uses now().)
    // We cannot mutate the private entry directly, so rely on the duplicate
    // path setting a fresh created_unix and assert it is >= the source.
    const SceneIndexEntry* before = indexEntry(ctrl, "src");
    CHECK(before != nullptr, "duplicate: src entry present");
    const int64_t srcCreated = before->created_unix;

    CHECK(ctrl.duplicate("src", "copy"), "duplicate: succeeds");
    CHECK(fs::exists(dir / "src.json"),  "duplicate: src file present");
    CHECK(fs::exists(dir / "copy.json"), "duplicate: copy file present");
    CHECK(indexHas(ctrl, "src"),  "duplicate: src in index");
    CHECK(indexHas(ctrl, "copy"), "duplicate: copy in index");

    const SceneIndexEntry* copyE = indexEntry(ctrl, "copy");
    CHECK(copyE != nullptr, "duplicate: copy entry present");
    // Cloned meta.
    CHECK(copyE->tags.size() == 2 && copyE->tags[0] == "a" && copyE->tags[1] == "b",
          "duplicate: tags cloned");
    CHECK(copyE->note == "hello", "duplicate: note cloned");
    // Fresh creation time (>= source's; both wall-clock seconds).
    CHECK(copyE->created_unix >= srcCreated, "duplicate: created_unix fresh");

    // Reject: dst exists.
    CHECK(!ctrl.duplicate("src", "copy"), "duplicate: reject dst exists");
    // Reject: src missing.
    CHECK(!ctrl.duplicate("ghost", "x"), "duplicate: reject src missing");

    fs::remove_all(dir);
    std::printf("PASS: test_duplicate\n");
}

// ---- Test 3: delete (gone from disk AND index) ------------------------------

static void test_delete() {
    auto dir = freshDir("spe_scene_libops_del");
    SceneController ctrl(dir.string());
    saveScene(ctrl, "doomed");
    CHECK(fs::exists(dir / "doomed.json"), "delete: precondition file present");
    CHECK(indexHas(ctrl, "doomed"), "delete: precondition in index");

    CHECK(ctrl.remove("doomed"), "delete: succeeds");
    CHECK(!fs::exists(dir / "doomed.json"), "delete: file gone");
    CHECK(!indexHas(ctrl, "doomed"), "delete: gone from index");
    CHECK(!listHas(ctrl, "doomed"),  "delete: gone from list");

    fs::remove_all(dir);
    std::printf("PASS: test_delete\n");
}

// ---- Test 4: setMeta round-trip ---------------------------------------------

static void test_set_meta() {
    auto dir = freshDir("spe_scene_libops_meta");
    SceneController ctrl(dir.string());
    saveScene(ctrl, "scene1");

    CHECK(ctrl.setMeta("scene1", "{\"tags\":[\"drums\",\"loud\"],\"note\":\"my note\"}"),
          "setMeta: succeeds");
    const SceneIndexEntry* e = indexEntry(ctrl, "scene1");
    CHECK(e != nullptr, "setMeta: entry present");
    CHECK(e->tags.size() == 2, "setMeta: tag count");
    CHECK(e->tags[0] == "drums" && e->tags[1] == "loud", "setMeta: tag values");
    CHECK(e->note == "my note", "setMeta: note value");

    // Persisted: a fresh controller over the same dir reads the meta back.
    {
        SceneController ctrl2(dir.string());
        const SceneIndexEntry* e2 = indexEntry(ctrl2, "scene1");
        CHECK(e2 != nullptr, "setMeta: persisted entry present");
        CHECK(e2->tags.size() == 2 && e2->tags[0] == "drums" && e2->tags[1] == "loud",
              "setMeta: persisted tags");
        CHECK(e2->note == "my note", "setMeta: persisted note");
    }

    fs::remove_all(dir);
    std::printf("PASS: test_set_meta\n");
}

// ---- Test 5: D3 — corrupt index.json → rebuild recovers from disk files -----

static void test_d3_rebuild_from_corrupt_index() {
    auto dir = freshDir("spe_scene_libops_d3");
    {
        // Create three scene files via a controller, then corrupt the index.
        SceneController ctrl(dir.string());
        saveScene(ctrl, "s1");
        saveScene(ctrl, "s2");
        saveScene(ctrl, "s3");
        CHECK(indexHas(ctrl, "s1") && indexHas(ctrl, "s2") && indexHas(ctrl, "s3"),
              "d3: precondition all indexed");
    }

    // Truncate / garble index.json.
    {
        std::ofstream ofs(dir / "index.json", std::ios::trunc);
        ofs << "{\"scenes\":[ {\"name\":\"s1\""; // truncated garbage
    }

    // Construct a new controller → parse failure → rebuildIndex from disk files.
    SceneController recovered(dir.string());
    CHECK(indexHas(recovered, "s1"), "d3: s1 recovered");
    CHECK(indexHas(recovered, "s2"), "d3: s2 recovered");
    CHECK(indexHas(recovered, "s3"), "d3: s3 recovered");
    CHECK(recovered.index().scenes.size() == 3, "d3: exactly 3 recovered");

    // rebuildIndex also drops index entries with no backing file:
    // delete a file out-of-band, then rebuild.
    fs::remove(dir / "s2.json");
    recovered.rebuildIndex();
    CHECK(indexHas(recovered, "s1"), "d3: s1 after rebuild");
    CHECK(!indexHas(recovered, "s2"), "d3: s2 dropped (no file)");
    CHECK(indexHas(recovered, "s3"), "d3: s3 after rebuild");

    fs::remove_all(dir);
    std::printf("PASS: test_d3_rebuild_from_corrupt_index\n");
}

// ---- Test 6: isSafeSceneName rejects traversal on every op ------------------

static void test_traversal_rejected() {
    auto dir = freshDir("spe_scene_libops_traversal");
    SceneController ctrl(dir.string());
    saveScene(ctrl, "ok");

    const char* bad[] = {"../evil", "a/b", "a\\b", "with.dot", ""};
    for (const char* b : bad) {
        CHECK(!ctrl.rename("ok", b),       "traversal: rename dst rejected");
        CHECK(!ctrl.rename(b, "ok2"),      "traversal: rename src rejected");
        CHECK(!ctrl.duplicate("ok", b),    "traversal: duplicate dst rejected");
        CHECK(!ctrl.duplicate(b, "ok2"),   "traversal: duplicate src rejected");
        CHECK(!ctrl.remove(b),             "traversal: delete rejected");
        CHECK(!ctrl.setMeta(b, "{}"),      "traversal: setMeta rejected");
    }
    // A NUL embedded in a std::string also fails the guard (size>0, contains \0).
    {
        std::string nul("a");
        nul.push_back('\0');
        nul += "b";
        CHECK(!ctrl.remove(nul), "traversal: NUL rejected");
    }

    // The valid scene is untouched.
    CHECK(fs::exists(dir / "ok.json"), "traversal: valid scene intact");

    fs::remove_all(dir);
    std::printf("PASS: test_traversal_rejected\n");
}

// ---- main ------------------------------------------------------------------

int main() {
    test_rename();
    test_duplicate();
    test_delete();
    test_set_meta();
    test_d3_rebuild_from_corrupt_index();
    test_traversal_rejected();
    std::printf("ALL PASS\n");
    return 0;
}

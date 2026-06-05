// core/src/ipc/SceneController.h
// SceneController: message-thread handler for SceneSave/SceneLoad/SceneList and
// the v0.9 Lane E (E-M1) library-management ops (rename/duplicate/delete/meta).
// Must NOT be called from the RT audio thread.
#pragma once
#include "ipc/Command.h"
#include "ipc/SceneSnapshot.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace spe::ipc {

// One scene-library index entry. Meta lives ONLY here (the per-scene .json
// snapshot stays the pure untouched snapshot), so existing scene wire-byte
// hashes are preserved.
struct SceneIndexEntry {
    std::string              name;
    int64_t                  created_unix = 0;
    std::vector<std::string> tags;
    std::string              note;
};

// The persisted scene library index — a rebuildable cache over the scene files.
// Scene files on disk are ground truth; this index resolves to them on any
// divergence via SceneController::rebuildIndex (the D3 rescan fallback).
struct SceneIndex {
    std::vector<SceneIndexEntry> scenes;
};

class SceneController {
public:
    explicit SceneController(const std::string& scenesDir);

    // Returns true if the command was handled (SceneSave/SceneLoad/SceneList,
    // or the E-M1 SceneRename/SceneDuplicate/SceneDelete/SceneMeta ops).
    // Safe to call from message/control thread only.
    bool handleCommand(const Command& cmd);

    const std::string& scenesDir() const { return scenesDir_; }

    // Last scene-list result (populated after SceneList command).
    const std::vector<std::string>& lastSceneList() const { return lastSceneList_; }
    // Last loaded snapshot (populated after successful SceneLoad).
    const std::optional<SceneSnapshot>& lastLoaded() const { return lastLoaded_; }

    // ---- v0.9 Lane E (E-M1) library-management ops --------------------------
    // All validate names via isSafeSceneName and persist the index on success.

    // Rename from→to. Rejects if either name is unsafe, src missing, or dst
    // already exists. Renames the .json file and the index entry.
    bool rename(const std::string& from, const std::string& to);

    // Duplicate from→to (copies the .json file; clones the index entry with a
    // fresh created_unix). Rejects if names unsafe, src missing, or dst exists.
    bool duplicate(const std::string& from, const std::string& to);

    // Delete the named scene (.json file + index entry). Rejects unsafe names.
    bool remove(const std::string& name);

    // Set meta (tags/note parsed from metaJson) on the named index entry.
    // Creates the entry if missing-but-backed-by-a-file. Rejects unsafe names.
    bool setMeta(const std::string& name, const std::string& metaJson);

    // D3 rescan fallback: reconcile the index with the scene files on disk —
    // add default entries for files missing from the index (created_unix = file
    // mtime), and drop index entries with no backing file. Persists the result.
    void rebuildIndex();

    // Read access to the in-memory index.
    const SceneIndex& index() const { return index_; }

    // F4b — engine-agnostic seam: lets the daemon inject a callback that
    // captures the live authoritative per-object state into a scene's objects
    // on SceneSave. Keeps SceneController free of any engine dependency.
    using ObjectStateProvider = std::function<void(std::vector<ObjectSnapshot>&)>;
    void setObjectStateProvider(ObjectStateProvider p) { objStateProvider_ = std::move(p); }

    // ⑥h — symmetric seam for the room engine block: the daemon injects a
    // callback capturing the live room state into a scene on SceneSave. Optional
    // (unset → scenes carry no room block, fully backward compatible).
    using RoomStateProvider = std::function<void(RoomSnapshot&)>;
    void setRoomStateProvider(RoomStateProvider p) { roomStateProvider_ = std::move(p); }

private:
    // Find an index entry by name (nullptr if absent).
    SceneIndexEntry*       findEntry(const std::string& name);
    const SceneIndexEntry* findEntry(const std::string& name) const;

    // Serialise / parse index.json (hand-rolled JSON, SceneSnapshot.cpp style).
    bool        loadIndexFromDisk();   // true on clean parse, false otherwise
    bool        persistIndex() const;
    std::string indexPath() const;

    std::string scenesDir_;
    SceneIndex  index_;
    std::vector<std::string> lastSceneList_;
    std::optional<SceneSnapshot> lastLoaded_;
    ObjectStateProvider objStateProvider_; // F4b — set by the daemon (optional)
    RoomStateProvider   roomStateProvider_; // ⑥h — set by the daemon (optional)
};

} // namespace spe::ipc

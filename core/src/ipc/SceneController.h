// core/src/ipc/SceneController.h
// SceneController: message-thread handler for SceneSave/SceneLoad/SceneList commands.
// Must NOT be called from the RT audio thread.
#pragma once
#include "ipc/Command.h"
#include "ipc/SceneSnapshot.h"
#include <string>
#include <vector>

namespace spe::ipc {

class SceneController {
public:
    explicit SceneController(const std::string& scenesDir);

    // Returns true if the command was handled (SceneSave/SceneLoad/SceneList).
    // Safe to call from message/control thread only.
    bool handleCommand(const Command& cmd);

    const std::string& scenesDir() const { return scenesDir_; }

    // Last scene-list result (populated after SceneList command).
    const std::vector<std::string>& lastSceneList() const { return lastSceneList_; }
    // Last loaded snapshot (populated after successful SceneLoad).
    const std::optional<SceneSnapshot>& lastLoaded() const { return lastLoaded_; }

private:
    std::string scenesDir_;
    std::vector<std::string> lastSceneList_;
    std::optional<SceneSnapshot> lastLoaded_;
};

} // namespace spe::ipc

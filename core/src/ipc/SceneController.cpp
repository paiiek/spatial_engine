// core/src/ipc/SceneController.cpp
// Message-thread handler for SceneSave / SceneLoad / SceneList commands.
// RT audio thread must never call this code.

#include "ipc/SceneController.h"

namespace spe::ipc {

SceneController::SceneController(const std::string& scenesDir)
    : scenesDir_(scenesDir) {}

bool SceneController::handleCommand(const Command& cmd) {
    switch (cmd.tag) {
        case CommandTag::SceneSave: {
            const auto& p = std::get<PayloadSceneSave>(cmd.payload);
            SceneSnapshot snap;
            snap.name = p.name; // null-terminated fixed-size buffer
            snap.saveToDisk(scenesDir_);
            return true;
        }
        case CommandTag::SceneLoad: {
            const auto& p = std::get<PayloadSceneLoad>(cmd.payload);
            lastLoaded_ = SceneSnapshot::loadFromDisk(scenesDir_, p.name);
            return true;
        }
        case CommandTag::SceneList:
            lastSceneList_ = SceneSnapshot::listScenes(scenesDir_);
            return true;
        default:
            return false;
    }
}

} // namespace spe::ipc

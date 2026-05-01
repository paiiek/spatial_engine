// core/src/ipc/SceneController.cpp
// Message-thread handler for SceneSave / SceneLoad / SceneList commands.
// RT audio thread must never call this code.

#include "ipc/SceneController.h"
#include <cstring>

namespace spe::ipc {

SceneController::SceneController(const std::string& scenesDir)
    : scenesDir_(scenesDir) {}

bool SceneController::handleCommand(const Command& cmd) {
    switch (cmd.tag) {
        case CommandTag::SceneSave: {
            auto& p = std::get<PayloadSceneSave>(cmd.payload);
            SceneSnapshot snap;
            snap.name = std::string(p.name, std::strlen(p.name));
            snap.saveToDisk(scenesDir_);
            return true;
        }
        case CommandTag::SceneLoad: {
            auto& p = std::get<PayloadSceneLoad>(cmd.payload);
            std::string name(p.name, std::strlen(p.name));
            lastLoaded_ = SceneSnapshot::loadFromDisk(scenesDir_, name);
            return true;
        }
        case CommandTag::SceneList: {
            lastSceneList_ = SceneSnapshot::listScenes(scenesDir_);
            return true;
        }
        default:
            return false;
    }
}

} // namespace spe::ipc

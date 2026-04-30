// core/src/ipc/ExternalControl.h
// Abstract base: anything that can dispatch Commands to the engine.

#pragma once
#include "Command.h"

namespace spe::ipc {

class ExternalControl {
public:
    virtual ~ExternalControl() = default;

    // Dispatch a command to the engine. Called from control thread.
    virtual void dispatch(Command const& cmd) = 0;

    // Optional: start/stop transport (e.g. open socket).
    virtual void start() {}
    virtual void stop()  {}
};

} // namespace spe::ipc

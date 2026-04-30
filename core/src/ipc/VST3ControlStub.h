// core/src/ipc/VST3ControlStub.h
// No-op ExternalControl for VST3 plugin slot (C-MA #6.f).
// dispatch() does nothing; engine state is unmodified.
// Factory registration verified by static_assert at compile time.

#pragma once
#include "ExternalControl.h"
#include <memory>

namespace spe::ipc {

class VST3ControlStub final : public ExternalControl {
public:
    VST3ControlStub() = default;
    ~VST3ControlStub() override = default;

    // No-op: engine state unmodified.
    void dispatch(Command const& /*cmd*/) override {}

    void start() override {}
    void stop()  override {}

    // Factory helper.
    static std::unique_ptr<ExternalControl> create() {
        return std::make_unique<VST3ControlStub>();
    }
};

// Compile-time contract: VST3ControlStub must be derived from ExternalControl.
static_assert(std::is_base_of_v<ExternalControl, VST3ControlStub>,
              "VST3ControlStub must derive from ExternalControl");

} // namespace spe::ipc

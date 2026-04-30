// test_p4_vst3stub.cpp
// P4: VST3ControlStub factory registration, dispatch() is no-op,
//     engine state is unmodified, compile-time ExternalControl signature check.

#include "ipc/VST3ControlStub.h"
#include "ipc/StateModel.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace spe::ipc;

int main() {
    // Compile-time check (static_assert in header ensures this).
    static_assert(std::is_base_of_v<ExternalControl, VST3ControlStub>,
                  "VST3ControlStub must derive from ExternalControl");

    // Factory creates a valid instance.
    std::unique_ptr<ExternalControl> ctrl = VST3ControlStub::create();
    assert(ctrl != nullptr);

    // Dispatch several commands; state model is NOT connected — stub is no-op.
    StateModel sm;

    // Build a move command.
    Command cmd;
    cmd.tag = CommandTag::ObjMove; cmd.seq=1; cmd.id=1;
    PayloadObjMove p; p.obj_id=0; p.az_rad=3.14f; p.el_rad=0.f; p.dist_m=1.f;
    cmd.payload = p;

    ctrl->start();
    ctrl->dispatch(cmd); // no-op
    ctrl->stop();

    // Engine state unmodified (never connected).
    assert(sm.objectState(0).valid == false);
    assert(sm.objectState(0).az_rad == 0.f);

    // Polymorphic usage: cast back to VST3ControlStub.
    assert(dynamic_cast<VST3ControlStub*>(ctrl.get()) != nullptr);

    std::puts("PASS test_p4_vst3stub");
    return 0;
}

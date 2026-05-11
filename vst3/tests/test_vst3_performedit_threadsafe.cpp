// vst3/tests/test_vst3_performedit_threadsafe.cpp
// S2.6 (C4): 1000-iter performEdit thread-safety harness.
//
// Strategy (a): UDP I/O thread pushes (paramId, value) pairs via
// SpatialEngineController::pushParamEdit(); message thread drains via
// drainParamEdits() and calls IComponentHandler::performEdit.
//
// Test structure:
//   - FakeComponentHandler records every (beginEdit, performEdit, endEdit) triplet.
//   - Producer thread: pushes 1000 (paramId=0, value=i/999.0) entries at ~1kHz.
//   - Consumer (main) thread: polls drainParamEdits() at ~100Hz until all 1000
//     entries are received or a 10s timeout expires.
//   - Assertions:
//       1. All 1000 calls reach the handler (no drops in the ring).
//       2. No crashes / data corruption (checked via call count == 1000).
//       3. Value ordering preserved: each received value is >= the previous
//          (monotonic push implies monotonic receive for a SPSC ring).
//
// IMPORTANT: pushParamEdit() and drainParamEdits() are the only two public
// APIs tested here. The UDP→controller wiring is S4's responsibility.

#include "SpatialEngineController.hpp"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// FakeComponentHandler
// ---------------------------------------------------------------------------
class FakeComponentHandler : public Steinberg::Vst::IComponentHandler {
public:
    struct Record {
        Steinberg::Vst::ParamID    id;
        Steinberg::Vst::ParamValue value;
    };

    // FUnknown (minimal stub — no reference counting needed in tests)
    Steinberg::tresult PLUGIN_API
    queryInterface(const Steinberg::TUID /*_iid*/, void** obj) override
    {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef()  override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override
    {
        last_begin_id_ = id;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API
    performEdit(Steinberg::Vst::ParamID id,
                Steinberg::Vst::ParamValue value) override
    {
        records_.push_back({id, value});
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override
    {
        last_end_id_ = id;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override
    {
        return Steinberg::kResultOk;
    }

    static const Steinberg::FUID iid;

    const std::vector<Record>& records() const { return records_; }

private:
    std::vector<Record> records_;
    Steinberg::Vst::ParamID last_begin_id_{0};
    Steinberg::Vst::ParamID last_end_id_{0};
};

// Satisfy DECLARE_CLASS_IID linkage requirement for IComponentHandler
const Steinberg::FUID FakeComponentHandler::iid(
    Steinberg::FUID::fromTUID(Steinberg::Vst::IComponentHandler::iid));

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static int failures = 0;

#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",          \
                         msg, __FILE__, __LINE__);               \
            ++failures;                                          \
        }                                                        \
    } while (0)

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    using namespace spe::vst3;
    using namespace std::chrono_literals;

    constexpr int kIters      = 1000;
    constexpr int kParamId    = 0; // pan_az
    constexpr auto kPushDelay = std::chrono::microseconds(1000); // ~1 kHz
    constexpr auto kDrainDelay= std::chrono::milliseconds(10);   // ~100 Hz
    constexpr auto kTimeout   = std::chrono::seconds(10);

    // 1. Set up controller + fake handler
    SpatialEngineController ctl;
    ctl.initialize(nullptr);

    FakeComponentHandler handler;
    ctl.setComponentHandler(&handler);

    // 2. Producer thread: push 1000 entries, values 0/999 .. 999/999
    std::atomic<bool> producer_done{false};
    std::atomic<int>  push_count{0};

    std::thread producer([&]() {
        for (int i = 0; i < kIters; ++i) {
            double v = static_cast<double>(i) / static_cast<double>(kIters - 1);
            // Retry push until ring has space (should never block long in practice)
            while (!ctl.pushParamEdit(
                       static_cast<Steinberg::Vst::ParamID>(kParamId), v)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            push_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(kPushDelay);
        }
        producer_done.store(true, std::memory_order_release);
    });

    // 3. Consumer (main thread): drain at ~100 Hz until all 1000 received or timeout
    auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (true) {
        ctl.drainParamEdits();
        int received = static_cast<int>(handler.records().size());
        if (received >= kIters) break;
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr,
                "TIMEOUT: only %d/%d records received after 10s\n",
                received, kIters);
            break;
        }
        std::this_thread::sleep_for(kDrainDelay);
    }

    producer.join();

    // Final drain to catch any last items pushed between last sleep and join
    ctl.drainParamEdits();

    const auto& recs = handler.records();

    // Assertion 1: all 1000 calls delivered
    CHECK(static_cast<int>(recs.size()) == kIters,
          "all 1000 performEdit calls must be delivered");

    // Assertion 2: no data corruption — all paramIds == kParamId
    bool id_ok = true;
    for (const auto& r : recs) {
        if (static_cast<int>(r.id) != kParamId) { id_ok = false; break; }
    }
    CHECK(id_ok, "all records must have paramId == kParamId (no corruption)");

    // Assertion 3: value ordering preserved (SPSC ring is FIFO)
    bool order_ok = true;
    for (int i = 1; i < static_cast<int>(recs.size()); ++i) {
        if (recs[i].value < recs[i - 1].value - 1e-9) {
            order_ok = false;
            std::fprintf(stderr,
                "ORDER VIOLATION at index %d: %.6f < %.6f\n",
                i, recs[i].value, recs[i - 1].value);
            break;
        }
    }
    CHECK(order_ok, "value ordering must be monotonically non-decreasing");

    if (failures == 0) {
        std::printf("PASS: test_vst3_performedit_threadsafe (1000 iters, strategy a)\n");
        return 0;
    } else {
        std::printf("FAIL: %d assertion(s) failed\n", failures);
        return 1;
    }
}

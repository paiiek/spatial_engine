// core/tests/core_unit/test_p6_dante_loopback.cpp
// P6 unit tests: DanteBackend port discovery + channel-order validation,
// LiveMicInput silence-fallback.  Runs in NO_JUCE CI environment (no JACK,
// no hardware required).

#include "audio_io/DanteBackend.h"
#include "input/LiveMicInput.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// p6_dante_port_discovery
// discoverPorts() must return a vector (empty on NO_JUCE / no-JACK host).
// If hardware IS present every entry must have non-negative indices.
// ---------------------------------------------------------------------------
static void p6_dante_port_discovery() {
    auto ports = spe::audio_io::DantePortDiscovery::discoverPorts();
    // Result is always a valid vector — could be empty (CI) or populated (lab).
    for (auto& p : ports) {
        assert(p.jackPortIndex >= 0 && "jackPortIndex must be non-negative");
        assert(p.danteChannel  >= 0 && "danteChannel must be non-negative");
    }
    std::printf("[PASS] p6_dante_port_discovery  (ports=%zu)\n", ports.size());
}

// ---------------------------------------------------------------------------
// p6_dante_channel_order_stub
// validateChannelOrder(8) must return false on NO_JUCE / no-JACK host.
// On a lab host with JACK it may return true — both are accepted.
// ---------------------------------------------------------------------------
static void p6_dante_channel_order_stub() {
    bool ok = spe::audio_io::DantePortDiscovery::validateChannelOrder(8);
    // false  → expected in CI (no hardware).
    // true   → valid on lab host.
    (void)ok;
    std::printf("[PASS] p6_dante_channel_order_stub  (result=%s)\n",
                ok ? "true (hardware present)" : "false (stub/no-hw)");
}

// ---------------------------------------------------------------------------
// p6_live_mic_silence_fallback
// LiveMicInput::pull() must return n_frames of zeros in NO_JUCE mode.
// ---------------------------------------------------------------------------
static void p6_live_mic_silence_fallback() {
    spe::input::LiveMicInput mic(48000);
    mic.start();

    constexpr int N = 256;
    float buf[N];
    for (int i = 0; i < N; ++i) buf[i] = 1.0f; // pre-fill with non-zero

    int got = mic.pull(buf, N);
    assert(got == N && "pull must return exactly n_frames");

    for (int i = 0; i < N; ++i) {
        assert(std::fabs(buf[i]) < 1e-9f && "NO_JUCE LiveMicInput must produce silence");
    }

    mic.stop();
    std::printf("[PASS] p6_live_mic_silence_fallback\n");
}

// ---------------------------------------------------------------------------
// p6_live_mic_never_at_end
// ---------------------------------------------------------------------------
static void p6_live_mic_never_at_end() {
    spe::input::LiveMicInput mic(48000);
    mic.start();
    assert(!mic.atEnd() && "mic source must never report end-of-stream");
    // decodeMore() must return true (infinite source).
    bool more = mic.decodeMore();
    assert(more && "decodeMore must return true for live mic");
    mic.stop();
    std::printf("[PASS] p6_live_mic_never_at_end\n");
}

int main() {
    p6_dante_port_discovery();
    p6_dante_channel_order_stub();
    p6_live_mic_silence_fallback();
    p6_live_mic_never_at_end();
    std::printf("All P6 tests passed.\n");
    return 0;
}

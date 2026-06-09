// test_a3_input_routing.cpp
// v0.9 Lane 6 (A3) — per-object input→object audio routing (channel remap +
// per-route gain + many-to-one fan-out), driven over the production command
// path (dispatchCommand → cmd_fifo_ → audioBlock drain → obj_cache_) and the
// real render loop.
//
// State proof:  objInputRouteAt(obj) == {src_ch, gain}  (decode→drain→cache).
// Audio proof:  RELATIVE assertions on the test-owned block.output_channels
//               (the identical per-object chain cancels), since dry_scratch_ is
//               overwritten in place by the per-object chain (no "dry" tap).
//
// Cases:
//   AC1a remap+gain (state)         AC1b remap+gain (output ratio ≈ 2.0)
//   AC2  fan-out (output 2× single) AC3  out-of-range → sine (non-constant)
//   AC4  backward-compat default    AC5  reset sentinel + /sys/reset
//   DEC  decoder unit (,iiiif / arity / src<-1 → Unknown)

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/LayoutLoader.h"
#include "ipc/Command.h"
#include "ipc/CommandDecoder.h"
#include "audio_io/AudioCallback.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <variant>
#include <vector>

using namespace spe;

static constexpr int FRAMES = 64;

// ── command dispatch helpers (production path) ───────────────────────────────
static void dMove(core::SpatialEngine& e, uint32_t id, float az, float el, float dist) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjMove;
    ipc::PayloadObjMove p; p.obj_id = id; p.az_rad = az; p.el_rad = el; p.dist_m = dist;
    c.payload = p; e.dispatchCommand(c);
}
static void dInput(core::SpatialEngine& e, uint32_t id, int32_t src, float gain) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjInput;
    ipc::PayloadObjInput p; p.obj_id = id; p.src_ch = src; p.gain = gain;
    c.payload = p; e.dispatchCommand(c);
}
static void dReset(core::SpatialEngine& e) {
    ipc::Command c; c.tag = ipc::CommandTag::SysReset;
    c.payload = ipc::PayloadSysReset{}; e.dispatchCommand(c);
}

// ── input-injecting audio rig ────────────────────────────────────────────────
// Feeds n_in constant input channels (inConsts[ch]) and captures the LAST
// block's planar speaker output. Re-zeroes the output each block (the engine
// assigns the speaker bus, but noise/other paths accumulate — keep it clean).
static std::vector<float> pumpInput(core::SpatialEngine& e, int n_spk,
                                    const std::vector<float>& inConsts, int blocks) {
    const int n_in = static_cast<int>(inConsts.size());
    std::vector<std::vector<float>> inb(static_cast<size_t>(n_in),
                                        std::vector<float>(FRAMES, 0.f));
    std::vector<const float*> inp(static_cast<size_t>(n_in));
    for (int c = 0; c < n_in; ++c) {
        std::fill(inb[static_cast<size_t>(c)].begin(),
                  inb[static_cast<size_t>(c)].end(), inConsts[static_cast<size_t>(c)]);
        inp[static_cast<size_t>(c)] = inb[static_cast<size_t>(c)].data();
    }
    std::vector<std::vector<float>> outb(static_cast<size_t>(n_spk),
                                         std::vector<float>(FRAMES, 0.f));
    std::vector<float*> outp(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s) outp[static_cast<size_t>(s)] = outb[static_cast<size_t>(s)].data();

    audio_io::AudioBlock b;
    b.output_channels      = outp.data();
    b.output_channel_count = n_spk;
    b.input_channels       = inp.data();
    b.input_channel_count  = n_in;
    b.num_frames           = FRAMES;
    b.sample_rate          = 48000.0;
    for (int k = 0; k < blocks; ++k) {
        for (int s = 0; s < n_spk; ++s)
            std::fill(outb[static_cast<size_t>(s)].begin(),
                      outb[static_cast<size_t>(s)].end(), 0.f);
        e.audioBlock(b);
    }
    std::vector<float> last(static_cast<size_t>(n_spk * FRAMES));
    for (int s = 0; s < n_spk; ++s)
        for (int n = 0; n < FRAMES; ++n)
            last[static_cast<size_t>(s * FRAMES + n)] = outb[static_cast<size_t>(s)][static_cast<size_t>(n)];
    return last;
}

static double energy(const std::vector<float>& v) {
    double e = 0.0;
    for (float x : v) e += std::fabs(static_cast<double>(x));
    return e;
}

// Largest |max-min| across any single speaker channel in a captured block.
static double maxChannelSwing(const std::vector<float>& v, int n_spk) {
    double best = 0.0;
    for (int s = 0; s < n_spk; ++s) {
        float lo = v[static_cast<size_t>(s * FRAMES)];
        float hi = lo;
        for (int n = 1; n < FRAMES; ++n) {
            const float x = v[static_cast<size_t>(s * FRAMES + n)];
            lo = std::min(lo, x); hi = std::max(hi, x);
        }
        best = std::max(best, static_cast<double>(hi - lo));
    }
    return best;
}

static geometry::SpeakerLayout loadLayout(const std::string& configs_dir) {
    auto result = spe::geometry::load_layout(configs_dir + "/lab_8ch.yaml");
    assert(spe::geometry::is_ok(result) && "lab_8ch.yaml layout must load");
    return std::get<spe::geometry::SpeakerLayout>(result);
}

// SpatialEngine is non-copyable/non-movable (atomics + threads), so configure
// it in place rather than returning by value.
static void configEngine(core::SpatialEngine& e, const geometry::SpeakerLayout& layout) {
    e.setLayout(layout);
    e.prepareToPlay(48000.0, FRAMES);
    e.setObjectSourceInput(true);
}

// Identical per-object chain across compared objects (same position/DSP) so the
// audio assertions isolate the routing change.
static constexpr float kAz = 0.30f, kEl = 0.0f, kDist = 1.0f;
static constexpr int   kSpk = 8;

// ── AC1a — remap+gain STATE proof ────────────────────────────────────────────
static void test_ac1a_state(const geometry::SpeakerLayout& layout) {
    core::SpatialEngine e(0); configEngine(e, layout);
    dMove(e, 0, kAz, kEl, kDist);
    dInput(e, 0, /*src*/ 1, /*gain*/ 2.0f);
    pumpInput(e, kSpk, {0.1f, 0.4f}, 4);
    const auto r = e.objInputRouteAt(0);
    assert(r.src_ch == 1 && std::fabs(r.gain - 2.0f) < 1e-6f);
    std::puts("PASS test_ac1a_state");
}

// ── AC1b — remap+gain RELATIVE audio (output ratio ≈ 2.0) ────────────────────
static void test_ac1b_ratio(const geometry::SpeakerLayout& layout) {
    const std::vector<float> in = {0.1f, 0.4f};   // both objects read src=1 (0.4)
    core::SpatialEngine eA(0); configEngine(eA, layout);
    dMove(eA, 0, kAz, kEl, kDist); dInput(eA, 0, 1, 1.0f);
    const auto outA = pumpInput(eA, kSpk, in, 24);

    core::SpatialEngine eB(0); configEngine(eB, layout);
    dMove(eB, 0, kAz, kEl, kDist); dInput(eB, 0, 1, 2.0f);
    const auto outB = pumpInput(eB, kSpk, in, 24);

    const double eA_ = energy(outA), eB_ = energy(outB);
    assert(eA_ > 1e-3 && "gain-1.0 route must be audible");
    const double ratio = eB_ / eA_;
    std::printf("  AC1b ratio = %.4f (expect ~2.0)\n", ratio);
    assert(std::fabs(ratio - 2.0) < 1e-3);
    std::puts("PASS test_ac1b_ratio");
}

// ── AC2 — fan-out: two objects share one src → output is exactly 2× single ────
static void test_ac2_fanout(const geometry::SpeakerLayout& layout) {
    const std::vector<float> in = {0.1f, 0.4f};
    core::SpatialEngine eS(0); configEngine(eS, layout);
    dMove(eS, 0, kAz, kEl, kDist); dInput(eS, 0, 1, 1.0f);
    const auto outS = pumpInput(eS, kSpk, in, 24);

    core::SpatialEngine eF(0); configEngine(eF, layout);
    dMove(eF, 0, kAz, kEl, kDist); dInput(eF, 0, 1, 1.0f);
    dMove(eF, 1, kAz, kEl, kDist); dInput(eF, 1, 1, 1.0f);   // fan-out: src=1 again
    const auto outF = pumpInput(eF, kSpk, in, 24);

    assert(energy(outS) > 1e-3);
    // Two identical objects reading the same input pointer ⇒ fan-out = 2× single,
    // sample-for-sample (independent lock-free reads, no overwrite/aliasing).
    for (size_t k = 0; k < outS.size(); ++k)
        assert(std::fabs(outF[k] - 2.0f * outS[k]) < 1e-4f);
    std::puts("PASS test_ac2_fanout");
}

// ── AC3 — out-of-range src → sine fallback (non-constant), gain ignored ───────
static void test_ac3_out_of_range_sine(const geometry::SpeakerLayout& layout) {
    const std::vector<float> in = {0.1f, 0.2f, 0.3f};   // 3-channel input
    core::SpatialEngine eSine(0); configEngine(eSine, layout);
    dMove(eSine, 0, kAz, kEl, kDist);
    dInput(eSine, 0, /*src*/ 99, /*gain*/ 2.0f);          // 99 >= 3 → sine fallback
    const auto outSine = pumpInput(eSine, kSpk, in, 24);

    core::SpatialEngine eConst(0); configEngine(eConst, layout);
    dMove(eConst, 0, kAz, kEl, kDist);
    dInput(eConst, 0, /*src*/ 0, /*gain*/ 1.0f);          // in-range constant feed
    const auto outConst = pumpInput(eConst, kSpk, in, 24);

    const double swingSine  = maxChannelSwing(outSine, kSpk);
    const double swingConst = maxChannelSwing(outConst, kSpk);
    std::printf("  AC3 swing sine=%.5f const=%.6f\n", swingSine, swingConst);
    // Sine path varies sample-to-sample; the steady-state constant feed does not.
    assert(swingSine > 1e-3 && "out-of-range route must emit the (non-constant) sine");
    assert(swingConst < swingSine * 0.05 && "constant feed is ~flat at steady state");
    // And the two outputs are not the same signal.
    bool differs = false;
    for (size_t k = 0; k < outSine.size(); ++k)
        if (std::fabs(outSine[k] - outConst[k]) > 1e-4f) { differs = true; break; }
    assert(differs);
    std::puts("PASS test_ac3_out_of_range_sine");
}

// ── AC4 — backward-compat default (no /obj/input) ────────────────────────────
static void test_ac4_default(const geometry::SpeakerLayout& layout) {
    // (a) state: a fresh engine with no routing has the default sentinel.
    core::SpatialEngine e0(0); configEngine(e0, layout);
    dMove(e0, 0, kAz, kEl, kDist);
    dMove(e0, 1, kAz, kEl, kDist);
    pumpInput(e0, kSpk, {0.1f, 0.5f}, 4);
    for (int i = 0; i < spe::MAX_OBJECTS; ++i) {
        const auto r = e0.objInputRouteAt(static_cast<size_t>(i));
        assert(r.src_ch == -1 && std::fabs(r.gain - 1.0f) < 1e-6f);
    }
    // (b) default-route audio: obj0 reads ch0 (0.1), obj1 reads ch1 (0.5) at the
    //     SAME position ⇒ output ratio ≈ 0.5/0.1 = 5.0 (the identical chain cancels).
    const std::vector<float> in = {0.1f, 0.5f};
    core::SpatialEngine eA(0); configEngine(eA, layout);
    dMove(eA, 0, kAz, kEl, kDist);                 // default → src = 0
    const auto outA = pumpInput(eA, kSpk, in, 24);

    core::SpatialEngine eB(0); configEngine(eB, layout);
    dMove(eB, 1, kAz, kEl, kDist);                 // default → src = 1
    const auto outB = pumpInput(eB, kSpk, in, 24);

    const double eA_ = energy(outA), eB_ = energy(outB);
    assert(eA_ > 1e-4);
    const double ratio = eB_ / eA_;
    std::printf("  AC4 default ratio = %.4f (expect ~5.0)\n", ratio);
    assert(std::fabs(ratio - 5.0) < 5e-3);
    std::puts("PASS test_ac4_default");
}

// ── AC5 — reset sentinel + /sys/reset ────────────────────────────────────────
static void test_ac5_reset(const geometry::SpeakerLayout& layout) {
    core::SpatialEngine e(0); configEngine(e, layout);
    dMove(e, 0, kAz, kEl, kDist);
    dInput(e, 0, 1, 2.0f);
    pumpInput(e, kSpk, {0.1f, 0.4f}, 2);
    {
        const auto r = e.objInputRouteAt(0);
        assert(r.src_ch == 1 && std::fabs(r.gain - 2.0f) < 1e-6f);
    }
    // Explicit reset to default via src=-1.
    dInput(e, 0, -1, 1.0f);
    pumpInput(e, kSpk, {0.1f, 0.4f}, 2);
    {
        const auto r = e.objInputRouteAt(0);
        assert(r.src_ch == -1 && std::fabs(r.gain - 1.0f) < 1e-6f);
    }
    // /sys/reset clears routing for every object via obj_cache_.fill(ObjCache{}).
    dInput(e, 0, 2, 0.5f);
    dInput(e, 3, 1, 3.0f);
    dInput(e, 7, 0, 0.25f);
    pumpInput(e, kSpk, {0.1f, 0.4f, 0.7f}, 2);
    dReset(e);
    pumpInput(e, kSpk, {0.1f, 0.4f, 0.7f}, 2);
    for (int i = 0; i < spe::MAX_OBJECTS; ++i) {
        const auto r = e.objInputRouteAt(static_cast<size_t>(i));
        assert(r.src_ch == -1 && std::fabs(r.gain - 1.0f) < 1e-6f);
    }
    std::puts("PASS test_ac5_reset");
}

// ── DEC — decoder unit (raw OSC bytes via the public decode()) ───────────────
// Native /obj/* commands carry the schema_version+seq int prefix (the engine-
// wide payload_int_offset convention, shared with /obj/dsp), so the wire form
// is ,iiiif <schema> <seq> obj_id src_ch gain. The decoder mirrors /obj/dsp
// (getInt(0)=obj_id, getInt(1)=src_ch, getFloat(0)=gain) and requires the full
// payload + src_ch >= -1.
static void appendStr(std::vector<uint8_t>& v, const char* s) {
    const size_t l = std::strlen(s), t = (l + 4) & ~size_t(3);
    for (size_t k = 0; k < l; ++k) v.push_back(static_cast<uint8_t>(s[k]));
    for (size_t k = l; k < t; ++k) v.push_back(0);
}
static void appendI32(std::vector<uint8_t>& v, int32_t i) {
    const uint32_t u = static_cast<uint32_t>(i);
    v.push_back(static_cast<uint8_t>(u >> 24)); v.push_back(static_cast<uint8_t>(u >> 16));
    v.push_back(static_cast<uint8_t>(u >> 8));  v.push_back(static_cast<uint8_t>(u));
}
static void appendF32(std::vector<uint8_t>& v, float f) {
    uint32_t u; std::memcpy(&u, &f, 4);
    v.push_back(static_cast<uint8_t>(u >> 24)); v.push_back(static_cast<uint8_t>(u >> 16));
    v.push_back(static_cast<uint8_t>(u >> 8));  v.push_back(static_cast<uint8_t>(u));
}
static void test_dec_decoder() {
    using namespace spe::ipc;
    {   // /obj/input ,iiiif 1 7 3 5 2.0 → ObjInput{3,5,2.0}
        std::vector<uint8_t> p;
        appendStr(p, "/obj/input"); appendStr(p, ",iiiif");
        appendI32(p, 1); appendI32(p, 7); appendI32(p, 3); appendI32(p, 5); appendF32(p, 2.0f);
        CommandDecoder dec;
        const Command c = dec.decode(std::span<const uint8_t>(p.data(), p.size()));
        assert(c.tag == CommandTag::ObjInput);
        const auto* q = std::get_if<PayloadObjInput>(&c.payload);
        assert(q && q->obj_id == 3u && q->src_ch == 5 && std::fabs(q->gain - 2.0f) < 1e-6f);
    }
    {   // reset form: src_ch = -1 decodes
        std::vector<uint8_t> p;
        appendStr(p, "/obj/input"); appendStr(p, ",iiiif");
        appendI32(p, 1); appendI32(p, 7); appendI32(p, 0); appendI32(p, -1); appendF32(p, 1.0f);
        CommandDecoder dec;
        const Command c = dec.decode(std::span<const uint8_t>(p.data(), p.size()));
        assert(c.tag == CommandTag::ObjInput);
        const auto* q = std::get_if<PayloadObjInput>(&c.payload);
        assert(q && q->obj_id == 0u && q->src_ch == -1 && std::fabs(q->gain - 1.0f) < 1e-6f);
    }
    {   // malformed arity (,if — missing src_ch) → Unknown
        std::vector<uint8_t> p;
        appendStr(p, "/obj/input"); appendStr(p, ",if");
        appendI32(p, 5); appendF32(p, 2.0f);
        CommandDecoder dec;
        const Command c = dec.decode(std::span<const uint8_t>(p.data(), p.size()));
        assert(c.tag == CommandTag::Unknown);
    }
    {   // src_ch < -1 → Unknown
        std::vector<uint8_t> p;
        appendStr(p, "/obj/input"); appendStr(p, ",iiiif");
        appendI32(p, 1); appendI32(p, 7); appendI32(p, 0); appendI32(p, -2); appendF32(p, 1.0f);
        CommandDecoder dec;
        const Command c = dec.decode(std::span<const uint8_t>(p.data(), p.size()));
        assert(c.tag == CommandTag::Unknown);
    }
    std::puts("PASS test_dec_decoder");
}

int main(int argc, char** argv) {
    std::string configs_dir = (argc >= 2) ? std::string(argv[1]) : std::string(SPE_CONFIGS_DIR);
    std::puts("test_a3_input_routing: per-object input→object routing");
    const auto layout = loadLayout(configs_dir);
    test_dec_decoder();
    test_ac1a_state(layout);
    test_ac1b_ratio(layout);
    test_ac2_fanout(layout);
    test_ac3_out_of_range_sine(layout);
    test_ac4_default(layout);
    test_ac5_reset(layout);
    std::puts("test_a3_input_routing: ALL PASS");
    return 0;
}

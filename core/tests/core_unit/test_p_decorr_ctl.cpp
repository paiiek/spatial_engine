// test_p_decorr_ctl.cpp
// Dreamscape Convergence ⑦ — /decorr/* OSC wire format + live-engine apply.
//
// (1) decode every op (enable / set ,fffiii / per-param) + reject paths.
// (2) encode → decode round-trip for each op.
// (3) live engine: /decorr/set applies through OSC→FIFO→drain (introspected);
//     the drain allocates nothing (rt_alloc_violations()==0 under RT asserts);
//     enabling decorrelation measurably changes the speaker-bus output.

#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"
#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "audio_io/AudioCallback.h"
#include "util/RtAssertNoAlloc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace spe::ipc;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Build a raw OSC packet (single-leading-tag, no seq/id). `types` order matches
// the args: floats in `f`, ints in `iargs` consumed left to right by tag.
static std::vector<uint8_t> osc(const std::string& addr, const std::string& types,
                                const std::vector<float>& fargs,
                                const std::vector<int32_t>& iargs) {
    auto pad = [](std::vector<uint8_t>& b){ while (b.size() % 4) b.push_back(0); };
    std::vector<uint8_t> p;
    for (char c : addr) { p.push_back((uint8_t)c); }
    p.push_back(0); pad(p);
    std::string tt = "," + types;
    for (char c : tt) { p.push_back((uint8_t)c); }
    p.push_back(0); pad(p);
    size_t fi = 0, ii = 0;
    auto be32 = [&](uint32_t u){
        p.push_back((uint8_t)((u>>24)&0xFF)); p.push_back((uint8_t)((u>>16)&0xFF));
        p.push_back((uint8_t)((u>>8)&0xFF));  p.push_back((uint8_t)(u&0xFF));
    };
    for (char t : types) {
        if (t == 'f') { uint32_t u; float v = fargs[fi++]; std::memcpy(&u,&v,4); be32(u); }
        else if (t == 'i') { be32((uint32_t)iargs[ii++]); }
    }
    return p;
}

int main() {
    CommandDecoder dec;
    using DOp = PayloadDecorrCtl::Op;

    // ---- (1) decode per-param + enable ----
    {
        auto pkt = osc("/decorr/enable", "i", {}, {1});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::DecorrCtl, "enable: tag");
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::Enable && p.enabled, "enable: op+flag");
    }
    {
        auto pkt = osc("/decorr/mix", "f", {0.6f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::Mix && feq(p.mix01, 0.6f), "mix");
    }
    {
        auto pkt = osc("/decorr/spread", "f", {12.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::Spread && feq(p.delay_spread_ms, 12.f), "spread");
    }
    {
        auto pkt = osc("/decorr/ap", "f", {0.8f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::Ap && feq(p.ap_coeff01, 0.8f), "ap");
    }
    {
        auto pkt = osc("/decorr/stages", "i", {}, {6});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::Stages && p.stages == 6, "stages");
    }
    {
        auto pkt = osc("/decorr/seed", "i", {}, {424242});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::Seed && p.seed == 424242u, "seed");
    }
    // ---- /decorr/set ,fffiii ----
    {
        auto pkt = osc("/decorr/set", "fffiii", {0.5f, 10.f, 0.7f}, {1, 5, 9999});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::DecorrCtl, "set: tag");
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::SetAll, "set: op");
        CHECK(feq(p.mix01, 0.5f) && feq(p.delay_spread_ms, 10.f) && feq(p.ap_coeff01, 0.7f),
              "set: floats mix/spread/ap");
        CHECK(p.enabled && p.stages == 5 && p.seed == 9999u, "set: ints enabled/stages/seed");
    }
    // ---- /decorr/set with INTERLEAVED tags (,ififif) — separate-array guarantee ----
    // The decoder stores ints and floats in separate positional arrays, so tag
    // ORDER must not affect indexing: getFloat(k)=k-th float, getInt(k)=k-th int
    // regardless of interleaving. osc() consumes f/i tags left-to-right from the
    // two arg vectors, so "ififif" → floats[mix,spread,ap], ints[en,stages,seed].
    {
        auto pkt = osc("/decorr/set", "ififif", {0.5f, 10.f, 0.7f}, {1, 5, 9999});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::DecorrCtl, "set interleaved: tag");
        auto& p = std::get<PayloadDecorrCtl>(c.payload);
        CHECK(p.op == DOp::SetAll, "set interleaved: op");
        CHECK(feq(p.mix01, 0.5f) && feq(p.delay_spread_ms, 10.f) && feq(p.ap_coeff01, 0.7f),
              "set interleaved: floats by position");
        CHECK(p.enabled && p.stages == 5 && p.seed == 9999u,
              "set interleaved: ints by position (tag order irrelevant)");
    }
    // ---- reject: partial /decorr/set (missing ints) ----
    {
        const uint32_t before = dec.rejectCount();
        auto pkt = osc("/decorr/set", "fff", {0.5f, 10.f, 0.7f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::Unknown, "partial set rejected");
        CHECK(dec.rejectCount() == before + 1, "partial set reject counted");
    }
    // ---- reject: unknown /decorr leaf ----
    {
        auto pkt = osc("/decorr/bogus", "f", {1.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::Unknown, "unknown leaf rejected");
    }

    // ---- (2) encode → decode round-trip ----
    {
        auto rt = [&](const PayloadDecorrCtl& in){
            Command c; c.tag = CommandTag::DecorrCtl; c.payload = in;
            std::vector<uint8_t> buf; CHECK(dec.encode(c, buf), "encode ok");
            Command d = dec.decode(std::span<const uint8_t>(buf));
            CHECK(d.tag == CommandTag::DecorrCtl, "rt tag");
            return std::get<PayloadDecorrCtl>(d.payload);
        };
        { PayloadDecorrCtl p; p.op = DOp::Enable; p.enabled = true;
          auto o = rt(p); CHECK(o.op == p.op && o.enabled, "rt enable"); }
        { PayloadDecorrCtl p; p.op = DOp::Mix; p.mix01 = 0.42f;
          auto o = rt(p); CHECK(feq(o.mix01, 0.42f), "rt mix"); }
        { PayloadDecorrCtl p; p.op = DOp::Stages; p.stages = 7;
          auto o = rt(p); CHECK(o.stages == 7, "rt stages"); }
        { PayloadDecorrCtl p; p.op = DOp::Seed; p.seed = 0xDEADBEEFu;
          auto o = rt(p); CHECK(o.seed == 0xDEADBEEFu, "rt seed (uint32 exact)"); }
        { PayloadDecorrCtl p; p.op = DOp::SetAll;
          p.enabled = true; p.mix01 = 0.55f; p.delay_spread_ms = 8.f; p.ap_coeff01 = 0.66f;
          p.stages = 3; p.seed = 12321u;
          auto o = rt(p);
          CHECK(o.op == DOp::SetAll, "rt set op");
          CHECK(feq(o.mix01,0.55f)&&feq(o.delay_spread_ms,8.f)&&feq(o.ap_coeff01,0.66f), "rt set floats");
          CHECK(o.enabled && o.stages==3 && o.seed==12321u, "rt set ints"); }
    }

    // ---- (3) live engine apply + no-alloc + audible change ----
    {
        spe::core::SpatialEngine engine(0);
        spe::geometry::SpeakerLayout l; l.name = "quad";
        for (int ch = 1; ch <= 4; ++ch) {
            spe::geometry::Speaker s; s.channel = ch;
            const float az = -3.14159265f + 2.f*3.14159265f*(ch-1)/4.f;
            s.x = std::sin(az); s.z = std::cos(az);
            l.speakers.push_back(s);
        }
        engine.setLayout(l);
        engine.prepareToPlay(48000.0, 256);

        auto send = [&](const PayloadDecorrCtl& p){
            Command c; c.tag = CommandTag::DecorrCtl; c.payload = p; engine.dispatchCommand(c);
        };
        auto sendCmd = [&](CommandTag t, auto payload){
            Command c; c.tag = t; c.payload = payload; engine.dispatchCommand(c);
        };
        // An active object so the bus is non-silent.
        { PayloadObjMove p; p.obj_id = 0; p.az_rad = 0.3f; p.el_rad = 0.f; p.dist_m = 2.f;
          sendCmd(CommandTag::ObjMove, p); }

        const int N = 4;
        std::vector<std::vector<float>> bufs((size_t)N, std::vector<float>(256, 0.f));
        std::vector<float*> ptrs((size_t)N);
        for (int s = 0; s < N; ++s) ptrs[(size_t)s] = bufs[(size_t)s].data();
        auto block = [&](){
            spe::audio_io::AudioBlock blk; blk.output_channels = ptrs.data();
            blk.output_channel_count = N; blk.num_frames = 256; blk.sample_rate = 48000.0;
            engine.audioBlock(blk);
        };

        // Apply /decorr/set and confirm it lands in the engine.
        { PayloadDecorrCtl p; p.op = DOp::SetAll; p.enabled = true; p.mix01 = 0.8f;
          p.delay_spread_ms = 10.f; p.ap_coeff01 = 0.7f; p.stages = 5; p.seed = 31337u; send(p); }
        spe::util::rt_alloc_violations_reset();
        block();
        CHECK(spe::util::rt_alloc_violations() == 0, "(RT) decorr drain + process allocates nothing");
        CHECK(engine.decorrEnabledForTest() && feq(engine.decorrMixForTest(), 0.8f), "apply: enabled+mix");
        CHECK(engine.decorrStagesForTest() == 5 && engine.decorrSeedForTest() == 31337u, "apply: stages+seed");
        CHECK(feq(engine.decorrSpreadForTest(), 10.f) && feq(engine.decorrApForTest(), 0.7f), "apply: spread+ap");

        // Audible change: capture energy with decorr ON, then OFF (fresh engine
        // each, identical object) — the per-channel allpass redistributes the bus
        // so the per-speaker energy profile differs.
        auto capture = [&](bool on){
            spe::core::SpatialEngine e(0); e.setLayout(l); e.prepareToPlay(48000.0, 256);
            Command c; c.tag = CommandTag::ObjMove;
            PayloadObjMove m; m.obj_id = 0; m.az_rad = 0.3f; m.dist_m = 2.f; c.payload = m;
            e.dispatchCommand(c);
            if (on) { Command d; d.tag = CommandTag::DecorrCtl;
                PayloadDecorrCtl p; p.op = DOp::SetAll; p.enabled = true; p.mix01 = 0.9f;
                p.delay_spread_ms = 12.f; p.ap_coeff01 = 0.7f; p.stages = 6; p.seed = 7u;
                d.payload = p; e.dispatchCommand(d); }
            std::vector<std::vector<float>> b((size_t)N, std::vector<float>(256, 0.f));
            std::vector<float*> pr((size_t)N);
            for (int s = 0; s < N; ++s) pr[(size_t)s] = b[(size_t)s].data();
            std::vector<double> en((size_t)N, 0.0);
            for (int blk_i = 0; blk_i < 20; ++blk_i) {
                spe::audio_io::AudioBlock blk; blk.output_channels = pr.data();
                blk.output_channel_count = N; blk.num_frames = 256; blk.sample_rate = 48000.0;
                e.audioBlock(blk);
                for (int s = 0; s < N; ++s)
                    for (int n = 0; n < 256; ++n) {
                        const float v = b[(size_t)s][(size_t)n];
                        en[(size_t)s] += (double)v * v;
                    }
            }
            return en;
        };
        const auto eOff = capture(false);
        const auto eOn  = capture(true);
        double absDelta = 0.0, base = 0.0;
        for (int s = 0; s < N; ++s) { base += eOff[(size_t)s];
            absDelta += std::fabs(eOn[(size_t)s] - eOff[(size_t)s]); }
        std::printf("[decorr-ctl] bus Σ|Δspk|=%.4g (%.2f%% of off=%.4g)\n",
                    absDelta, 100.0*absDelta/(base+1e-30), base);
        CHECK(base > 1e-9, "decorr: bus non-silent with decorr off");
        CHECK(absDelta > 0.02 * base, "decorr: enabling decorrelation measurably changes the bus");
    }

    if (failures == 0) { std::printf("test_p_decorr_ctl: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_p_decorr_ctl: %d FAIL\n", failures);
    return 1;
}

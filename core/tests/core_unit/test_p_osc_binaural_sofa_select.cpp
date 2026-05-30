// test_p_osc_binaural_sofa_select.cpp
// B-M3 gate: /sys/binaural_sofa_select ,s round-trip + rejection tests.
//
// Tests:
//   1. Encode + decode round-trip: name survives encode → decode.
//   2. Empty string → makeUnknown() (tag == Unknown, rejectCount increments).
//   3. Unknown/out-of-catalog name decodes as SysBinauralSofaSelect but
//      HrtfCatalog::find() returns nullptr (safe no-op contract).
//   4. Known catalog name decodes and resolves through HrtfCatalog to the
//      expected speh_path.

#include "hrtf/HrtfCatalog.h"
#include "ipc/Command.h"
#include "ipc/CommandDecoder.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace spe::ipc;

// Helper: build a raw OSC packet for /sys/binaural_sofa_select ,s "<name>"
// so we can test the decode path directly without going through encode().
static std::vector<uint8_t> makeSelectPacket(const std::string& name)
{
    // OSC address: /sys/binaural_sofa_select
    const std::string addr = "/sys/binaural_sofa_select";
    // type-tag string: ",s"
    const std::string tags = ",s";

    auto padTo4 = [](std::vector<uint8_t>& v) {
        while (v.size() % 4 != 0) v.push_back(0);
    };

    std::vector<uint8_t> pkt;
    // Address
    for (char c : addr) pkt.push_back(static_cast<uint8_t>(c));
    pkt.push_back(0);
    padTo4(pkt);
    // Type tags
    for (char c : tags) pkt.push_back(static_cast<uint8_t>(c));
    pkt.push_back(0);
    padTo4(pkt);
    // String argument
    for (char c : name) pkt.push_back(static_cast<uint8_t>(c));
    pkt.push_back(0);
    padTo4(pkt);
    return pkt;
}

int main(int argc, char* argv[])
{
    // ─────────────────────────────────────────────────────────────────
    // Test 1: encode + decode round-trip preserves the catalog name.
    // ─────────────────────────────────────────────────────────────────
    {
        CommandDecoder dec;
        const std::string kName = "kemar";

        Command cmd;
        cmd.tag = CommandTag::SysBinauralSofaSelect;
        cmd.seq = 1;
        cmd.id  = 1;
        PayloadSysBinauralSofaSelect p;
        p.name = kName;
        cmd.payload = p;

        std::vector<uint8_t> buf;
        bool ok = dec.encode(cmd, buf);
        assert(ok && "Test 1: encode failed");

        Command rt = dec.decode(std::span<const uint8_t>(buf));
        assert(rt.tag == CommandTag::SysBinauralSofaSelect
               && "Test 1: decoded tag mismatch");
        auto& rp = std::get<PayloadSysBinauralSofaSelect>(rt.payload);
        assert(rp.name == kName && "Test 1: name mismatch after round-trip");
        assert(dec.rejectCount() == 0 && "Test 1: unexpected rejectCount");
        std::puts("PASS Test 1: encode/decode round-trip");
    }

    // ─────────────────────────────────────────────────────────────────
    // Test 2: empty string → makeUnknown() (tag == Unknown).
    // ─────────────────────────────────────────────────────────────────
    {
        CommandDecoder dec;
        auto pkt = makeSelectPacket("");   // empty string argument
        Command rt = dec.decode(std::span<const uint8_t>(pkt));
        assert(rt.tag == CommandTag::Unknown
               && "Test 2: empty string must produce Unknown");
        assert(dec.rejectCount() == 1
               && "Test 2: rejectCount must be 1 for empty-string rejection");
        std::puts("PASS Test 2: empty string → Unknown + rejectCount==1");
    }

    // ─────────────────────────────────────────────────────────────────
    // Test 3: no-arg packet → makeUnknown().
    // ─────────────────────────────────────────────────────────────────
    {
        // Build a packet with no string argument (type-tag ",")
        const std::string addr = "/sys/binaural_sofa_select";
        const std::string tags = ",";  // no args
        auto padTo4 = [](std::vector<uint8_t>& v) {
            while (v.size() % 4 != 0) v.push_back(0);
        };
        std::vector<uint8_t> pkt;
        for (char c : addr) pkt.push_back(static_cast<uint8_t>(c));
        pkt.push_back(0); padTo4(pkt);
        for (char c : tags) pkt.push_back(static_cast<uint8_t>(c));
        pkt.push_back(0); padTo4(pkt);

        CommandDecoder dec;
        Command rt = dec.decode(std::span<const uint8_t>(pkt));
        assert(rt.tag == CommandTag::Unknown
               && "Test 3: missing string arg must produce Unknown");
        std::puts("PASS Test 3: missing string arg → Unknown");
    }

    // ─────────────────────────────────────────────────────────────────
    // Test 4: out-of-catalog name decodes as SysBinauralSofaSelect but
    //         HrtfCatalog::find() returns nullptr — safe no-op contract.
    // ─────────────────────────────────────────────────────────────────
    {
        CommandDecoder dec;
        auto pkt = makeSelectPacket("nonexistent_dataset_xyz");
        Command rt = dec.decode(std::span<const uint8_t>(pkt));
        assert(rt.tag == CommandTag::SysBinauralSofaSelect
               && "Test 4: unknown name should still decode as SysBinauralSofaSelect");
        auto& rp = std::get<PayloadSysBinauralSofaSelect>(rt.payload);
        assert(rp.name == "nonexistent_dataset_xyz");

        // Catalog lookup returns nullptr → engine does safe no-op.
        spe::hrtf::HrtfCatalog cat;
        const auto* entry = cat.find(rp.name);
        assert(entry == nullptr
               && "Test 4: out-of-catalog name must return nullptr from find()");
        std::puts("PASS Test 4: out-of-catalog name → nullptr (safe no-op)");
    }

    // ─────────────────────────────────────────────────────────────────
    // Test 5: known catalog name resolves to the expected speh_path.
    //         Requires catalog.json to be available (SPE_CATALOG_PATH).
    // ─────────────────────────────────────────────────────────────────
#ifdef SPE_CATALOG_PATH
    {
        spe::hrtf::HrtfCatalog cat;
        bool loaded = cat.load(SPE_CATALOG_PATH);
        assert(loaded && "Test 5: catalog load failed");

        // Test round-trip: decode "kemar", resolve → speh_path.
        CommandDecoder dec;
        auto pkt = makeSelectPacket("kemar");
        Command rt = dec.decode(std::span<const uint8_t>(pkt));
        assert(rt.tag == CommandTag::SysBinauralSofaSelect);
        auto& rp = std::get<PayloadSysBinauralSofaSelect>(rt.payload);
        assert(rp.name == "kemar");

        const auto* entry = cat.find(rp.name);
        assert(entry != nullptr && "Test 5: 'kemar' must be in catalog");
        assert(!entry->speh_path.empty() && "Test 5: speh_path must be non-empty");
        assert(entry->speh_path == "assets/hrtf/kemar.speh"
               && "Test 5: speh_path mismatch");
        std::puts("PASS Test 5: catalog name 'kemar' resolves to correct speh_path");
    }
#else
    std::puts("SKIP Test 5: SPE_CATALOG_PATH not defined");
#endif

    std::puts("ALL PASS test_p_osc_binaural_sofa_select");
    return 0;
}

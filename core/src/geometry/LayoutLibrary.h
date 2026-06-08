// core/src/geometry/LayoutLibrary.h
// Phase 4.3 — a persistent library of up to 50 speaker-layout slots, one YAML
// file per slot under a directory (mirrors the reference engine's
// SpeakerLayoutSlots library, SpatialSessionState.cpp:789-868, in mmhoa's
// canonical YAML). Pure control-thread file I/O on top of LayoutLoader's
// load_layout/save_layout — the RT audio thread MUST NOT call any of this.
//
// Each slot file is `speaker_layout_NN.yaml`; an optional `label: "..."` top-
// level key (ignored by load_layout) carries a human display label. An
// in-memory occupancy/label index is built once at construction so /list does
// not stat 50 files per query.
#pragma once

#include "geometry/LayoutLoader.h"
#include "geometry/SpeakerLayout.h"

#include <array>
#include <string>

namespace spe::geometry {

class LayoutLibrary {
public:
    static constexpr int kSlotCount = 50;

    // Roots the library at `dir` (created if absent) and scans existing slots.
    explicit LayoutLibrary(std::string dir);

    // Save `layout` to `slot` with display `label`. Returns false on bad slot
    // index or I/O failure. Overwrites any existing slot.
    bool save(int slot, const SpeakerLayout& layout, const std::string& label);

    // Load the layout in `slot`. Returns a parse-error string if empty/bad.
    LayoutResult load(int slot) const;

    // Delete `slot`'s file. Returns false on bad index; true if now empty
    // (including when it was already empty).
    bool clear(int slot);

    bool        occupied(int slot) const noexcept;
    std::string label(int slot) const;          // "" if empty/bad slot
    int         occupiedCount() const noexcept;

private:
    static bool validSlot(int slot) noexcept { return slot >= 0 && slot < kSlotCount; }
    std::string slotPath(int slot) const;
    void        rescanSlot(int slot);

    struct Slot { bool occupied = false; std::string label; };

    std::string                       dir_;
    std::array<Slot, kSlotCount>      slots_{};
};

}  // namespace spe::geometry

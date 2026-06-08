// core/src/geometry/LayoutLibrary.cpp
#include "geometry/LayoutLibrary.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

namespace spe::geometry {

namespace fs = std::filesystem;

static std::string slotFileName(int slot) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "speaker_layout_%02d.yaml", slot);
    return std::string(buf);
}

LayoutLibrary::LayoutLibrary(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);          // best-effort; save() reports real failure
    for (int s = 0; s < kSlotCount; ++s) rescanSlot(s);
}

std::string LayoutLibrary::slotPath(int slot) const {
    return (fs::path(dir_) / slotFileName(slot)).string();
}

void LayoutLibrary::rescanSlot(int slot) {
    auto& dst = slots_[static_cast<size_t>(slot)];
    dst = Slot{};
    std::error_code ec;
    if (!fs::exists(slotPath(slot), ec)) return;
    dst.occupied = true;
    // Display label = `label:` key if present, else the layout `name:`.
    try {
        YAML::Node root = YAML::LoadFile(slotPath(slot));
        if (root["label"])     dst.label = root["label"].as<std::string>();
        else if (root["name"]) dst.label = root["name"].as<std::string>();
    } catch (...) { /* occupied but unreadable label — leave empty */ }
}

bool LayoutLibrary::save(int slot, const SpeakerLayout& layout, const std::string& label) {
    if (!validSlot(slot)) return false;
    const std::string path = slotPath(slot);
    if (!save_layout(layout, path)) return false;
    // Append the display label as a YAML key load_layout ignores (we read it
    // back for the occupancy index). Labels are display strings — no embedded
    // quotes/newlines expected.
    {
        std::ofstream out(path, std::ios::app);
        if (!out) return false;
        out << "label: \"" << label << "\"\n";
        out.flush();
        if (!out) return false;
    }
    slots_[static_cast<size_t>(slot)] = Slot{true, label};
    return true;
}

LayoutResult LayoutLibrary::load(int slot) const {
    if (!validSlot(slot)) return std::string("layout slot out of range");
    if (!slots_[static_cast<size_t>(slot)].occupied) return std::string("layout slot empty");
    return load_layout(slotPath(slot));
}

bool LayoutLibrary::clear(int slot) {
    if (!validSlot(slot)) return false;
    std::error_code ec;
    fs::remove(slotPath(slot), ec);            // no-op if already absent
    slots_[static_cast<size_t>(slot)] = Slot{};
    return true;
}

bool LayoutLibrary::occupied(int slot) const noexcept {
    return validSlot(slot) && slots_[static_cast<size_t>(slot)].occupied;
}

std::string LayoutLibrary::label(int slot) const {
    return validSlot(slot) ? slots_[static_cast<size_t>(slot)].label : std::string();
}

int LayoutLibrary::occupiedCount() const noexcept {
    int n = 0;
    for (const auto& s : slots_) if (s.occupied) ++n;
    return n;
}

}  // namespace spe::geometry

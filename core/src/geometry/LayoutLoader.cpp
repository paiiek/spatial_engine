// core/src/geometry/LayoutLoader.cpp

#include "geometry/LayoutLoader.h"
#include "coords/Coords.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <limits>
#include <sstream>

namespace spe::geometry {

static std::string make_error(const std::string& msg) {
    return msg;
}

static const char* regularity_to_string(Regularity r) {
    switch (r) {
        case Regularity::LINEAR:      return "LINEAR";
        case Regularity::CIRCULAR:    return "CIRCULAR";
        case Regularity::PLANAR_GRID: return "PLANAR_GRID";
        case Regularity::IRREGULAR:   break;
    }
    return "IRREGULAR";
}

LayoutResult load_layout(const std::string& yaml_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path);
    } catch (const YAML::Exception& e) {
        return make_error(std::string("yaml parse error: ") + e.what());
    }

    if (!root["version"]) return make_error(kErrMissingVersion);
    if (!root["name"])    return make_error(kErrMissingName);
    if (!root["speakers"] || !root["speakers"].IsSequence())
        return make_error(kErrMissingSpeakers);

    SpeakerLayout layout;
    layout.version = root["version"].as<std::string>();
    layout.name    = root["name"].as<std::string>();

    // Initialise YAML channel → vector index lookup to "unmapped" (-1).
    layout.channel_to_idx_.fill(static_cast<int16_t>(-1));

    // Parse regularity_hint if present.
    if (root["regularity_hint"]) {
        const auto hint = root["regularity_hint"].as<std::string>();
        if      (hint == "LINEAR")      layout.regularity = Regularity::LINEAR;
        else if (hint == "CIRCULAR")    layout.regularity = Regularity::CIRCULAR;
        else if (hint == "PLANAR_GRID") layout.regularity = Regularity::PLANAR_GRID;
        else                            layout.regularity = Regularity::IRREGULAR;
    }

    for (const auto& node : root["speakers"]) {
        // Support either flat az_deg/el_deg or xyz node.
        bool flat_az  = node["az_deg"] && node["az_deg"].IsDefined();
        bool flat_xyz = node["x"] && node["x"].IsDefined();
        bool has_xyz  = node["xyz"].IsDefined();

        if (flat_az && flat_xyz) return make_error(kErrBothXyzAndSphere);
        if (has_xyz && flat_az)  return make_error(kErrBothXyzAndSphere);

        if (!node["channel"]) {
            return make_error(std::string("speaker missing 'channel'"));
        }

        const int ch = node["channel"].as<int>();
        if (ch < 1) return make_error(kErrNegativeChannel);
        // Spec says channel >= 1; ch==0 also illegal.
        if (ch < 1) return make_error(kErrBadChannelValue);
        if (ch > SpeakerLayout::kMaxYamlChannel) return make_error(kErrChannelTooLarge);

        // Reject duplicate YAML channel declarations: a second speaker
        // claiming the same channel:N would silently shadow the first under
        // the channel→index lookup. Loud failure beats silent mis-routing.
        if (layout.channel_to_idx_[static_cast<size_t>(ch)] != -1) {
            return make_error(kErrDuplicateChannel);
        }

        Speaker sp;
        sp.channel = ch;

        if (flat_az) {
            const float az_deg  = node["az_deg"].as<float>();
            const float el_deg  = node["el_deg"] ? node["el_deg"].as<float>() : 0.0f;
            const float dist_m  = node["dist_m"] ? node["dist_m"].as<float>() : 1.0f;
            auto xyz = spe::coords::yaml_speaker_to_cartesian(az_deg, el_deg, dist_m);
            sp.x = xyz[0];
            sp.y = xyz[1];
            sp.z = xyz[2];
        } else if (flat_xyz) {
            sp.x = node["x"].as<float>();
            sp.y = node["y"].as<float>();
            sp.z = node["z"].as<float>();
        } else if (has_xyz) {
            sp.x = node["xyz"][0].as<float>();
            sp.y = node["xyz"][1].as<float>();
            sp.z = node["xyz"][2].as<float>();
        } else {
            return make_error(std::string("speaker missing position (az_deg or x/y/z)"));
        }

        // Optional time-alignment fields (default 0)
        sp.delay_ms = node["delay_ms"] ? node["delay_ms"].as<float>() : 0.f;
        sp.gain_db  = node["gain_db"]  ? node["gain_db"].as<float>()  : 0.f;

        // Record (1-based YAML channel) → (0-based vector index) before push.
        layout.channel_to_idx_[static_cast<size_t>(ch)] =
            static_cast<int16_t>(layout.speakers.size());
        layout.speakers.push_back(sp);
    }

    if (layout.speakers.empty()) return make_error(kErrMissingSpeakers);

    return layout;
}

bool save_layout(const SpeakerLayout& layout, const std::string& yaml_path) {
    std::ofstream out(yaml_path, std::ios::trunc);
    if (!out) return false;

    // max_digits10 guarantees float values reload bit-exactly.
    out.precision(std::numeric_limits<float>::max_digits10);

    out << "version: \"" << (layout.version.empty() ? "1.0" : layout.version) << "\"\n";
    out << "name: \"" << layout.name << "\"\n";
    out << "regularity_hint: \"" << regularity_to_string(layout.regularity) << "\"\n";
    out << "speakers:\n";
    for (const auto& s : layout.speakers) {
        // Cartesian form (x/y/z) preserves the stored geometry exactly; az/el is
        // lossy on load (converted to x/y/z), so we never write it back.
        out << "  - channel: " << s.channel << "\n";
        out << "    x: " << s.x << "\n";
        out << "    y: " << s.y << "\n";
        out << "    z: " << s.z << "\n";
        if (s.delay_ms != 0.f) out << "    delay_ms: " << s.delay_ms << "\n";
        if (s.gain_db  != 0.f) out << "    gain_db: "  << s.gain_db  << "\n";
    }
    out.flush();
    return static_cast<bool>(out);
}

}  // namespace spe::geometry

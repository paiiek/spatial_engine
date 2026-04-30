// core/src/render/LayoutCompatibilityChecker.h
// P2: rule table compiled and registered; validate() returns Compatible for all pairs.
// P3 will fill in the real rules.

#pragma once

#include "geometry/SpeakerLayout.h"

#include <string>
#include <vector>

namespace spe::render {

enum class Algorithm : uint8_t {
    VBAP = 0,
    WFS  = 1,
    DBAP = 2,
};

enum class CompatStatus : uint8_t {
    Compatible   = 0,
    Incompatible = 1,
};

struct CompatResult {
    CompatStatus status;
    std::string  reason;
};

// Registry entry — kept for P3 expansion.
struct CompatRule {
    std::string layout_name;
    Algorithm   algorithm;
    CompatStatus status;
    std::string  reason;
};

class LayoutCompatibilityChecker {
public:
    // Register a layout+algorithm pair with a rule.
    void register_rule(const std::string& layout_name, Algorithm algo,
                       CompatStatus status, std::string reason = {});

    // P2: always returns Compatible regardless of registered rules.
    // WHY: P3 will read the rule table. Registering now keeps the API stable.
    CompatResult validate(const geometry::SpeakerLayout& layout,
                          Algorithm algo) const;

    const std::vector<CompatRule>& rules() const { return rules_; }

private:
    std::vector<CompatRule> rules_;
};

}  // namespace spe::render

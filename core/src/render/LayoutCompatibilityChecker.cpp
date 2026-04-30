// core/src/render/LayoutCompatibilityChecker.cpp

#include "render/LayoutCompatibilityChecker.h"

namespace spe::render {

void LayoutCompatibilityChecker::register_rule(const std::string& layout_name,
                                                Algorithm algo,
                                                CompatStatus status,
                                                std::string reason) {
    rules_.push_back({layout_name, algo, status, std::move(reason)});
}

CompatResult LayoutCompatibilityChecker::validate(
        const geometry::SpeakerLayout& /*layout*/,
        Algorithm /*algo*/) const {
    // P2 stub: all pairs compatible. P3 replaces this with rule-table lookup.
    return {CompatStatus::Compatible, {}};
}

}  // namespace spe::render

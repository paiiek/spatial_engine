// test_p3_compat_rules.cpp
// 6 known-bad pairs rejected with correct reason, 6 known-good pairs accepted.

#include "render/LayoutCompatibilityChecker.h"
#include "geometry/SpeakerLayout.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

using namespace spe::render;
using namespace spe::geometry;

static SpeakerLayout make_2d_circular(int n, float radius = 1.f) {
    SpeakerLayout l;
    l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < n; ++i) {
        float az = 2.f * 3.14159265f * i / n;
        Speaker s; s.channel = i+1;
        s.x = radius * std::sin(az); s.y = 0.f; s.z = radius * std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

static SpeakerLayout make_irregular(int n) {
    SpeakerLayout l;
    l.regularity = Regularity::IRREGULAR;
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i+1;
        s.x = (float)i * 0.3f; s.y = (float)i * 0.1f; s.z = 0.f;
        l.speakers.push_back(s);
    }
    return l;
}

// Linear array with spacing < 21.4 mm (e.g. 10 mm spacing)
static SpeakerLayout make_linear_tight(int n) {
    SpeakerLayout l;
    l.regularity = Regularity::LINEAR;
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i+1;
        s.x = i * 0.010f; s.y = 0.f; s.z = 0.f;  // 10 mm spacing
        l.speakers.push_back(s);
    }
    return l;
}

// Linear array with large spacing (1 m)
static SpeakerLayout make_linear_wide(int n) {
    SpeakerLayout l;
    l.regularity = Regularity::LINEAR;
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i+1;
        s.x = i * 1.0f; s.y = 0.f; s.z = 0.f;  // 1 m spacing
        l.speakers.push_back(s);
    }
    return l;
}

// 1D linear layout (all y=0, same z) → dimensionality = 1
static SpeakerLayout make_1d_linear(int n) {
    SpeakerLayout l;
    l.regularity = Regularity::LINEAR;
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i+1;
        s.x = (float)i; s.y = 0.f; s.z = 0.f;
        l.speakers.push_back(s);
    }
    return l;
}

int main() {
    LayoutCompatibilityChecker checker;

    // --- Known-bad pairs (should be Incompatible) ---

    // 1. VBAP on 1D linear layout (dimensionality < 2)
    {
        auto l = make_1d_linear(4);
        auto r = checker.validate(l, Algorithm::VBAP);
        CHECK(r.status == CompatStatus::Incompatible);
        CHECK(!r.reason.empty());
    }

    // 2. VBAP with only 2 speakers
    {
        auto l = make_2d_circular(2);
        auto r = checker.validate(l, Algorithm::VBAP);
        CHECK(r.status == CompatStatus::Incompatible);
    }

    // 3. WFS on irregular layout
    {
        auto l = make_irregular(8);
        auto r = checker.validate(l, Algorithm::WFS);
        CHECK(r.status == CompatStatus::Incompatible);
    }

    // 4. WFS on linear layout with wide spacing (> 21.4 mm)
    {
        auto l = make_linear_wide(4);
        auto r = checker.validate(l, Algorithm::WFS);
        CHECK(r.status == CompatStatus::Incompatible);
    }

    // 5. WFS on 2D circular layout with wide spacing
    {
        auto l = make_2d_circular(4, 2.f); // radius 2m → spacing ~2.8 m >> 21.4 mm
        auto r = checker.validate(l, Algorithm::WFS);
        CHECK(r.status == CompatStatus::Incompatible);
    }

    // 6. DBAP with only 1 speaker
    {
        SpeakerLayout l;
        l.regularity = Regularity::IRREGULAR;
        Speaker s; s.channel=1; s.x=0; s.y=0; s.z=1;
        l.speakers.push_back(s);
        auto r = checker.validate(l, Algorithm::DBAP);
        CHECK(r.status == CompatStatus::Incompatible);
    }

    // --- Known-good pairs (should be Compatible) ---

    // 7. VBAP on 2D circular with 4 speakers
    {
        auto l = make_2d_circular(4);
        auto r = checker.validate(l, Algorithm::VBAP);
        CHECK(r.status == CompatStatus::Compatible);
    }

    // 8. VBAP on 2D circular with 3 speakers (minimum)
    {
        auto l = make_2d_circular(3);
        auto r = checker.validate(l, Algorithm::VBAP);
        CHECK(r.status == CompatStatus::Compatible);
    }

    // 9. WFS on tight linear array (2 speakers, 10 mm spacing → max_spacing=10mm < 21.4mm)
    {
        auto l = make_linear_tight(2);
        auto r = checker.validate(l, Algorithm::WFS);
        CHECK(r.status == CompatStatus::Compatible);
    }

    // 10. DBAP on irregular layout (always passes if ≥2 speakers)
    {
        auto l = make_irregular(8);
        auto r = checker.validate(l, Algorithm::DBAP);
        CHECK(r.status == CompatStatus::Compatible);
    }

    // 11. DBAP with 2 speakers (minimum)
    {
        SpeakerLayout l;
        l.regularity = Regularity::IRREGULAR;
        for (int i = 0; i < 2; ++i) {
            Speaker s; s.channel=i+1; s.x=(float)i; s.y=0; s.z=0;
            l.speakers.push_back(s);
        }
        auto r = checker.validate(l, Algorithm::DBAP);
        CHECK(r.status == CompatStatus::Compatible);
    }

    // 12. VBAP on lab_8ch-style layout (2 elevation levels → dimensionality=2)
    {
        SpeakerLayout l;
        l.regularity = Regularity::CIRCULAR;
        // 4 lower + 4 upper
        float azs[] = {-135,  -45, 45, 135, -135, -45, 45, 135};
        float els[] = {0, 0, 0, 0, 30, 30, 30, 30};
        for (int i = 0; i < 8; ++i) {
            float az = azs[i] * 3.14159265f / 180.f;
            float el = els[i] * 3.14159265f / 180.f;
            Speaker s; s.channel=i+1;
            s.x = std::cos(el)*std::sin(az);
            s.y = std::sin(el);
            s.z = std::cos(el)*std::cos(az);
            l.speakers.push_back(s);
        }
        auto r = checker.validate(l, Algorithm::VBAP);
        CHECK(r.status == CompatStatus::Compatible);
    }

    if (failures == 0) std::printf("test_p3_compat_rules: ALL PASS\n");
    return failures;
}

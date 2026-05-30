// core/tests/core_unit/test_p_binaural_dataset_itd_oracle.cpp
//
// B-M6 — Per-dataset ITD oracle regression guard.
//
// For each catalog dataset whose .speh is present (skip-with-message when a
// fetched file is absent) PLUS the always-present synthetic_itd_pm90.speh
// fixture: load via HrtfLookup (active-slot path), query az=+90 and az=−90 at
// el=0, measure the inter-aural delay via onset threshold of L/R HRIR.
//
// Expected ITD is computed from the dataset's OWN sample_rate:
//   expected_samples = sample_rate * (a/c) * (theta + sin(theta))
//   a=0.0875 m, c=343 m/s, theta=pi/2
//   => (0.0875/343) * (pi/2 + 1) ≈ 0.000255 * 2.5708 ≈ 0.000656 s
//   => at 48000 Hz: ≈ 31.5 samples
//
// Sign convention (BinauralMonitor.h:40-50):
//   az=+90 deg = RIGHT side (engine convention, RIGHT=+az).
//   Right source => right ear leads => onset_R < onset_L => (onset_L - onset_R) > 0.
//
// Anti-vacuity: a counter of datasets actually exercised is checked at the end;
//   the test FAILS (returns 1) if the counter is 0. The synthetic_itd_pm90.speh
//   fixture is NOT skippable (committed), guaranteeing counter >= 1.

#include "hrtf/HrtfLookup.h"
#include "hrtf/SofaBinReader.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#error "SPE_FIXTURES_DIR must be defined by CMake"
#endif

#ifndef SPE_CATALOG_PATH
#error "SPE_CATALOG_PATH must be defined by CMake"
#endif

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Woodworth closed-form ITD in samples:
//   ITD = SR * (a/c) * (theta + sin(theta)), theta=pi/2
//   a = 0.0875 m (average head radius), c = 343 m/s
float woodworthITD(float sample_rate) {
    const float a = 0.0875f;
    const float c = 343.0f;
    const float theta = kPi / 2.0f;
    return sample_rate * (a / c) * (theta + std::sin(theta));
}

// Find onset (first sample index where |ir| > threshold * peak).
// Returns -1 if the IR is all zeros.
int onset(const float* ir, int len, float threshold = 0.1f) {
    float peak = 0.f;
    for (int i = 0; i < len; ++i)
        if (std::abs(ir[i]) > peak) peak = std::abs(ir[i]);
    if (peak < 1e-9f) return -1;
    for (int i = 0; i < len; ++i)
        if (std::abs(ir[i]) >= threshold * peak) return i;
    return -1;
}

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// Check a single dataset; returns true if the dataset was exercised (file
// present and assertions passed), false if skipped (file absent).
// On assertion failure it prints an error and returns false with *failed=true.
bool check_dataset(const std::string& name,
                   const std::string& speh_path,
                   bool               mandatory,
                   bool*              failed) {
    if (!file_exists(speh_path)) {
        if (mandatory) {
            std::fprintf(stderr,
                "FAIL [%s]: mandatory fixture not found: %s\n",
                name.c_str(), speh_path.c_str());
            *failed = true;
            return false;
        }
        std::printf("SKIP [%s]: %s not present (fetched dataset absent on CI)\n",
                    name.c_str(), speh_path.c_str());
        return false;
    }

    // Load via the active-slot path (HrtfLookup::loadIntoActive + activeTable/activeTree).
    spe::hrtf::HrtfLookup lut;
    // Pass expected_sr=0 to skip SR validation (each dataset has its own SR).
    const auto res = lut.loadIntoActive(speh_path, 0.f);
    if (res != spe::hrtf::SpehResult::Ok) {
        std::fprintf(stderr,
            "FAIL [%s]: loadIntoActive returned %d for %s\n",
            name.c_str(), static_cast<int>(res), speh_path.c_str());
        *failed = true;
        return false;
    }

    // Snapshot the active slot exactly ONCE (mirrors the audio-thread protocol).
    const int slot = lut.activeSlot();
    const spe::hrtf::HrtfTable& tbl  = lut.tableForSlot(slot);
    const spe::hrtf::KdTree3D&  tree = lut.treeForSlot(slot);

    const float sr = tbl.sample_rate;
    const float expected_itd = woodworthITD(sr);

    // az=+90 (right source): engine az in RADIANS for lookupHrtfFromTree.
    const float az_p90_rad = kPi / 2.f;
    const float az_m90_rad = -kPi / 2.f;
    const float el_rad     = 0.f;

    const auto hp = spe::hrtf::lookupHrtfFromTree(tbl, tree, az_p90_rad, el_rad);
    const auto hm = spe::hrtf::lookupHrtfFromTree(tbl, tree, az_m90_rad, el_rad);

    const int ir_len = static_cast<int>(tbl.ir_length);

    // az=+90: RIGHT source → right ear (hp.right) leads.
    const int onL_p90 = onset(hp.left,  ir_len);
    const int onR_p90 = onset(hp.right, ir_len);
    // az=−90: LEFT source → left ear (hm.left) leads.
    const int onL_m90 = onset(hm.left,  ir_len);
    const int onR_m90 = onset(hm.right, ir_len);

    if (onL_p90 < 0 || onR_p90 < 0 || onL_m90 < 0 || onR_m90 < 0) {
        std::fprintf(stderr,
            "FAIL [%s]: zero/silent HRIR detected (onL_p90=%d onR_p90=%d"
            " onL_m90=%d onR_m90=%d)\n",
            name.c_str(), onL_p90, onR_p90, onL_m90, onR_m90);
        *failed = true;
        return false;
    }

    // Measured ITD = onset difference; positive means left ear delayed (right leads).
    const float meas_p90 = static_cast<float>(onL_p90 - onR_p90);  // expected > 0
    const float meas_m90 = static_cast<float>(onR_m90 - onL_m90);  // expected > 0

    std::printf(
        "INFO [%s] SR=%.0f  Woodworth=%.2f smp  "
        "az+90: L-R_onset=%+.0f  az-90: R-L_onset=%+.0f\n",
        name.c_str(), sr, expected_itd, meas_p90, meas_m90);

    // ── Sign check ──────────────────────────────────────────────────────────
    // az=+90 (right source) → right ear leads → (onset_L − onset_R) > 0.
    if (meas_p90 <= 0.f) {
        std::fprintf(stderr,
            "FAIL [%s]: az=+90 sign wrong: onsetL=%d onsetR=%d "
            "(expected onsetL > onsetR for right source)\n",
            name.c_str(), onL_p90, onR_p90);
        *failed = true;
        return false;
    }
    // az=−90 (left source) → left ear leads → (onset_R − onset_L) > 0.
    if (meas_m90 <= 0.f) {
        std::fprintf(stderr,
            "FAIL [%s]: az=−90 sign wrong: onsetL=%d onsetR=%d "
            "(expected onsetR > onsetL for left source)\n",
            name.c_str(), onL_m90, onR_m90);
        *failed = true;
        return false;
    }

    // ── Magnitude check (within ±20% of Woodworth) ──────────────────────────
    const float tol = 0.20f;
    const float lo  = expected_itd * (1.f - tol);
    const float hi  = expected_itd * (1.f + tol);

    if (meas_p90 < lo || meas_p90 > hi) {
        std::fprintf(stderr,
            "FAIL [%s]: az=+90 ITD %.2f outside [%.2f, %.2f] samples "
            "(Woodworth=%.2f, ±20%%)\n",
            name.c_str(), meas_p90, lo, hi, expected_itd);
        *failed = true;
        return false;
    }
    if (meas_m90 < lo || meas_m90 > hi) {
        std::fprintf(stderr,
            "FAIL [%s]: az=−90 ITD %.2f outside [%.2f, %.2f] samples "
            "(Woodworth=%.2f, ±20%%)\n",
            name.c_str(), meas_m90, lo, hi, expected_itd);
        *failed = true;
        return false;
    }

    std::printf("OK   [%s] az±90 ITD within ±20%% of Woodworth (%.2f smp). Sign correct.\n",
                name.c_str(), expected_itd);
    return true;
}

// Minimal JSON catalog parser — extracts "name" and "speh_path" pairs.
// Only handles the flat catalog.json structure without a full JSON library.
std::vector<std::pair<std::string,std::string>> parseCatalog(const std::string& path) {
    std::vector<std::pair<std::string,std::string>> entries;
    std::ifstream f(path);
    if (!f.good()) return entries;

    std::string name, speh_path;
    std::string line;
    while (std::getline(f, line)) {
        auto extract = [&](const std::string& key) -> std::string {
            const std::string needle = "\"" + key + "\"";
            auto pos = line.find(needle);
            if (pos == std::string::npos) return {};
            pos = line.find(':', pos + needle.size());
            if (pos == std::string::npos) return {};
            pos = line.find('"', pos + 1);
            if (pos == std::string::npos) return {};
            auto end = line.find('"', pos + 1);
            if (end == std::string::npos) return {};
            return line.substr(pos + 1, end - pos - 1);
        };
        std::string n = extract("name");
        if (!n.empty()) name = n;
        std::string sp = extract("speh_path");
        if (!sp.empty()) speh_path = sp;

        // Emit entry when we see the closing brace of an object that has both fields.
        if (!name.empty() && !speh_path.empty() &&
            line.find('}') != std::string::npos) {
            entries.push_back({name, speh_path});
            name.clear();
            speh_path.clear();
        }
    }
    return entries;
}

} // namespace

int main() {
    bool failed   = false;
    int exercised = 0;

    // ── Always-present synthetic fixture (NOT skippable) ───────────────────
    const std::string synth_path =
        std::string(SPE_FIXTURES_DIR) + "/synthetic_itd_pm90.speh";

    if (check_dataset("synthetic_itd_pm90", synth_path, /*mandatory=*/true, &failed))
        ++exercised;
    if (failed) return 1;

    // ── Catalog datasets (skip-with-message if .speh absent) ───────────────
    const std::string catalog_path = SPE_CATALOG_PATH;
    auto entries = parseCatalog(catalog_path);
    if (entries.empty()) {
        std::fprintf(stderr,
            "WARN: catalog not found or empty at %s — only synthetic fixture exercised.\n",
            catalog_path.c_str());
    }

    // Resolve speh_path relative to the repo root (one level above core/build).
    // The path stored in catalog.json is "assets/hrtf/xxx.speh" — relative to
    // the workspace root. We derive the root from SPE_FIXTURES_DIR by going up
    // three levels: fixtures → tests → core → root.
    std::string fixtures_dir = SPE_FIXTURES_DIR;
    // Go up: fixtures_dir/../../.. = root
    auto parent = [](const std::string& p) -> std::string {
        auto pos = p.rfind('/');
        return pos == std::string::npos ? "." : p.substr(0, pos);
    };
    const std::string repo_root = parent(parent(parent(fixtures_dir)));

    for (auto& [name, rel_path] : entries) {
        std::string abs_path = repo_root + "/" + rel_path;
        if (check_dataset(name, abs_path, /*mandatory=*/false, &failed))
            ++exercised;
        if (failed) return 1;
    }

    // ── Anti-vacuity guard ─────────────────────────────────────────────────
    if (exercised == 0) {
        std::fprintf(stderr,
            "FAIL: zero datasets exercised — synthetic_itd_pm90.speh should always "
            "be present and non-skippable. Check fixture path: %s\n",
            synth_path.c_str());
        return 1;
    }

    std::printf(
        "OK  test_p_binaural_dataset_itd_oracle: %d dataset(s) exercised, "
        "all ITD oracles within ±20%% of Woodworth, sign correct.\n",
        exercised);
    return 0;
}

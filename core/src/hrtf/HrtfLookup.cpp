// core/src/hrtf/HrtfLookup.cpp

#include "hrtf/HrtfLookup.h"
#include <cmath>
#include <limits>

namespace spe::hrtf {

static constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;

int nearestPosition(const HrtfTable& table, float az_deg, float el_deg)
{
    const float az1 = az_deg * kDeg2Rad;
    const float el1 = el_deg * kDeg2Rad;
    const float sin_el1 = std::sin(el1);
    const float cos_el1 = std::cos(el1);

    float best_dot = -2.f;
    int   best_idx = 0;

    const uint32_t N = table.n_positions;
    const float*   p = table.positions.data();

    for (uint32_t i = 0; i < N; ++i) {
        const float az2     = p[i * 3 + 0] * kDeg2Rad;
        const float el2     = p[i * 3 + 1] * kDeg2Rad;
        const float cos_d   = sin_el1 * std::sin(el2) +
                              cos_el1 * std::cos(el2) * std::cos(az1 - az2);
        if (cos_d > best_dot) {
            best_dot = cos_d;
            best_idx = static_cast<int>(i);
        }
    }
    return best_idx;
}

HrtfPair lookupHrtf(const HrtfTable& table, float az_rad, float el_rad)
{
    const float az_deg = az_rad * (180.f / 3.14159265358979323846f);
    const float el_deg = el_rad * (180.f / 3.14159265358979323846f);
    const int   idx    = nearestPosition(table, az_deg, el_deg);
    return {table.ir(idx, 0), table.ir(idx, 1),
            static_cast<int>(table.ir_length)};
}

} // namespace spe::hrtf

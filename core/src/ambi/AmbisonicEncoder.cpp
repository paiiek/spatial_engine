#include "AmbisonicEncoder.h"
#include <cmath>

namespace spe::ambi {

AmbiCoeffs1st AmbisonicEncoder::encode_1st_order(float az_rad, float el_rad) noexcept {
    // W = 1.0 (SN3D-consistent, not FuMa 1/√2)
    // Engine az convention: az=0 → +z(front), az=π/2 → +x(right)
    // Source unit vector: s = (cos(el)*sin(az), sin(el), cos(el)*cos(az))
    // X=front=cos(el)*cos(az), Y=right=cos(el)*sin(az), Z=up=sin(el)
    const double az = az_rad;
    const double el = el_rad;
    const double cos_el = std::cos(el);
    return {
        1.0f,
        static_cast<float>(cos_el * std::cos(az)),   // X = front
        static_cast<float>(cos_el * std::sin(az)),   // Y = right
        static_cast<float>(std::sin(el))             // Z = up
    };
}

} // namespace spe::ambi

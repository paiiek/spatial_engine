#pragma once
#include <array>
#include <cmath>

namespace spe::ambi {

struct AmbiCoeffs1st { float W, X, Y, Z; };

class AmbisonicEncoder {
public:
    // ACN ordering, W=1.0 (SN3D-consistent default; decoder convention deferred to v2)
    // az_rad: engine convention atan2(x,z) — az=0 → +z front, az=π/2 → +x right
    // el_rad: elevation, el=0 horizontal, el=+π/2 directly above
    static AmbiCoeffs1st encode_1st_order(float az_rad, float el_rad) noexcept;
};

} // namespace spe::ambi

#pragma once
#include <array>
#include <cmath>

namespace spe::ambi {

struct AmbiCoeffs1st { float W, X, Y, Z; };

// 9 channels, ACN order 0..8, SN3D normalisation:
//   [0]=W(Y_0^0), [1]=Y(Y_1^-1), [2]=Z(Y_1^0), [3]=X(Y_1^1),
//   [4]=Y_2^-2, [5]=Y_2^-1, [6]=Y_2^0, [7]=Y_2^1, [8]=Y_2^2
// NOTE: ACN index order differs from legacy AmbiCoeffs1st field order {W,X,Y,Z}.
// Mapping: arr[1]↔legacy.Y, arr[2]↔legacy.Z, arr[3]↔legacy.X.
using AmbiCoeffs2nd = std::array<float, 9>;

// 16 channels, ACN order 0..15, SN3D normalisation
using AmbiCoeffs3rd = std::array<float, 16>;

class AmbisonicEncoder {
public:
    // ACN ordering, W=1.0 (SN3D-consistent default; decoder convention deferred to v2)
    // az_rad: engine convention atan2(x,z) — az=0 → +z front, az=π/2 → +x right
    // el_rad: elevation, el=0 horizontal, el=+π/2 directly above
    static AmbiCoeffs1st encode_1st_order(float az_rad, float el_rad) noexcept;
    static AmbiCoeffs2nd encode_2nd_order(float az_rad, float el_rad) noexcept;
    static AmbiCoeffs3rd encode_3rd_order(float az_rad, float el_rad) noexcept;
};

} // namespace spe::ambi

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

AmbiCoeffs2nd AmbisonicEncoder::encode_2nd_order(float az_rad, float el_rad) noexcept {
    const double az = static_cast<double>(az_rad);
    const double el = static_cast<double>(el_rad);
    const double sin_az = std::sin(az), cos_az = std::cos(az);
    const double sin_el = std::sin(el), cos_el = std::cos(el);
    const double cos_el2 = cos_el * cos_el;
    const double sin_el2 = sin_el * sin_el;
    const double sin_2az = 2.0 * sin_az * cos_az;
    const double cos_2az = cos_az * cos_az - sin_az * sin_az;
    constexpr double kSqrt3 = 1.7320508075688772;
    constexpr double kSqrt3_2 = kSqrt3 / 2.0;

    AmbiCoeffs2nd c{};
    // ACN 0..3: same as 1st order (SN3D, ACN order)
    c[0] = 1.f;
    c[1] = static_cast<float>(cos_el * sin_az);       // Y_1^-1 (legacy Y)
    c[2] = static_cast<float>(sin_el);                 // Y_1^0  (legacy Z)
    c[3] = static_cast<float>(cos_el * cos_az);        // Y_1^1  (legacy X)
    // ACN 4..8: 2nd order
    c[4] = static_cast<float>(kSqrt3 * sin_2az * cos_el2);
    c[5] = static_cast<float>(kSqrt3 * sin_az * sin_el * cos_el);
    c[6] = static_cast<float>(0.5 * (3.0 * sin_el2 - 1.0));
    c[7] = static_cast<float>(kSqrt3 * cos_az * sin_el * cos_el);
    c[8] = static_cast<float>(kSqrt3_2 * cos_2az * cos_el2);
    return c;
}

AmbiCoeffs3rd AmbisonicEncoder::encode_3rd_order(float az_rad, float el_rad) noexcept {
    const double az = static_cast<double>(az_rad);
    const double el = static_cast<double>(el_rad);
    const double sin_az = std::sin(az), cos_az = std::cos(az);
    const double sin_el = std::sin(el), cos_el = std::cos(el);
    const double cos_el2 = cos_el * cos_el;
    const double sin_el2 = sin_el * sin_el;
    const double sin_2az = 2.0 * sin_az * cos_az;
    const double cos_2az = cos_az * cos_az - sin_az * sin_az;
    const double sin_3az = sin_az * (3.0 - 4.0 * sin_az * sin_az);  // sin(3x)=3sin(x)-4sin^3(x)
    const double cos_3az = cos_az * (4.0 * cos_az * cos_az - 3.0);  // cos(3x)=4cos^3(x)-3cos(x)
    constexpr double kSqrt3   = 1.7320508075688772;
    constexpr double kSqrt3_2 = kSqrt3 / 2.0;
    constexpr double kSqrt6_4 = 0.6123724356957945;   // sqrt(6)/4
    constexpr double kSqrt10_4 = 0.7905694150420949;  // sqrt(10)/4
    constexpr double kSqrt15_2 = 1.9364916731037085;  // sqrt(15)/2

    AmbiCoeffs3rd c{};
    // ACN 0..3: same as 1st order
    c[0] = 1.f;
    c[1] = static_cast<float>(cos_el * sin_az);
    c[2] = static_cast<float>(sin_el);
    c[3] = static_cast<float>(cos_el * cos_az);
    // ACN 4..8: same as 2nd order
    c[4] = static_cast<float>(kSqrt3 * sin_2az * cos_el2);
    c[5] = static_cast<float>(kSqrt3 * sin_az * sin_el * cos_el);
    c[6] = static_cast<float>(0.5 * (3.0 * sin_el2 - 1.0));
    c[7] = static_cast<float>(kSqrt3 * cos_az * sin_el * cos_el);
    c[8] = static_cast<float>(kSqrt3_2 * cos_2az * cos_el2);
    // ACN 9..15: 3rd order
    c[9]  = static_cast<float>(kSqrt10_4 * sin_3az * cos_el2 * cos_el);
    c[10] = static_cast<float>(kSqrt15_2 * sin_2az * sin_el * cos_el2);
    c[11] = static_cast<float>(kSqrt6_4  * sin_az * (5.0*sin_el2 - 1.0) * cos_el);
    c[12] = static_cast<float>(0.5 * (5.0*sin_el2*sin_el - 3.0*sin_el));
    c[13] = static_cast<float>(kSqrt6_4  * cos_az * (5.0*sin_el2 - 1.0) * cos_el);
    c[14] = static_cast<float>(kSqrt15_2 * cos_2az * sin_el * cos_el2);
    c[15] = static_cast<float>(kSqrt10_4 * cos_3az * cos_el2 * cos_el);
    return c;
}

} // namespace spe::ambi

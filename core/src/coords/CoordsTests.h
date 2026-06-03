// core/src/coords/CoordsTests.h — hand-computed expected-value lookup table
// consumed by test_p2_coords.cpp. All expected values derived by hand; tolerance 1e-6.

#pragma once

#include <cmath>

namespace spe::coords::tests {

// M_PI may not be defined under strict standards. Define locally.
static constexpr float kPi  = 3.14159265358979323846f;
static constexpr float kPi2 = kPi / 2.0f;
static constexpr float kPi4 = kPi / 4.0f;

struct PairCase {
    float in_a, in_b;
    float exp_a, exp_b;
};

struct TupleCase {
    float in_x, in_y, in_z;
    float exp_az, exp_el, exp_dist;
};

struct ScalarCase {
    float in;
    float exp;
};

struct CartCase {
    float az_deg, el_deg, dist_m;
    float exp_x, exp_y, exp_z;
};

// Dreamscape Convergence: pipeline (az,el) -> ported-frame unit vector.
// ported = (cosEl*sinAz, cosEl*cosAz, sinEl). +X=right, +Y=front, +Z=up.
struct DirCase {
    float az_rad, el_rad;
    float exp_x, exp_y, exp_z;
};

inline const DirCase kPipelineDirToPorted[] = {
    {  0.0f,   0.0f,   0.0f,          1.0f,          0.0f       },  // front
    {  kPi2,   0.0f,   1.0f,          0.0f,          0.0f       },  // RIGHT -> +X
    { -kPi2,   0.0f,  -1.0f,          0.0f,          0.0f       },  // LEFT  -> -X
    {  0.0f,   kPi2,   0.0f,          0.0f,          1.0f       },  // UP    -> +Z
    {  kPi4,   0.0f,   0.70710678f,   0.70710678f,   0.0f       },  // front-right
};

// pipeline_to_ambix cases
inline constexpr PairCase kPipeToAmbix[] = {
    // (az, el) -> (-az, el)
    {  kPi4,    0.0f,    -kPi4,   0.0f  },  // right 45° -> left 45°
    {  0.0f,    kPi2,     0.0f,   kPi2  },  // el only passthrough
    { -kPi4,    kPi4,     kPi4,   kPi4  },  // left az negates
    {  kPi2,   -kPi4,    -kPi2,  -kPi4  },  // compound
};

// ambix_to_pipeline cases
inline constexpr PairCase kAmbixToPipe[] = {
    {  kPi4,   0.0f,   -kPi4,   0.0f  },  // symmetric inversion
    {  0.0f,   kPi2,    0.0f,   kPi2  },
    { -kPi2,   0.0f,    kPi2,   0.0f  },
};

// cartesian_to_pipeline cases — hand-computed
// cartesian_to_pipeline(1,0,1): az=atan2(1,1)=pi/4, el=0, dist=sqrt(2)
// cartesian_to_pipeline(-1,0,1): az=atan2(-1,1)=-pi/4, el=0, dist=sqrt(2)
// cartesian_to_pipeline(0,1,0): az=atan2(0,0)=0, el=atan2(1,0)=pi/2, dist=1
// cartesian_to_pipeline(0,-1,0): az=0, el=atan2(-1,0)=-pi/2, dist=1
// cartesian_to_pipeline(1,1,0): az=atan2(1,0)=pi/2, el=atan2(1,1)=pi/4, dist=sqrt(2)
// cartesian_to_pipeline(0,0,1): az=0, el=0, dist=1
// cartesian_to_pipeline(-1,0,-1): az=atan2(-1,-1)=-3pi/4, el=0, dist=sqrt(2)
inline const TupleCase kCartToPipe[] = {
    {  1.0f,  0.0f,  1.0f,   kPi4,        0.0f,          1.41421356f },
    { -1.0f,  0.0f,  1.0f,  -kPi4,        0.0f,          1.41421356f },
    {  0.0f,  1.0f,  0.0f,   0.0f,        kPi2,          1.0f        },
    {  0.0f, -1.0f,  0.0f,   0.0f,       -kPi2,          1.0f        },
    {  0.0f,  0.0f,  1.0f,   0.0f,        0.0f,          1.0f        },
    {  1.0f,  1.0f,  0.0f,   kPi2,        kPi4,          1.41421356f },
};

// image_y_to_listener_el: el = arcsin(-y)
// y=+0.5 -> arcsin(-0.5) = -pi/6
// y=0    -> 0
// y=-1   -> arcsin(1) = pi/2
// y=+1   -> arcsin(-1) = -pi/2
// y=-0.5 -> arcsin(0.5) = pi/6
inline const ScalarCase kImageYToEl[] = {
    {  0.5f,  -kPi / 6.0f     },  // arcsin(-0.5) = -pi/6
    {  0.0f,   0.0f            },
    { -1.0f,   kPi2            },  // arcsin(1) = pi/2
    {  1.0f,  -kPi2            },  // arcsin(-1) = -pi/2
    { -0.5f,   kPi / 6.0f     },  // arcsin(0.5) = pi/6
};

// yaml_speaker_to_cartesian cases — hand-computed
// az=90,el=0,d=1: x=cos(0)*sin(pi/2)=1, y=0, z=cos(0)*cos(pi/2)=0
// az=0, el=0,d=1: x=0, y=0, z=1
// az=180,el=0,d=1: x=cos(0)*sin(pi)=0, y=0, z=cos(0)*cos(pi)=-1
// az=0, el=90,d=1: x=cos(pi/2)*sin(0)=0, y=sin(pi/2)=1, z=0
// az=-90,el=0,d=1: x=cos(0)*sin(-pi/2)=-1, y=0, z=0
// az=0, el=-90,d=1: x=0, y=-1, z=0
// az=45,el=0,d=1: x=sin(pi/4)=sqrt(2)/2, y=0, z=cos(pi/4)=sqrt(2)/2
inline const CartCase kYamlSpeakerToCart[] = {
    {  90.0f,   0.0f,  1.0f,   1.0f,       0.0f,   0.0f       },
    {   0.0f,   0.0f,  1.0f,   0.0f,       0.0f,   1.0f       },
    { 180.0f,   0.0f,  1.0f,   0.0f,       0.0f,  -1.0f       },
    {   0.0f,  90.0f,  1.0f,   0.0f,       1.0f,   0.0f       },
    { -90.0f,   0.0f,  1.0f,  -1.0f,       0.0f,   0.0f       },
    {   0.0f, -90.0f,  1.0f,   0.0f,      -1.0f,   0.0f       },
    {  45.0f,   0.0f,  1.0f,   0.70710678f, 0.0f,  0.70710678f},
};

}  // namespace spe::coords::tests

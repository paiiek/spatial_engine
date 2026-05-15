// core/src/hrtf/KdTree3D.cpp

#include "hrtf/KdTree3D.h"

#include <algorithm>
#include <cmath>

namespace spe::hrtf {

static constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;

static inline void azElToXYZ(float az_rad, float el_rad,
                             float& x, float& y, float& z) noexcept
{
    const float cel = std::cos(el_rad);
    x = cel * std::cos(az_rad);
    y = std::sin(el_rad);
    z = cel * std::sin(az_rad);
}

void KdTree3D::build(const HrtfTable& table)
{
    nodes_.clear();
    points_.clear();

    const uint32_t N = table.n_positions;
    if (N == 0) return;

    points_.reserve(N);
    const float* p = table.positions.data();
    for (uint32_t i = 0; i < N; ++i) {
        // positions are stored as (az_deg, el_deg, dist_m)
        const float az = p[i * 3 + 0] * kDeg2Rad;
        const float el = p[i * 3 + 1] * kDeg2Rad;
        Point pt;
        pt.index = static_cast<int>(i);
        azElToXYZ(az, el, pt.x, pt.y, pt.z);
        points_.push_back(pt);
    }

    nodes_.reserve(N);
    (void)buildRecursive(0, static_cast<int>(N), /*depth=*/0);

    // points_ no longer needed.
    points_.clear();
    points_.shrink_to_fit();
}

int KdTree3D::buildRecursive(int lo, int hi, int depth)
{
    if (lo >= hi) return -1;

    const int axis = depth % 3;
    auto cmp = [axis](const Point& a, const Point& b) noexcept {
        switch (axis) {
        case 0: return a.x < b.x;
        case 1: return a.y < b.y;
        default: return a.z < b.z;
        }
    };

    const int mid = lo + (hi - lo) / 2;
    std::nth_element(points_.begin() + lo,
                     points_.begin() + mid,
                     points_.begin() + hi, cmp);

    // Allocate node.
    Node node{};
    node.x     = points_[static_cast<std::size_t>(mid)].x;
    node.y     = points_[static_cast<std::size_t>(mid)].y;
    node.z     = points_[static_cast<std::size_t>(mid)].z;
    node.index = points_[static_cast<std::size_t>(mid)].index;
    node.axis  = axis;
    node.left  = -1;
    node.right = -1;
    const int slot = static_cast<int>(nodes_.size());
    nodes_.push_back(node);

    // Recurse — write children indices back through slot reference.
    const int l = buildRecursive(lo, mid, depth + 1);
    const int r = buildRecursive(mid + 1, hi, depth + 1);
    nodes_[static_cast<std::size_t>(slot)].left  = l;
    nodes_[static_cast<std::size_t>(slot)].right = r;
    return slot;
}

int KdTree3D::nearest(float az_rad, float el_rad) const noexcept
{
    if (nodes_.empty()) return 0;

    float qx, qy, qz;
    azElToXYZ(az_rad, el_rad, qx, qy, qz);

    // Iterative DFS with fixed-size stack. log2(200000) ~= 17, padded to 64
    // gives generous headroom for unbalanced fallback (still alloc-free).
    constexpr int kStackDepth = 64;
    int stack[kStackDepth];
    int sp = 0;
    stack[sp++] = 0;  // root

    int   best_idx  = nodes_[0].index;
    float best_dist = (qx - nodes_[0].x) * (qx - nodes_[0].x)
                    + (qy - nodes_[0].y) * (qy - nodes_[0].y)
                    + (qz - nodes_[0].z) * (qz - nodes_[0].z);

    while (sp > 0) {
        const int ni = stack[--sp];
        if (ni < 0) continue;
        const Node& n = nodes_[static_cast<std::size_t>(ni)];

        const float dx = qx - n.x;
        const float dy = qy - n.y;
        const float dz = qz - n.z;
        const float d  = dx * dx + dy * dy + dz * dz;
        if (d < best_dist) {
            best_dist = d;
            best_idx  = n.index;
        }

        // Plane distance for the split axis.
        float diff;
        switch (n.axis) {
        case 0:  diff = qx - n.x; break;
        case 1:  diff = qy - n.y; break;
        default: diff = qz - n.z; break;
        }

        const int near_child = (diff < 0.f) ? n.left : n.right;
        const int far_child  = (diff < 0.f) ? n.right : n.left;

        // Visit FAR last so NEAR is popped first (DFS order biases NEAR exploration).
        // Push FAR only if the splitting hyperplane could yield a closer point.
        if (far_child >= 0 && diff * diff < best_dist) {
            if (sp < kStackDepth) stack[sp++] = far_child;
        }
        if (near_child >= 0) {
            if (sp < kStackDepth) stack[sp++] = near_child;
        }
    }

    return best_idx;
}

} // namespace spe::hrtf

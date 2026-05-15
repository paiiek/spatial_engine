// core/src/hrtf/KdTree3D.h
// 3D KD-tree on unit-Cartesian HRTF positions for O(log N) nearest-neighbor lookup.
//
// v0.5 P2: replaces the O(N) trig brute-force in HrtfLookup::nearestPosition().
//
// Build:   O(N log N), allocations OK — control thread, called at .speh load.
// Query:   O(log N) expected, no trig, fully alloc-free with a fixed-size
//          iterative traversal stack (depth bound = log2(MAX_POINTS) + 32).
//
// Coordinate convention: query is (az_rad, el_rad) in engine frame; internally
// the tree stores unit-Cartesian (x, y, z) = (cos(el)*cos(az), sin(el),
// cos(el)*sin(az)). Nearest-neighbor minimises squared Euclidean distance,
// which on the unit sphere is monotone with great-circle distance.

#pragma once

#include "hrtf/SofaBinReader.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spe::hrtf {

class KdTree3D {
public:
    KdTree3D() = default;

    // Build tree from HrtfTable. Allocates; control thread only.
    // Idempotent — calling build() twice rebuilds from scratch.
    void build(const HrtfTable& table);

    // Returns true if the tree is non-empty.
    bool isBuilt() const noexcept { return !nodes_.empty(); }

    // Query: nearest-neighbor on the unit sphere.
    // (az_rad, el_rad) in engine frame; returns the index of the closest
    // position in the source HrtfTable. Alloc-free, no trig in the inner
    // traversal loop (trig only on the query coordinate at entry).
    //
    // Returns 0 on empty tree (caller must check isBuilt()).
    int nearest(float az_rad, float el_rad) const noexcept;

    // Number of points stored.
    int size() const noexcept { return static_cast<int>(nodes_.size()); }

private:
    struct Node {
        float x, y, z;       // unit-Cartesian
        int   index;         // index into source HrtfTable
        int   left;          // index into nodes_ or -1
        int   right;         // index into nodes_ or -1
        int   axis;          // splitting axis: 0=x, 1=y, 2=z
    };

    // Build recursively over the slice points_[lo..hi). Returns the index in
    // nodes_ of the chosen median (or -1 if range empty).
    int buildRecursive(int lo, int hi, int depth);

    std::vector<Node> nodes_;

    // Scratch used only during build(): copy of (x,y,z,idx) reordered in-place.
    // Cleared after build to free memory.
    struct Point { float x, y, z; int index; };
    std::vector<Point> points_;
};

} // namespace spe::hrtf

#ifndef slic3r_AlignmentSnap_hpp_
#define slic3r_AlignmentSnap_hpp_

#include <vector>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace AlignmentSnap {

struct SnapSettings {
    bool   enabled             = false;
    double sensitivity_px      = 8.0;
    double strength_px         = 6.0;
    bool   edge_align          = true;
    bool   center_align        = true;
    bool   contact             = true;
    bool   spacing_propagation = true;
};

struct Neighbor { BoundingBoxf bbox; int id = -1; };

enum class Axis { X = 0, Y = 1 };

struct GuideLine    { Axis axis; double coord; double span_lo; double span_hi; };
struct SpacingBadge { Vec2d a; Vec2d b; double value_mm; };
struct GhostBox     { BoundingBoxf bbox; };

struct SnapResult {
    Vec2d corrected_delta { 0.0, 0.0 };
    std::vector<GuideLine>    lines;
    std::vector<SpacingBadge> badges;
    std::vector<GhostBox>     ghosts;
    bool engaged[2] = { false, false };
};

struct SnapState { bool engaged[2] = { false, false }; double coord[2] = { 0.0, 0.0 }; };

// Returns raw_delta corrected so the moved bbox snaps to targets, plus guides to draw.
SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state);

}} // namespace Slic3r::AlignmentSnap
#endif

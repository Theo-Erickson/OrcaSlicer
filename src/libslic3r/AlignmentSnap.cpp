#include "libslic3r/AlignmentSnap.hpp"

namespace Slic3r { namespace AlignmentSnap {

SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state)
{
    SnapResult r;
    r.corrected_delta = raw_delta;
    (void)mover_start; (void)targets; (void)s; (void)px_per_mm; (void)state;
    return r;
}

}} // namespace Slic3r::AlignmentSnap

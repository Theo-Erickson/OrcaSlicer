#include "libslic3r/AlignmentSnap.hpp"
#include <cmath>
#include <algorithm>

namespace Slic3r { namespace AlignmentSnap {

namespace {
    struct Cand { double correction; double dist_px; };
    inline double lo (const BoundingBoxf& b, int a){ return a == 0 ? b.min.x() : b.min.y(); }
    inline double hi (const BoundingBoxf& b, int a){ return a == 0 ? b.max.x() : b.max.y(); }
    inline double ctr(const BoundingBoxf& b, int a){ return a == 0 ? b.center().x() : b.center().y(); }

    // Do the two boxes overlap along the axis perpendicular to `axis`?
    inline bool overlaps_perp(const BoundingBoxf& m, const BoundingBoxf& t, int axis) {
        int p = axis ^ 1;
        return lo(m, p) < hi(t, p) && lo(t, p) < hi(m, p);
    }
}

SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state)
{
    SnapResult r;
    r.corrected_delta = raw_delta;
    if (!s.enabled || targets.empty() || px_per_mm <= 0.0)
        return r;

    // Mover box at the raw (unsnapped) position.
    BoundingBoxf mover = mover_start;
    mover.min += raw_delta;
    mover.max += raw_delta;

    // X and Y are resolved independently.
    for (int a = 0; a < 2; ++a) {
        const double mover_lo = lo(mover, a);
        const double mover_hi = hi(mover, a);
        const double mover_c  = ctr(mover, a);

        // If this axis was already engaged, honour the wider break-away window first (hysteresis).
        if (state.engaged[a]) {
            const double d = std::min({ std::abs(state.coord[a] - mover_lo),
                                        std::abs(state.coord[a] - mover_hi),
                                        std::abs(state.coord[a] - mover_c) });
            if (d * px_per_mm <= s.sensitivity_px + s.strength_px) {
                // Stay snapped: correct whichever mover key sits closest to the engaged line.
                const double keys[3] = { mover_lo, mover_hi, mover_c };
                double bestc = state.coord[a] - keys[0];
                for (double k : keys)
                    if (std::abs(state.coord[a] - k) < std::abs(bestc))
                        bestc = state.coord[a] - k;
                if (a == 0) r.corrected_delta.x() += bestc; else r.corrected_delta.y() += bestc;
                r.engaged[a] = true;
                continue;
            }
            state.engaged[a] = false; // drifted past the margin -> fresh search
        }

        Cand   best { 0.0, s.sensitivity_px + 1.0 };
        bool   found = false;
        double engaged_line = 0.0;
        for (const Neighbor& n : targets) {
            auto consider = [&](double mover_key, double target_key) {
                const double correction = target_key - mover_key;   // mm to add on this axis
                const double dpx        = std::abs(correction) * px_per_mm;
                if (dpx <= s.sensitivity_px && dpx < best.dist_px) {
                    best = { correction, dpx };
                    found = true;
                    engaged_line = target_key;
                }
            };
            if (s.edge_align) {
                consider(mover_lo, lo(n.bbox, a)); // min <-> min
                consider(mover_hi, hi(n.bbox, a)); // max <-> max
            }
            if (s.center_align) {
                consider(mover_c, ctr(n.bbox, a)); // center <-> center
            }
            if (s.contact && overlaps_perp(mover, n.bbox, a)) {
                consider(mover_hi, lo(n.bbox, a)); // mover max touches target min
                consider(mover_lo, hi(n.bbox, a)); // mover min touches target max
            }
        }
        if (found) {
            if (a == 0) r.corrected_delta.x() += best.correction; else r.corrected_delta.y() += best.correction;
            r.engaged[a]     = true;
            state.engaged[a] = true;
            state.coord[a]   = engaged_line;
        }
    }

    return r;
}

}} // namespace Slic3r::AlignmentSnap

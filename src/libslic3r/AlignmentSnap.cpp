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

    // Are two boxes aligned along the axis perpendicular to `axis` (share an edge or center)?
    inline bool perp_aligned(const BoundingBoxf& x, const BoundingBoxf& y, int axis, double tol) {
        int p = axis ^ 1;
        return std::abs(lo(x, p)  - lo(y, p))  <= tol
            || std::abs(ctr(x, p) - ctr(y, p)) <= tol
            || std::abs(hi(x, p)  - hi(y, p))  <= tol;
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

    const double thresh_mm = s.sensitivity_px / px_per_mm;

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

    // Spacing propagation: continue an existing evenly spaced, aligned row.
    if (s.spacing_propagation) {
        for (int a = 0; a < 2; ++a) {
            if (r.engaged[a]) continue; // don't override a hard alignment

            std::vector<const Neighbor*> row;
            row.reserve(targets.size());
            for (const Neighbor& n : targets) row.push_back(&n);
            std::sort(row.begin(), row.end(), [&](const Neighbor* p, const Neighbor* q) {
                return ctr(p->bbox, a) < ctr(q->bbox, a);
            });

            for (size_t i = 0; i + 1 < row.size(); ++i) {
                if (!perp_aligned(row[i]->bbox, row[i + 1]->bbox, a, thresh_mm)) continue;
                const double spacing = ctr(row[i + 1]->bbox, a) - ctr(row[i]->bbox, a);
                if (spacing <= thresh_mm) continue;

                const double slot    = ctr(row[i + 1]->bbox, a) + spacing; // next slot beyond the row
                const double mover_c = (a == 0 ? mover.center().x() : mover.center().y());
                if (std::abs(slot - mover_c) * px_per_mm <= s.sensitivity_px) {
                    const double correction = slot - mover_c;
                    if (a == 0) r.corrected_delta.x() += correction; else r.corrected_delta.y() += correction;
                    r.engaged[a]     = true;
                    state.engaged[a] = true;
                    state.coord[a]   = slot;

                    // Badges: existing gap, then the new gap being created.
                    if (a == 0) {
                        const double y = ctr(row[i]->bbox, 1);
                        r.badges.push_back(SpacingBadge{ Vec2d(ctr(row[i]->bbox, 0), y),     Vec2d(ctr(row[i + 1]->bbox, 0), y), spacing });
                        r.badges.push_back(SpacingBadge{ Vec2d(ctr(row[i + 1]->bbox, 0), y), Vec2d(slot, y),                     spacing });
                    } else {
                        const double x = ctr(row[i]->bbox, 0);
                        r.badges.push_back(SpacingBadge{ Vec2d(x, ctr(row[i]->bbox, 1)),     Vec2d(x, ctr(row[i + 1]->bbox, 1)), spacing });
                        r.badges.push_back(SpacingBadge{ Vec2d(x, ctr(row[i + 1]->bbox, 1)), Vec2d(x, slot),                     spacing });
                    }
                    break;
                }
            }
        }
    }

    // Guides: ghost boxes for nearby neighbors + alignment lines for engaged axes.
    BoundingBoxf mv = mover_start;
    mv.min += r.corrected_delta;
    mv.max += r.corrected_delta;

    const double reveal_mm = 3.0 * thresh_mm;
    for (const Neighbor& n : targets) {
        const double dx = std::max({ 0.0, n.bbox.min.x() - mv.max.x(), mv.min.x() - n.bbox.max.x() });
        const double dy = std::max({ 0.0, n.bbox.min.y() - mv.max.y(), mv.min.y() - n.bbox.max.y() });
        if (std::sqrt(dx * dx + dy * dy) <= reveal_mm)
            r.ghosts.push_back(GhostBox{ n.bbox });
    }

    for (int a = 0; a < 2; ++a) {
        if (!r.engaged[a]) continue;
        const int p = a ^ 1;
        double span_lo = (p == 0 ? mv.min.x() : mv.min.y());
        double span_hi = (p == 0 ? mv.max.x() : mv.max.y());
        for (const Neighbor& n : targets) {
            span_lo = std::min(span_lo, (p == 0 ? n.bbox.min.x() : n.bbox.min.y()));
            span_hi = std::max(span_hi, (p == 0 ? n.bbox.max.x() : n.bbox.max.y()));
        }
        r.lines.push_back(GuideLine{ a == 0 ? Axis::X : Axis::Y, state.coord[a], span_lo, span_hi });
    }

    return r;
}

}} // namespace Slic3r::AlignmentSnap

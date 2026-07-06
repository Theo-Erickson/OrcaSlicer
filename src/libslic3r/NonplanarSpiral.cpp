// NonplanarSpiral.cpp — see NonplanarSpiral.hpp for the module overview.

#include "NonplanarSpiral.hpp"

#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

// Per-layer summary used by the region scan.
struct LayerMetric {
    bool   circular  = false;  // dominant island is approximately circular
    double radius_mm = 0.0;    // equivalent-area radius of the dominant island
    Vec2d  center_mm = Vec2d::Zero();
};

// A perfect circle has 4*pi*Area/Perimeter^2 == 1. A square is ~0.785. This threshold
// accepts domes/spheres/cones while rejecting boxes and other faceted tops.
constexpr double CIRCULARITY_THRESHOLD = 0.85;

// Summarize one layer's dominant (largest-area) island.
LayerMetric metric_for_layer(const NonplanarLayerSlice& layer)
{
    LayerMetric m;
    const ExPolygon* dominant = nullptr;
    double           best_area = 0.0;  // scaled^2
    for (const ExPolygon& ex : layer.islands) {
        const double a = ex.area();
        if (a > best_area) { best_area = a; dominant = &ex; }
    }
    if (dominant == nullptr || best_area <= 0.0)
        return m;

    const Polygon& contour   = dominant->contour;
    const double   perimeter = contour.length();  // scaled
    // Circularity is dimensionless, so the scaled units cancel — no unscaling needed here.
    const double   circularity =
        (perimeter > 0.0) ? (4.0 * M_PI * best_area / (perimeter * perimeter)) : 0.0;

    m.circular  = circularity >= CIRCULARITY_THRESHOLD;
    // Equivalent-area radius: area = pi * r^2. Unscale the scaled radius to mm.
    m.radius_mm = unscale<double>(std::sqrt(best_area / M_PI));
    m.center_mm = unscaled<double>(contour.centroid());
    return m;
}

} // namespace

std::optional<NonplanarTopRegion> detect_nonplanar_region(
    const std::vector<NonplanarLayerSlice>& layers,
    const NonplanarRegionOverride&          ovr,
    int                                     max_top_layers)
{
    const int n = static_cast<int>(layers.size());
    if (n < 3)
        return std::nullopt;

    std::vector<LayerMetric> metric(n);
    for (int i = 0; i < n; ++i)
        metric[i] = metric_for_layer(layers[i]);

    int first = -1;
    int last  = -1;

    // Explicit layer-range override short-circuits the scan.
    if (ovr.enabled && ovr.first_layer && ovr.last_layer) {
        first = std::clamp(*ovr.first_layer, 0, n - 1);
        last  = std::clamp(*ovr.last_layer, 0, n - 1);
        if (last <= first)
            return std::nullopt;
    } else {
        // Auto-detect: walk down from the topmost circular layer while the surface stays
        // circular, concentric, and keeps widening. Widening is measured against a layer a
        // few steps above (WIDEN_LOOK) so slow growth near the near-flat apex is not mistaken
        // for the constant radius of a cylinder wall below a domed top.
        constexpr int    WIDEN_LOOK   = 3;      // layers to look up when measuring growth
        constexpr double WIDEN_TOL_MM = 0.02;   // min radius growth to count as "still widening"

        int top = n - 1;
        while (top >= 0 && !metric[top].circular)
            --top;
        if (top < 0)
            return std::nullopt;

        last = top;
        const Vec2d center = metric[top].center_mm;
        first = top;
        for (int i = top - 1; i >= 0; --i) {
            if (!metric[i].circular)
                break;
            // Concentricity guard: reject a layer whose center drifts relative to its size.
            if ((metric[i].center_mm - center).norm() > 0.2 * metric[i].radius_mm + EPSILON)
                break;
            const int ref = std::min(top, i + WIDEN_LOOK);
            if (metric[i].radius_mm <= metric[ref].radius_mm + WIDEN_TOL_MM)
                break;  // stopped widening → base of the dome (or start of a cylinder)
            first = i;
        }

        if (last - first < 2)   // detected cap must be at least 3 layers to be meaningful
            return std::nullopt;

        // Restrict the spiral to the top N layers if requested, so it forms a thin skin
        // over a normally-filled body rather than replacing the whole cap (a hollow shell).
        if (max_top_layers > 0 && (last - first + 1) > max_top_layers)
            first = last - max_top_layers + 1;
    }

    NonplanarTopRegion region;
    region.first_layer = first;
    region.last_layer  = last;
    region.base_z      = layers[first].print_z;
    region.apex_z      = layers[last].print_z;
    region.center      = ovr.enabled && ovr.center_mm ? *ovr.center_mm : metric[first].center_mm;
    region.base_radius = ovr.enabled && ovr.base_radius_mm ? *ovr.base_radius_mm : metric[first].radius_mm;

    if (region.base_radius <= 0.0)
        return std::nullopt;

    return region;
}

std::vector<Vec3d> generate_spiral(
    const NonplanarTopRegion&    region,
    const NonplanarSpiralParams& params,
    const SurfaceZFn&            surface_z)
{
    std::vector<Vec3d> pts;
    if (!region.valid() || region.base_radius <= 0.0 ||
        params.line_width <= 0.0 || params.points_per_rev < 3)
        return pts;

    const double d_theta     = 2.0 * M_PI / params.points_per_rev;
    const double trans_angle = std::max(0.0, params.transition_revs) * 2.0 * M_PI;

    // The radial pitch is adapted per revolution so the SURFACE spacing between consecutive
    // passes stays ~one line width even on slopes: on a surface tilted by angle s, a radial
    // step dr covers dr/cos(s) along the surface, so we advance radially by line_width*cos(s).
    // A floor keeps near-vertical walls (which can't be tiled nonplanar-ly) from stalling it.
    const double min_pitch_per_rev = params.line_width * 0.15;
    const double probe_dr          = std::max(0.05, params.line_width);   // mm, slope probe
    const double cos_max_slope     = std::cos(std::clamp(params.max_slope_deg, 0.0, 90.0)
                                              * M_PI / 180.0);

    const size_t max_points = 4'000'000;

    const auto point_at = [&](double r, double theta) {
        return Vec2d(region.center.x() + r * std::cos(theta),
                     region.center.y() + r * std::sin(theta));
    };

    double r     = region.base_radius;
    double theta = 0.0;
    while (r > 0.0 && pts.size() < max_points) {
        const Vec2d                 xy   = point_at(r, theta);
        const std::optional<double> surf = surface_z(xy);

        // Estimate the local surface slope by probing one line width further in.
        double cos_slope = 1.0;
        if (surf) {
            const double                r2 = std::max(0.0, r - probe_dr);
            const std::optional<double> s2 = surface_z(point_at(r2, theta));
            const double                dr = r - r2;
            if (s2 && dr > 1e-9) {
                const double dz = *s2 - *surf;
                cos_slope = dr / std::sqrt(dr * dr + dz * dz);
            }
        }

        // Only emit where the surface is shallow enough. The steeper outer part is skipped
        // (and skipped fast, at a full line-width pitch) so normal perimeters print it.
        const bool within_slope = cos_slope >= cos_max_slope - 1e-9;
        if (within_slope) {
            const double progress = 1.0 - r / region.base_radius;
            double z = surf ? *surf : region.base_z + (region.apex_z - region.base_z) * progress;
            if (trans_angle > 0.0 && theta < trans_angle) {
                const double t = theta / trans_angle;   // 0 at base seam -> 1 after the ramp
                z = region.base_z + (z - region.base_z) * t;
            }
            pts.emplace_back(xy.x(), xy.y(), z);
        }

        const double pitch_per_rev = within_slope
            ? std::max(min_pitch_per_rev, params.line_width * cos_slope)
            : params.line_width;
        r     -= pitch_per_rev * (d_theta / (2.0 * M_PI));
        theta += d_theta;
    }

    // Close on the apex (only meaningful if we emitted the shallow top at all).
    if (!pts.empty()) {
        const std::optional<double> surf = surface_z(region.center);
        pts.emplace_back(region.center.x(), region.center.y(), surf ? *surf : region.apex_z);
    }

    return pts;
}

} // namespace Slic3r

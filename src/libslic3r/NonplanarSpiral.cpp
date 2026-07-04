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
    const NonplanarRegionOverride&          ovr)
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

        if (last - first < 2)   // need at least 3 layers to be a meaningful cap
            return std::nullopt;
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

} // namespace Slic3r

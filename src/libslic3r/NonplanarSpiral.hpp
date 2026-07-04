#pragma once
#ifndef slic3r_NonplanarSpiral_hpp
#define slic3r_NonplanarSpiral_hpp

// Tier A nonplanar surface spiral — pure geometry.
//
// This module owns the geometry of the "surface spiral" nonplanar mode:
//   - detect_nonplanar_region(): find a rotationally-symmetric dome cap from sliced layers
//   - (later phases) generate_spiral(), clearance_check()
//
// It deliberately depends only on libslic3r geometry types (no g-code, no GUI) so it can
// be unit-tested in isolation. See NonplanarSurface for the mesh Z-raycast used to lift
// the generated spiral onto the surface.

#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <vector>
#include <optional>
#include <functional>

namespace Slic3r {

// One sliced layer reduced to what region detection needs: its top Z (mm) and outline(s).
struct NonplanarLayerSlice {
    double     print_z = 0.0;  // mm, top Z of the layer
    ExPolygons islands;        // layer outline(s) in scaled coordinates
};

// Optional user overrides for region detection. When a field is set it forces that value;
// unset fields fall back to auto-detection. When enabled is false the overrides are ignored.
struct NonplanarRegionOverride {
    bool                  enabled        = false;
    std::optional<Vec2d>  center_mm;       // forced XY center (mm)
    std::optional<double> base_radius_mm;  // forced base radius (mm)
    std::optional<int>    first_layer;     // forced lowest cap layer index
    std::optional<int>    last_layer;      // forced topmost cap layer index
};

// The detected rotationally-symmetric cap region, in the index space of the layers vector
// passed to detect_nonplanar_region().
struct NonplanarTopRegion {
    int    first_layer = -1;               // lowest layer of the cap (widest radius)
    int    last_layer  = -1;               // topmost layer of the cap (apex)
    Vec2d  center      = Vec2d::Zero();    // mm, XY center of the circular cross-sections
    double base_radius = 0.0;              // mm, radius at first_layer
    double base_z      = 0.0;              // mm, print_z at first_layer
    double apex_z      = 0.0;              // mm, print_z at last_layer

    bool valid() const { return first_layer >= 0 && last_layer > first_layer; }
    int  layer_count() const { return valid() ? (last_layer - first_layer + 1) : 0; }
};

// Detects the top dome cap from sliced layer outlines. A cap is a contiguous run of top
// layers whose dominant island is approximately circular, concentric, and widening toward
// the base (which excludes a constant-radius cylinder below a domed top). Returns nullopt
// when no such cap is found (e.g. a cube), in which case the caller falls back to normal
// slicing. Optional overrides force part or all of the region.
std::optional<NonplanarTopRegion> detect_nonplanar_region(
    const std::vector<NonplanarLayerSlice>& layers,
    const NonplanarRegionOverride&          ovr = {});

// Parameters controlling spiral generation.
struct NonplanarSpiralParams {
    double line_width      = 0.42;  // mm, radial pitch between successive revolutions
    int    points_per_rev  = 360;   // angular resolution (points per full turn)
    double transition_revs = 0.5;   // revolutions to blend from flat base Z to surface Z
};

// Surface Z lookup: given an XY position in plate-space mm, returns the mesh surface Z
// there (mm), or nullopt if the ray misses the mesh. Phase 4 supplies a lambda wrapping
// NonplanarSurface; tests supply an analytic function.
using SurfaceZFn = std::function<std::optional<double>(const Vec2d& xy_mm)>;

// Generates a single continuous area-filling Archimedean spiral over the detected cap.
// The spiral starts at the base seam (radius = region.base_radius, Z = region.base_z) and
// winds inward to the apex, with a constant radial pitch equal to params.line_width so the
// revolutions tile the cap. Each point's Z is taken from surface_z(); the first
// params.transition_revs blend from the flat base Z up to the surface to avoid a step at
// the join with the flat ring below. Points that miss the mesh fall back to a linear
// base->apex Z estimate. Returns plate-space mm points; empty if the region is degenerate.
std::vector<Vec3d> generate_spiral(
    const NonplanarTopRegion&    region,
    const NonplanarSpiralParams& params,
    const SurfaceZFn&            surface_z);

} // namespace Slic3r

#endif // slic3r_NonplanarSpiral_hpp

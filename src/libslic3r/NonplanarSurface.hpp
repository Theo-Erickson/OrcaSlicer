#pragma once
#ifndef slic3r_NonplanarSurface_hpp
#define slic3r_NonplanarSurface_hpp

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <vector>
#include <optional>

/*
Default (nonplanar disabled)
    Info: 
        Every layer prints at a flat, constant Z. Perimeters are perfect horizontal rings. 
        The staircase effect is fully visible on curved surfaces because each layer has a hard step at its edges. 
        Fast to slice, zero risk of nozzle collision, works on every model.

Normal Interpolation
    Info: 
        Finds the closest mesh face to each toolpath point and reads its surface normal. 
        Derives a Z offset from the slope angle of that normal. 
        If the face slopes at 30 degrees, the nozzle lifts proportionally to follow that slope.
    Advantages: 
        Fast, smooth results on organic curves, works well on gentle slopes like figurines or ergonomic handles, cheap to compute since it just reads a precomputed normal.
    Disadvantages: 
        The offset is derived from the normal angle not the actual surface position, so it approximates rather than exactly following the surface. 
        On hard-edged geometry like a dome or box with rounded corners it tends to undershoot because the normal at the slicing plane is nearly horizontal even when the surface above is significantly elevated. 
        Produces good blending but less geometric precision. Also sensitive to mesh quality since noisy normals produce noisy Z offsets.

Surface Raycast
    Info: 
        Queries from directly above each toolpath point and finds the actual surface Z at that XY location. 
        The nozzle is placed at the true geometric surface height rather than an approximation from normals.
    Advantages: 
        Geometrically accurate, works correctly on any curved surface including domes, boxes with fillets, and hard architectural curves. 
        The Z value is the actual surface intersection, not an estimate. Better for surfaces where precision matters more than smoothness.
    Disadvantages: 
        Slightly more expensive per point since it does two AABB queries. 
        The lift is still clamped to max_dz so on steeply curved surfaces it hits the ceiling and can only partially follow the geometry. 
        Also has the lateral tolerance requirement which can miss hits on very irregular or thin geometry.

When to use each NonPlanarMode
    Use Normal Interpolation for organic smooth models like characters, terrain, or any surface where the slope changes gradually. The smooth blending hides the approximation error well.
    Use Surface Raycast for architectural or mechanical models with defined curves: rounded box tops, dome shapes, fillets on edges. Anywhere you want the nozzle to be at the exact surface rather than an approximation.
    In both cases the fundamental limitation compared to the reference images you showed is that the toolpath direction is still horizontal rings. Both modes lift those rings to follow the surface, but neither restructures the toolpath to run up the slope. That is a Tier 3 change that would require replacing the layer generation system.
*/

namespace Slic3r {

// ─── Configuration passed in from PrintConfig ─────────────────────────────

// Holds all user-facing nonplanar slicing settings read from PrintConfig.
// Passed by value into NonplanarSurface so it is self-contained.
struct NonplanarConfig {
    // Master switch: when false, all nonplanar logic is bypassed entirely.
    bool   enabled             = false;

    // Which algorithm to use for Z offset computation.
    NonplanarMode mode          = NonplanarMode::NormalInterpolation;

    // Maximum allowed surface slope in degrees before the Z offset is clamped.
    // Prevents the toolpath from attempting to follow near-vertical faces.
    //
    // Safe upper bounds by nozzle diameter:
    //   0.4 mm nozzle: up to ~30 degrees
    //   0.6 mm nozzle: up to ~40 degrees
    //   0.8 mm nozzle: up to ~45 degrees
    // The "nozzle-aware auto-clamp" checkbox in the UI enforces these ceilings
    // automatically; this field holds the final resolved angle after clamping.
    double max_slope_angle_deg  = 30.0;

    // Nominal layer height in mm. Used to compute max_dz() and as the search
    // window for the raycast mode (rays are fired from layer_z + raycast_search_height
    // * layer_height downward).
    double layer_height         = 0.2;

    // Nozzle diameter in mm. Used as the lateral step size in NormalInterpolation
    // mode when converting a surface normal into a Z offset.
    double nozzle_diameter      = 0.4;

    // When true, nonplanar Z lifting is applied only to perimeter extrusions.
    // Infill and other roles remain at flat layer Z.
    bool   perimeters_only      = true;

    // How far above the nominal layer Z (in multiples of layer_height) to start
    // the downward ray in SurfaceRaycast mode. Default 2.0 means the ray starts
    // two layer heights above the nominal plane, which ensures it begins above
    // any surface the nozzle might encounter on that layer.
    // Only used when mode == SurfaceRaycast.
    double raycast_search_height = 2.0;

    // Blend factor applied to the computed Z offset before emitting G-code.
    // 1.0 = full nonplanar correction; 0.5 = half strength (useful for testing).
    double z_scale              = 1.0;

    // Controls how aggressively smooth_z_transitions blends adjacent Z values.
    // 0.0 = no smoothing; 1.0 = clamp each step to 50% of layer height.
    // Stored as a fraction; the actual max_step passed to smooth_z_transitions
    // is smoothing_strength * layer_height * 0.5.
    double smoothing_strength   = 1.0;

    // When true, nonplanar treatment is restricted to the topmost N layers of
    // the object (controlled by top_layer_count). Useful for improving top
    // surface finish without touching structural lower layers.
    bool   top_layers_only      = false;
    int    top_layer_count      = 3;

    // When true, extra debug comments are written into the G-code output.
    // Intended for users without C++ access who need to diagnose behavior.
    bool   debug_output         = false;
};

// ─── Per-point Z offset result ─────────────────────────────────────────────

// Result type returned by z_offset_at.
// Separates the computed delta-Z from a validity flag so callers can
// distinguish "offset is zero" from "no surface was found nearby".
struct ZOffset {
    // The signed Z delta to add to the flat layer Z for this XY position.
    // Positive values lift the nozzle; negative values lower it.
    double dz    = 0.0;
    // True if a mesh surface was found and dz is meaningful.
    bool   valid = false;
};

// ─── Main interface ────────────────────────────────────────────────────────

// Encapsulates all nonplanar slicing logic for a single mesh.
// Owns a copy of the indexed triangle set and a prebuilt AABB tree so it can
// answer spatial queries (nearest face, surface normal) efficiently at slice time.
class NonplanarSurface {
public:
    // Constructs the nonplanar surface helper for the given mesh and config.
    // Precomputes per-face unit normals and builds the AABB tree.
    // If cfg.enabled is false or the mesh has no faces, construction is a no-op
    // and all queries return invalid/zero offsets.
    explicit NonplanarSurface(const TriangleMesh& mesh, const NonplanarConfig& cfg);

    // Returns the signed Z offset to apply at the given XY position on layer_z.
    // Dispatches to the appropriate algorithm based on cfg.mode.
    ZOffset z_offset_at(const Vec2d& xy, double layer_z) const;

    // Converts a 2-D polyline at flat layer height layer_z into a 3-D lifted
    // point cloud. Uses cfg.z_scale to attenuate the lifting effect.
    // Returns one Vec3d per input point.
    std::vector<Vec3d> lift_polyline(
        const Polyline& poly,
        double          layer_z) const;
    
    // Variant of lift_polyline that accepts points already in mm plate-space,
    // skipping the unscaled<double> conversion. Use this when the caller has
    // already applied point_to_gcode() to convert from object-local scaled coords.
    std::vector<Vec3d> lift_polyline(
        const std::vector<Vec2d>& pts_mm,
        double                    layer_z) const;

    // Returns true if nonplanar slicing is active (cfg.enabled was set).
    bool is_enabled() const { return m_cfg.enabled; }
    NonplanarMode mode() const { return m_cfg.mode; }

    // Returns the mesh surface Z (mm, plate-space) directly above the given XY, found by
    // firing a vertical ray downward from above the mesh's top. Unlike raycast_surface_z
    // this has no layer-height search window, so it works for the full-height queries the
    // SurfaceSpiral generator needs. Returns nullopt if the ray misses the mesh.
    std::optional<double> surface_z_at(const Vec2d& xy_mm) const;

private:
    // ── NormalInterpolation helpers ──────────────────────────────────────

    // Finds the closest mesh face and returns its precomputed unit normal.
    // Returns nullopt if the closest point is farther than the proximity guard.
    std::optional<Vec3d> surface_normal_at(const Vec3d& pt) const;

    // Converts a unit surface normal into a signed Z offset for one nozzle-width
    // lateral step. Slope is clamped by max_slope_angle_deg.
    double normal_to_dz(const Vec3d& normal, double step_size) const;

    // ── SurfaceRaycast helpers ──────────────────────────────────────────

    // Fires a downward ray from (xy, layer_z + search_offset) and returns the
    // Z coordinate of the first mesh intersection found, or nullopt if no hit
    // occurs within the search window. The returned Z is clamped to
    // [layer_z - max_dz(), layer_z + max_dz()] before being returned.
    std::optional<double> raycast_surface_z(const Vec2d& xy, double layer_z) const;

    // ── Shared helpers ──────────────────────────────────────────────────

    // Returns the per-layer Z deviation ceiling: 75% of the configured layer height.
    // All computed dz values are clamped to [-max_dz(), +max_dz()] before use.
    double max_dz() const;

    // ── Data ────────────────────────────────────────────────────────────

    indexed_triangle_set                            m_mesh;

    // Copy of the config supplied at construction time.
    NonplanarConfig                                 m_cfg;

    // Alias for the AABB tree type parameterized over 3-D float geometry.
    using TreeType = AABBTreeIndirect::Tree<3, float>;

    // Spatial acceleration structure built over m_mesh at construction time.
    // Used to find the closest triangle face to any query point in O(log n) time.
    TreeType                                        m_tree;

    // Z (mm) a little above the mesh's highest vertex — the origin height for the
    // full-height downward ray in surface_z_at(). Computed once at construction.
    float                                           m_mesh_top_z = 0.f;

    // Unit face normals, one per triangle in m_mesh.indices, precomputed in the
    // constructor so z_offset_at queries do not recompute cross-products at runtime.
    std::vector<Vec3f>                              m_face_normals;
};

// ── Free helpers ────────────────────────────────────────────────────────────

// Post-processes a lifted point array in-place by clamping the Z step between
// consecutive points to max_step in both forward and reverse passes.
void smooth_z_transitions(std::vector<Vec3d>& pts, double max_step);

// Returns the safe max_slope_angle_deg for a given nozzle diameter.
// Used by the UI "nozzle-aware auto-clamp" checkbox.
double nonplanar_safe_angle_for_nozzle(double nozzle_diameter_mm);

} // namespace Slic3r

#endif // slic3r_NonplanarSurface_hpp
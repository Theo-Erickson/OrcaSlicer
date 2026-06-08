#pragma once
#ifndef slic3r_NonplanarSurface_hpp
#define slic3r_NonplanarSurface_hpp

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"

#include <vector>
#include <optional>

namespace Slic3r {

// ─── Configuration passed in from PrintConfig ─────────────────────────────

// Holds all user-facing nonplanar slicing settings read from PrintConfig.
// Passed by value into NonplanarSurface so it is self-contained and
// does not depend on the lifetime of the originating config object.
struct NonplanarConfig {
    // Master switch: when false, all nonplanar logic is bypassed entirely.
    bool   enabled             = false;

    // Maximum allowed surface slope in degrees before the Z offset is clamped.
    // Prevents the toolpath from attempting overhangs the nozzle cannot safely traverse.
    double max_slope_angle_deg = 30.0;

    // Nominal layer height in mm, used to compute the maximum allowed Z deviation
    // per layer (max_dz) and the smoothing step in smooth_z_transitions.
    double layer_height        = 0.2;

    // Nozzle diameter in mm, used as the lateral step size when converting a
    // surface normal into a Z offset via normal_to_dz.
    double nozzle_diameter     = 0.4;

    // When true, nonplanar Z lifting is applied only to perimeter extrusions.
    // Infill extrusions are left at the flat layer Z.
    bool   perimeters_only     = true;
};

// ─── Per-point Z offset result ─────────────────────────────────────────────

// Result type returned by z_offset_at.
// Separates the computed delta-Z from a validity flag so callers can
// distinguish "offset is zero" from "no surface was found nearby".
struct ZOffset {
    // The signed Z delta to add to the flat layer Z for this XY position.
    // Positive values lift the nozzle; negative values lower it.
    double dz    = 0.0;

    // True if a mesh surface was found within the search radius and dz is meaningful.
    // False means the point is far from the mesh and the flat layer Z should be used.
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
    // and all queries will return invalid/zero offsets.
    explicit NonplanarSurface(const TriangleMesh& mesh, const NonplanarConfig& cfg);

    // Returns the signed Z offset to apply at the given XY position on layer layer_z.
    // Finds the closest mesh face via the AABB tree, reads its precomputed normal,
    // and converts slope information into a delta-Z value clamped to max_dz().
    // Returns an invalid ZOffset if the mesh is disabled, the tree is empty, or
    // no face is within the proximity threshold.
    ZOffset z_offset_at(const Vec2d& xy, double layer_z) const;

    // Converts a 2-D polyline at flat layer height layer_z into a 3-D lifted
    // point cloud by querying z_offset_at for every point.
    // z_scale lets callers attenuate the lifting effect (e.g., 0.5 for half strength).
    // Returns one Vec3d per input point; Z = layer_z + dz * z_scale.
    std::vector<Vec3d> lift_polyline(
        const Polyline& poly,
        double          layer_z,
        double          z_scale = 1.0) const;

    // Returns true if nonplanar slicing is active (cfg.enabled was set).
    bool is_enabled() const { return m_cfg.enabled; }

private:
    // Queries the AABB tree for the mesh face closest to pt, then returns
    // that face's precomputed unit normal as a Vec3d.
    // Returns nullopt if the closest point is farther than max_r (hard-coded
    // proximity threshold), preventing spurious offsets far from the model surface.
    std::optional<Vec3d> surface_normal_at(const Vec3d& pt) const;

    // Converts a surface unit normal into a signed Z offset for a single
    // nozzle step of width step_size.
    // The slope magnitude is derived from the ratio of the horizontal normal
    // component to the vertical component, then clamped by max_slope_angle_deg.
    // Sign follows the direction of the Z component of the normal.
    double normal_to_dz(const Vec3d& normal, double step_size) const;

    // Returns the per-layer Z deviation ceiling: 75% of the configured layer height.
    // All computed dz values are clamped to [-max_dz(), +max_dz()] before use.
    double max_dz() const;

    // Owned copy of the mesh triangle set (vertices + face index triples).
    // Stored by value, not pointer, so NonplanarSurface has no dependency on
    // the lifetime of the source TriangleMesh passed to the constructor.
    indexed_triangle_set                            m_mesh;

    // Copy of the config supplied at construction time.
    NonplanarConfig                                 m_cfg;

    // Alias for the AABB tree type parameterized over 3-D float geometry.
    using TreeType = AABBTreeIndirect::Tree<3, float>;

    // Spatial acceleration structure built over m_mesh at construction time.
    // Used to find the closest triangle face to any query point in O(log n) time.
    TreeType                                        m_tree;

    // Unit face normals, one per triangle in m_mesh.indices, precomputed in the
    // constructor so z_offset_at queries do not recompute cross-products at runtime.
    std::vector<Vec3f>                              m_face_normals;
};

// Post-processes a lifted point array in-place by clamping the Z step between
// consecutive points to max_step in both the forward and reverse pass.
// This smooths out sharp Z transitions that could cause toolpath artifacts or
// collisions when the mesh has high-frequency surface curvature.
void smooth_z_transitions(std::vector<Vec3d>& pts, double max_step);

} // namespace Slic3r

#endif // slic3r_NonplanarSurface_hpp
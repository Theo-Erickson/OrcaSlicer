// NonplanarSurface.cpp
// See NonplanarSurface.hpp for algorithm and mode documentation.

#include "NonplanarSurface.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <boost/log/trivial.hpp>

namespace Slic3r {

// ─── Free helper implementations ──────────────────────────────────────────

double nonplanar_safe_angle_for_nozzle(double nozzle_diameter_mm)
{
    // Empirical safe ceiling by nozzle width.
    // Wider nozzles bridge layer steps more easily, allowing steeper angles.
    if (nozzle_diameter_mm <= 0.35)  return 25.0;
    if (nozzle_diameter_mm <= 0.45)  return 30.0;
    if (nozzle_diameter_mm <= 0.55)  return 35.0;
    if (nozzle_diameter_mm <= 0.65)  return 40.0;
    return 45.0; // 0.6 mm and above
}

// ─── Construction ──────────────────────────────────────────────────────────

// Copies the indexed triangle set out of the source mesh by value to avoid
// holding a dangling reference, then precomputes face normals and builds the
// AABB tree. Early-exits if nonplanar slicing is disabled or the mesh is empty,
// leaving m_tree empty so all subsequent queries short-circuit cleanly.
NonplanarSurface::NonplanarSurface(const TriangleMesh& mesh, const NonplanarConfig& cfg)
    : m_mesh(mesh.its)   // copy the indexed_triangle_set by value — no dangling ref
    , m_cfg(cfg)
{
    if (!cfg.enabled || m_mesh.indices.empty())
        return;

    // Pre-compute unit face normals once. Degenerate (zero-area) faces get a
    // straight-up fallback normal so they never produce NaN offsets.
    // Iterating once here is cheaper than recomputing cross-products for every
    // z_offset_at query during slicing.
    m_face_normals.reserve(m_mesh.indices.size());
    for (const auto& face : m_mesh.indices) {
        // Retrieve the three vertex positions for this triangle.
        const Vec3f& v0 = m_mesh.vertices[face(0)];
        const Vec3f& v1 = m_mesh.vertices[face(1)];
        const Vec3f& v2 = m_mesh.vertices[face(2)];
        // Cross product of two edges gives the un-normalized face normal.
        Vec3f n = (v1 - v0).cross(v2 - v0);
        float len = n.norm();
        // Normalize, guarding against degenerate (zero-area) triangles.
        m_face_normals.push_back(len > 1e-8f ? (n / len) : Vec3f(0.f, 0.f, 1.f));
    }

    // Build AABB tree over the indexed_triangle_set.
    // The tree stores bounding boxes for each triangle so closest-point queries
    // run in O(log n) rather than O(n) per toolpath point.
    m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        m_mesh.vertices, m_mesh.indices);
}

// ─── Public API ───────────────────────────────────────────────────────────

// Dispatches to the appropriate algorithm based on cfg.mode.
ZOffset NonplanarSurface::z_offset_at(const Vec2d& xy, double layer_z) const
{
    // Short-circuit if the feature is disabled or no tree was built.
    if (!m_cfg.enabled || m_tree.empty())
        return {};

    switch (m_cfg.mode) {
    case NonplanarMode::NormalInterpolation: {
        // Query the mesh surface at a point half a layer above the nominal Z
        // so we sample the normal at approximately the bead center height.
        Vec3d query(xy.x(), xy.y(), layer_z + m_cfg.layer_height * 0.5);
        auto maybe_normal = surface_normal_at(query);
        if (!maybe_normal)
            return {};

        // Convert the surface normal slope into a Z delta, then clamp to the
        // per-layer maximum deviation so we never move the nozzle more than
        // 75% of a layer height in one step.
        const double dz = normal_to_dz(*maybe_normal, m_cfg.nozzle_diameter);
        return ZOffset{ std::clamp(dz, -max_dz(), max_dz()), true };
    }

    case NonplanarMode::SurfaceRaycast: {
        auto maybe_z = raycast_surface_z(xy, layer_z);
        if (!maybe_z)
            return {};
        // Convert absolute surface Z to a delta from the nominal layer Z.
        const double dz = *maybe_z - layer_z;
        return ZOffset{ std::clamp(dz, -max_dz(), max_dz()), true };
    }
    }

    return {};
}

// Applies z_offset_at to every point in a polyline, producing a lifted 3-D
// point cloud. z_scale allows partial application of the lifting effect,
// e.g. 0.5 to blend between flat and fully nonplanar output.
std::vector<Vec3d> NonplanarSurface::lift_polyline(
    const Polyline& poly,
    double          layer_z) const
{
    std::vector<Vec3d> result;
    result.reserve(poly.size());

    for (const Point& pt : poly.points) {
        Vec2d xy = unscaled<double>(pt);
        ZOffset off = z_offset_at(xy, layer_z);
        const double dz = off.valid ? off.dz * m_cfg.z_scale : 0.0;
        result.emplace_back(xy.x(), xy.y(), layer_z + dz);
    }
    

    // Apply Z-transition smoothing based on the user-configured strength.
    // smoothing_strength 0.0 skips this entirely; 1.0 uses the full half-layer clamp.
    if (m_cfg.smoothing_strength > 0.0 && result.size() >= 2) {
        const double max_step = m_cfg.smoothing_strength * m_cfg.layer_height * 0.5;
        smooth_z_transitions(result, max_step);
    }

    // smooth_z_transitions is currently disabled: left in place for future
    // re-enabling once the Z values are confirmed correct end-to-end.
    //smooth_z_transitions(result, m_cfg.layer_height * 0.5);
    return result;
}

std::vector<Vec3d> NonplanarSurface::lift_polyline(
    const std::vector<Vec2d>& pts_mm,
    double                    layer_z) const
{
    std::vector<Vec3d> result;
    result.reserve(pts_mm.size());

    for (const Vec2d& xy : pts_mm) {
        ZOffset off = z_offset_at(xy, layer_z);
        const double dz = off.valid ? off.dz * m_cfg.z_scale : 0.0;
        result.emplace_back(xy.x(), xy.y(), layer_z + dz);
    }

    if (m_cfg.smoothing_strength > 0.0 && result.size() >= 2) {
        const double max_step = m_cfg.smoothing_strength * m_cfg.layer_height * 0.5;
        smooth_z_transitions(result, max_step);
    }

    return result;
}

// ─── NormalInterpolation helpers ───────────────────────────────────────────

// Finds the mesh face closest to pt using the AABB tree and returns its
// precomputed unit normal. Returns nullopt if the tree or normal cache is empty,
// or if the closest point is farther than max_r mm (proximity guard to prevent
// offsets from being applied to toolpath segments that are far from the mesh surface).
std::optional<Vec3d> NonplanarSurface::surface_normal_at(const Vec3d& pt) const
{
    if (m_tree.empty() || m_face_normals.empty())
        return std::nullopt;

    // face_idx will be written by squared_distance_to_indexed_triangle_set
    // with the index of the closest triangle.
    size_t face_idx = std::numeric_limits<size_t>::max();
    // closest receives the actual closest point on the triangle surface in 3-D space.
    Vec3f  closest;

    // Pass a Vec3f query — the AABB tree is built over float vertices so the
    // query point must also be float.
    //The .cast<float>() on an rvalue produces a CwiseUnaryOp expression template, not a Vec3f. 
    //The overload resolution can't deduce VectorType consistently from a mix of std::vector<Vec3f> and a lazy Eigen expression. 
    //Naming it forces evaluation to a concrete Vec3f first, giving the template a single unambiguous type to bind to.
    const Vec3f pt_f = pt.cast<float>();
    double dist2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
        m_mesh.vertices,
        m_mesh.indices,
        m_tree,
        pt_f,
        face_idx,
        closest);

    // Sanity check: face_idx must be a valid index into m_face_normals.
    if (face_idx >= m_face_normals.size())
        return std::nullopt;

    // Proximity guard: if the closest point on the mesh is more than max_r mm away,
    // the query location is not on or near the surface, so no offset is applied.
    const double max_r = 30.0;
    if (dist2 > max_r * max_r)
        return std::nullopt;

    // Upcast from float to double to match the Vec3d return type expected by callers.
    return m_face_normals[face_idx].cast<double>();
}

// Converts a unit surface normal into a signed Z offset for one nozzle-width step.
// The horizontal component of the normal represents the surface slope; dividing it
// by the vertical component gives the tangent of the slope angle. This is clamped
// to the tangent of max_slope_angle_deg to prevent extreme lifts on near-vertical
// faces. The sign mirrors the Z direction of the normal: faces pointing up produce
// a positive (lift) offset, faces pointing down produce a negative (lower) offset.
double NonplanarSurface::normal_to_dz(const Vec3d& normal, double step_size) const
{
    // Vertical component of the normal (cos of inclination from the XY plane).
    const double nz  = normal.z();
    // Horizontal magnitude of the normal (sin of inclination).
    const double nxy = std::sqrt(normal.x() * normal.x() + normal.y() * normal.y());

    // Flat horizontal surfaces (nz near zero): no offset needed.
    if (std::abs(nz) < 1e-6)
        return 0.0;

    // Compute slope as tan(angle) = horizontal / vertical, then cap it at the
    // configured maximum slope to avoid toolpath collisions on steep geometry.
    const double max_slope = std::tan(m_cfg.max_slope_angle_deg * M_PI / 180.0);
    const double slope     = std::min(nxy / std::abs(nz), max_slope);

    // Sign follows the Z component of the normal: upward-facing surfaces lift
    // the nozzle (positive dz), downward-facing surfaces lower it.
    return (nz > 0.0 ? +1.0 : -1.0) * slope * step_size;
}

// ─── SurfaceRaycast helpers ────────────────────────────────────────────────

// Two-pass normal-guided surface query.
//
// WHY TWO PASSES:
// A naive closest-point query from directly above layer_z fails on curved surfaces.
// On a dome or any smoothly curved model, the face at the perimeter of each layer
// is tangent to the slicing plane at exactly layer_z. This means the closest point
// on that face to any query point near layer_z is always AT layer_z, so dz = 0
// regardless of how curved the surface is. Querying from above (layer_z + offset)
// helps but doesn't fully fix this because the same tangent face is still the
// closest geometry to the elevated query.
//
// THE FIX:
// Pass 1: find the face at layer_z and read its outward normal. This is the face
//         the toolpath sits on, and its normal points in the direction the nozzle
//         needs to travel to follow the surface curvature.
// Pass 2: step along that normal by one layer height to reach a point that is
//         strictly above the surface in the direction of curvature. The second
//         closest-point query from there finds the face the nozzle should conform
//         to on this layer, whose Z coordinate is the target lifted Z.
//
// This approximates a true downward ray without requiring a full ray-triangle
// intersection sweep. It works correctly for any smoothly curved convex surface
// because the outward normal always points away from the surface interior toward
// the region the nozzle occupies. For nearly-flat surfaces the normal is nearly
// vertical and the result degrades gracefully toward dz = 0, which is correct.
std::optional<double> NonplanarSurface::raycast_surface_z(const Vec2d& xy, double layer_z) const
{
    // Sanity check: reject obviously wrong coordinates that indicate
    // a units mismatch or uninitialized values.
    if (std::abs(xy.x()) > 1000.0 || std::abs(xy.y()) > 1000.0 ||
        layer_z < 0.0 || layer_z > 1000.0) {
        BOOST_LOG_TRIVIAL(warning) << "NP raycast SANITY FAIL: xy=("
            << xy.x() << "," << xy.y() << ") layer_z=" << layer_z
            << " - likely units mismatch, skipping";
        return std::nullopt;
        }
    
    if (m_tree.empty())
        return std::nullopt;

    // Fire a true vertical ray straight down from search_z. The first hit is the
    // topmost mesh surface directly above the toolpath point — exactly the surface Z
    // the nozzle should follow. This replaces the old closest-point query, which
    // returned the nearest surface anywhere in 3D and so under-sampled sloped faces
    // (the lateral tolerance then rejected exactly the slopes we care about).
    // Intersection is computed in double even though the mesh/tree are float, so
    // the ray origin and direction must be Vec3d (matches SeamPlacer / AABBMesh usage).
    const double search_z = layer_z + m_cfg.raycast_search_height * m_cfg.layer_height;
    const Vec3d origin(xy.x(), xy.y(), search_z);
    const Vec3d dir(0.0, 0.0, -1.0);

    igl::Hit<float> hit;
    if (!AABBTreeIndirect::intersect_ray_first_hit(
            m_mesh.vertices, m_mesh.indices, m_tree, origin, dir, hit))
        return std::nullopt;

    // dir is a unit vector, so hit.t is the distance travelled downward from search_z.
    const double surface_z = search_z - static_cast<double>(hit.t);

    // Accept only hits above the layer plane and within the search window. Hits at or
    // below layer_z are the already-sliced face, not the surface above it.
    if (surface_z <= layer_z + 1e-4 ||
        surface_z > layer_z + m_cfg.raycast_search_height * m_cfg.layer_height) {
        BOOST_LOG_TRIVIAL(trace) << "NP raycast rejected z_window: surface_z="
            << surface_z << " layer_z=" << layer_z;
        return std::nullopt;
    }

    BOOST_LOG_TRIVIAL(trace) << "NP raycast hit: surface_z=" << surface_z
        << " layer_z=" << layer_z << " dz=" << surface_z - layer_z;
    return surface_z;
}

// ─── Shared helpers ────────────────────────────────────────────────────────

// Returns the per-layer Z deviation ceiling used by the warp modes
// (NormalInterpolation / SurfaceRaycast): 2x the configured layer height.
// This caps how far a single planar-ring point may be lifted. Note the
// SurfaceSpiral mode does NOT use this clamp — it sets Z directly from the
// surface raycast and relies on the collision clearance check instead.
double NonplanarSurface::max_dz() const
{
    return m_cfg.layer_height * 2.00;
}

// ─── Free helper ──────────────────────────────────────────────────────────

// Smooths Z transitions in a lifted point array with a two-pass forward/backward
// clamp. Each consecutive pair of points is constrained so the Z delta cannot
// exceed max_step in either direction. The forward pass enforces the limit going
// head-to-tail; the backward pass enforces it going tail-to-head so that a large
// step at the end does not propagate unclamped values back toward the start.
void smooth_z_transitions(std::vector<Vec3d>& pts, double max_step)
{
    if (pts.size() < 2 || max_step <= 0.0)
        return;

    // Forward pass: clamp each point's Z relative to the previous point.
    for (size_t i = 1; i < pts.size(); ++i) {
        double dz = pts[i].z() - pts[i - 1].z();
        if      (dz >  max_step) pts[i].z() = pts[i - 1].z() + max_step;
        else if (dz < -max_step) pts[i].z() = pts[i - 1].z() - max_step;
    }
    // Backward pass: clamp each point's Z relative to the next point to
    // propagate constraints in the reverse direction as well.
    for (size_t i = pts.size() - 1; i > 0; --i) {
        double dz = pts[i - 1].z() - pts[i].z();
        if      (dz >  max_step) pts[i - 1].z() = pts[i].z() + max_step;
        else if (dz < -max_step) pts[i - 1].z() = pts[i].z() - max_step;
    }
}

} // namespace Slic3r
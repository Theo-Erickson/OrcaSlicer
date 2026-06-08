#include "NonplanarSurface.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {

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

    // Pre-compute unit face normals.
    // Iterating once here is cheaper than recomputing cross-products for every
    // z_offset_at query during slicing. Degenerate faces (zero-length normal)
    // get a fallback straight-up normal so they never produce NaN offsets.
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

// Computes the signed Z offset for a single XY coordinate on a given layer.
// The query point is placed halfway through the layer height so it samples the
// surface normal at the middle of the extrusion band rather than its bottom face.
ZOffset NonplanarSurface::z_offset_at(const Vec2d& xy, double layer_z) const
{
    BOOST_LOG_TRIVIAL(warning) << "NP query: " << xy.x() << ", " << xy.y() 
        << " layer_z=" << layer_z;
    
    // Short-circuit if the feature is disabled or no tree was built.
    if (!m_cfg.enabled || m_tree.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "NP: tree empty or disabled";
        return {};
    }

    // Offset the query point half a layer upward so the lookup samples the surface
    // at approximately the center of the deposited bead, not its bottom edge.
    Vec3d query(xy.x(), xy.y(), layer_z + m_cfg.layer_height * 0.5);
    auto maybe_normal = surface_normal_at(query);
    if (!maybe_normal) {
        BOOST_LOG_TRIVIAL(warning) << "NP: no normal at " 
            << xy.x() << "," << xy.y() << " z=" << layer_z;
        return {};
    }

    // Convert the surface normal slope into a Z delta, then clamp to the
    // per-layer maximum deviation so we never move the nozzle more than
    // 75% of a layer height in one step.
    const double dz = normal_to_dz(*maybe_normal, m_cfg.nozzle_diameter);
    BOOST_LOG_TRIVIAL(warning) << "NP: dz=" << dz << " at z=" << layer_z;
    return ZOffset{ std::clamp(dz, -max_dz(), max_dz()), true };
}

// Applies z_offset_at to every point in a polyline, producing a lifted 3-D
// point cloud. z_scale allows partial application of the lifting effect,
// e.g. 0.5 to blend between flat and fully nonplanar output.
std::vector<Vec3d> NonplanarSurface::lift_polyline(
    const Polyline& poly,
    double          layer_z,
    double          z_scale) const
{
    std::vector<Vec3d> result;
    result.reserve(poly.size());

    for (const Point& pt : poly.points) {
        // Convert from scaled integer coordinates to floating-point mm.
        Vec2d xy = unscaled<double>(pt);
        ZOffset off = z_offset_at(xy, layer_z);
        // Fall back to flat layer Z if no valid surface offset was found.
        double dz = off.valid ? off.dz * z_scale : 0.0;
        result.emplace_back(xy.x(), xy.y(), layer_z + dz);
    }

    // smooth_z_transitions is currently disabled: left in place for future
    // re-enabling once the Z values are confirmed correct end-to-end.
    //smooth_z_transitions(result, m_cfg.layer_height * 0.5);
    return result;
}

// ─── Private helpers ───────────────────────────────────────────────────────

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
    // query point must also be float.  Cast here, not at the call site.
    Vec3f pt_f = pt.cast<float>();

    // Squared distance query: cheaper than a full sqrt and sufficient for the
    // radius threshold check below.
    double dist2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
        m_mesh.vertices,
        m_mesh.indices,
        m_tree,
        pt_f,
        face_idx,
        closest);
    
    
    BOOST_LOG_TRIVIAL(warning) << "NP dist=" << std::sqrt(dist2) 
    << " max_r=" << 2.0
    << " query=(" << pt_f.x() << "," << pt_f.y() << "," << pt_f.z() << ")"
    << " closest=(" << closest.x() << "," << closest.y() << "," << closest.z() << ")";

    // Sanity check: face_idx must be a valid index into m_face_normals.
    if (face_idx >= m_face_normals.size())
        return std::nullopt;

    // Proximity guard: if the closest point on the mesh is more than max_r mm away,
    // the query location is not on or near the surface, so no offset is applied.
    // max_r is currently set wide (30 mm) to cover nonplanar regions; tighten
    // this value once the feature is confirmed working end-to-end.
    const double max_r = 30.0;
    if (dist2 > max_r * max_r)
        return std::nullopt;
    BOOST_LOG_TRIVIAL(warning) << "NP surface_normal_at:"
        << " dist=" << std::sqrt(dist2)
        << " max_r=" << max_r
        << " face=" << face_idx
        << " normal=" << m_face_normals[face_idx].transpose();
    BOOST_LOG_TRIVIAL(warning) << "NP: dist2=" << dist2 
    << " max_r2=" << (max_r * max_r)
    << " face_idx=" << face_idx;
    
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

    // Avoid divide-by-zero on perfectly horizontal surfaces (nz near zero).
    if (std::abs(nz) < 1e-6)
        return 0.0;

    // Compute slope as tan(angle) = horizontal / vertical, then cap it at the
    // configured maximum slope to avoid toolpath collisions on steep geometry.
    const double max_slope = std::tan(m_cfg.max_slope_angle_deg * M_PI / 180.0);
    const double slope     = std::min(nxy / std::abs(nz), max_slope);
    // TEMP: exaggerate for testing, remove before shipping
    const double debug_scale = 10.0;  // exaggerate heavily
    return (nz > 0.0 ? +1.0 : -1.0) * slope * step_size * debug_scale;
}

// Returns the maximum Z deviation allowed per layer: 75% of layer height.
// Keeping the clamp below one full layer height prevents the nozzle from
// colliding with previously printed material on adjacent layers.
double NonplanarSurface::max_dz() const
{
    return m_cfg.layer_height * 0.75f;
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
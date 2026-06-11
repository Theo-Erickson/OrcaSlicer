#include "GLGizmoMove.hpp"
#include "../PlaneHandlePrefs.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
//BBS: GUI refactor
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"

#include <glad/gl.h>
#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

#if ENABLE_FIXED_GRABBER
const double GLGizmoMove3D::Offset = 50.0;
#else
const double GLGizmoMove3D::Offset = 10.0;
#endif

// ORCA: grabber IDs for plane handles
// 0 = X axis, 1 = Y axis, 2 = Z axis  (existing)
// 3 = YZ plane (locks X, free in Y+Z), color = AXES_COLOR[0] (red)
// 4 = XZ plane (locks Y, free in X+Z), color = AXES_COLOR[1] (green)
// 5 = XY plane (locks Z, free in X+Y), color = AXES_COLOR[2] (blue)
static constexpr int PLANE_ID_YZ = 3;
static constexpr int PLANE_ID_XZ = 4;
static constexpr int PLANE_ID_XY = 5;

// How far along each axis the plane square is placed, as a fraction of the
// bounding box half-size. 0.33 puts it roughly a third of the way out.
static constexpr double PLANE_OFFSET_FACTOR = 0.33;
// Side length of the rendered square in the same local units.
static constexpr double PLANE_SQUARE_SIZE   = 0.25;

//BBS: GUI refactor: add obj manipulation
GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_object_manipulation(obj_manipulation)
{}

std::string GLGizmoMove3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    if (m_hover_id == 0 || m_grabbers[0].dragging)
        return "X: " + format(show_position ? position(0) : m_displacement(0), 2);
    else if (m_hover_id == 1 || m_grabbers[1].dragging)
        return "Y: " + format(show_position ? position(1) : m_displacement(1), 2);
    else if (m_hover_id == 2 || m_grabbers[2].dragging)
        return "Z: " + format(show_position ? position(2) : m_displacement(2), 2);
    // ORCA: plane handle tooltips — show all 3 axes, locked axis in [brackets]
    else if (m_hover_id == PLANE_ID_YZ || m_grabbers[PLANE_ID_YZ].dragging) {
        // X is locked
        return "[X: " + format(show_position ? position(0) : m_displacement(0), 2) + "]\n"
             +  "Y: " + format(show_position ? position(1) : m_displacement(1), 2) + "\n"
             +  "Z: " + format(show_position ? position(2) : m_displacement(2), 2);
    }
    else if (m_hover_id == PLANE_ID_XZ || m_grabbers[PLANE_ID_XZ].dragging) {
        // Y is locked
        return  "X: " + format(show_position ? position(0) : m_displacement(0), 2) + "\n"
             + "[Y: " + format(show_position ? position(1) : m_displacement(1), 2) + "]\n"
             +  "Z: " + format(show_position ? position(2) : m_displacement(2), 2);
    }
    else if (m_hover_id == PLANE_ID_XY || m_grabbers[PLANE_ID_XY].dragging) {
        // Z is locked
        return  "X: " + format(show_position ? position(0) : m_displacement(0), 2) + "\n"
             +  "Y: " + format(show_position ? position(1) : m_displacement(1), 2) + "\n"
             + "[Z: " + format(show_position ? position(2) : m_displacement(2), 2) + "]";
    }
    else
        return "";
}

// ORCA: ray-triangle intersection helper (Möller-Trumbore)
static bool ray_intersects_quad(const Linef3& ray,
                                 const Vec3d& c0, const Vec3d& c1,
                                 const Vec3d& c2, const Vec3d& c3,
                                 double& t_out)
{
    // Test two triangles: (c0,c1,c2) and (c0,c2,c3)
    auto test_tri = [&](const Vec3d& v0, const Vec3d& v1, const Vec3d& v2) -> bool {
        const Vec3d dir = ray.unit_vector();
        const Vec3d e1  = v1 - v0;
        const Vec3d e2  = v2 - v0;
        const Vec3d h   = dir.cross(e2);
        const double a  = e1.dot(h);
        if (std::abs(a) < 1e-8) return false;
        const double f  = 1.0 / a;
        const Vec3d s   = ray.a - v0;
        const double u  = f * s.dot(h);
        if (u < 0.0 || u > 1.0) return false;
        const Vec3d q   = s.cross(e1);
        const double v  = f * dir.dot(q);
        if (v < 0.0 || u + v > 1.0) return false;
        t_out = f * e2.dot(q);
        return t_out > 1e-6;
    };
    return test_tri(c0, c1, c2) || test_tri(c0, c2, c3);
}

bool GLGizmoMove3D::on_mouse(const wxMouseEvent &mouse_event)
{
    // ORCA: manually hit-test the plane handle quads using the mouse ray.
    // We do this before use_grabbers so that if the mouse is over a plane
    // quad we can set m_hover_id and handle drag ourselves.
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Linef3  ray    = wxGetApp().plater()->canvas3D()->mouse_ray(Point(mouse_event.GetX(), mouse_event.GetY()));

    // Build world-space quad corners for each enabled plane handle.
    // Grabber center is in local (bounding-box) space; matrix transforms to world.
    auto world_quad = [&](int id, const Vec3d& u, const Vec3d& v, double sz,
                          Vec3d& c0, Vec3d& c1, Vec3d& c2, Vec3d& c3)
    {
        const Transform3d& m = m_grabbers[id].matrix;
        const Vec3d cw = m * m_grabbers[id].center;
        const Vec3d uw = (m.linear() * u).normalized() * sz;
        const Vec3d vw = (m.linear() * v).normalized() * sz;
        c0 = cw - uw - vw;
        c1 = cw + uw - vw;
        c2 = cw + uw + vw;
        c3 = cw - uw + vw;
    };

    // Quad half-size in world units: scale the local sz by the matrix scale factor
    const Vec3d hs = 0.5 * m_bounding_box.size();
    const double sz_local = std::max({ hs.x(), hs.y(), hs.z() }) * PLANE_SQUARE_SIZE;
    // Extract uniform scale from the matrix (use first column magnitude)
    const double world_scale = m_grabbers[0].matrix.linear().col(0).norm();
    const double size_scale_ht = std::max(0.0f, m_plane_prefs.size_pct) / 100.0f;
    const double sz = sz_local * world_scale * size_scale_ht;

    // Only hit-test if we are not already dragging an axis grabber
    const bool axis_dragging = m_dragging && m_hover_id >= 0 && m_hover_id < 3;
    if (!axis_dragging) {
        int  hit_plane_id = -1;
        double best_t     = std::numeric_limits<double>::max();

        struct PlaneTest { int id; Vec3d u, v; };
        const PlaneTest tests[3] = {
            { PLANE_ID_YZ, Vec3d::UnitY(), Vec3d::UnitZ() },
            { PLANE_ID_XZ, Vec3d::UnitX(), Vec3d::UnitZ() },
            { PLANE_ID_XY, Vec3d::UnitX(), Vec3d::UnitY() },
        };

        for (const auto& t : tests) {
            if (!m_grabbers[t.id].enabled) continue;
            Vec3d c0, c1, c2, c3;
            world_quad(t.id, t.u, t.v, sz, c0, c1, c2, c3);
            double hit_t = 0.0;
            if (ray_intersects_quad(ray, c0, c1, c2, c3, hit_t) && hit_t < best_t) {
                best_t      = hit_t;
                hit_plane_id = t.id;
            }
        }

        if (mouse_event.Moving()) {
            // Update hover — override whatever the cube raycaster set
            if (hit_plane_id != -1)
                set_hover_id(hit_plane_id);
            else if (m_hover_id >= PLANE_ID_YZ)
                set_hover_id(-1);
            // If hovering a plane handle, consume the event so the canvas
            // doesn't reset our hover_id to -1 via the normal raycaster path
            if (m_hover_id >= PLANE_ID_YZ) {
                set_dirty();
                return true;
            }
        }
        else if (mouse_event.LeftDown() && hit_plane_id != -1) {
            // Start dragging a plane handle.
            set_hover_id(hit_plane_id);
            m_plane_drag_start_hit = ray.a + best_t * ray.unit_vector();

            const auto& lin = m_grabbers[0].matrix.linear();
            if (hit_plane_id == PLANE_ID_YZ)
                m_plane_drag_normal = (lin * Vec3d::UnitX()).normalized();
            else if (hit_plane_id == PLANE_ID_XZ)
                m_plane_drag_normal = (lin * Vec3d::UnitY()).normalized();
            else
                m_plane_drag_normal = (lin * Vec3d::UnitZ()).normalized();

            // Refresh the selection's position cache so that set_relative
            // translate uses the object's *current* position as the baseline,
            // not the baseline from a previous drag. Without this the locked
            // axis snaps back to wherever it was at the start of the last drag.
            m_parent.get_selection().setup_cache();

            m_dragging = true;
            on_start_dragging();
            set_dirty();
            return true;
        }
        else if (mouse_event.LeftUp() && m_dragging && m_hover_id >= PLANE_ID_YZ) {
            // Stop dragging a plane handle
            on_stop_dragging();
            m_dragging = false;
            set_dirty();
            return true;
        }
        else if (mouse_event.Dragging() && m_dragging && m_hover_id >= PLANE_ID_YZ) {
            // Continue dragging a plane handle
            const UpdateData data(ray, { mouse_event.GetX(), mouse_event.GetY() });
            on_dragging(data);
            set_dirty();
            return true;
        }
    }

    return use_grabbers(mouse_event);
}

void GLGizmoMove3D::data_changed(bool is_serializing) {
    m_grabbers[2].enabled = !m_parent.get_selection().is_wipe_tower();
    // ORCA: keep plane handles in sync with Z axis availability
    m_grabbers[PLANE_ID_XZ].enabled = m_grabbers[2].enabled;
    m_grabbers[PLANE_ID_XY].enabled = m_grabbers[2].enabled;
    change_cs_by_selection();
}

bool GLGizmoMove3D::on_init()
{
    // Existing axis grabbers
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
        m_grabbers.back().extensions = GLGizmoBase::EGrabberExtension::PosZ;
    }

    m_grabbers[0].angles = { 0.0, 0.5 * double(PI), 0.0 };
    m_grabbers[1].angles = { -0.5 * double(PI), 0.0, 0.0 };

    // ORCA: plane handle grabbers (used only for picking registration;
    // rendering is done manually in render_plane_handles)
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
        // Plane grabbers don't use the cone extension
        m_grabbers.back().extensions = GLGizmoBase::EGrabberExtension::None;
    }

    m_shortcut_key = WXK_CONTROL_M;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Move") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Move");
    }
}

bool GLGizmoMove3D::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoMove3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
}

void GLGizmoMove3D::on_start_dragging()
{
    assert(m_hover_id != -1);

    m_displacement = Vec3d::Zero();
    m_prev_plane_displacement = Vec3d::Zero();
    m_drag_plane_model.reset(); // force rebuild for the newly active plane
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    m_starting_box_center        = box.center();
    m_starting_box_bottom_center = box.center();
    m_starting_box_bottom_center(2) = box.min(2);

    if (m_hover_id >= PLANE_ID_YZ) {
        // ORCA: plane drag anchor = the mouse-ray hit point captured in on_mouse
        // LeftDown, giving zero delta on the very first drag frame.
        // m_plane_drag_normal and m_plane_drag_start_hit are already set by on_mouse.
        m_starting_drag_position = m_plane_drag_start_hit;
    } else {
        m_starting_drag_position = m_grabbers[m_hover_id].matrix * m_grabbers[m_hover_id].center;
    }
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_parent.do_move(L("Gizmo-Move"));
    m_displacement = Vec3d::Zero();
}

void GLGizmoMove3D::on_dragging(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);
    // ORCA: plane-constrained movement.
    // calc_plane_projection returns a world-space delta. We project onto the
    // gizmo's local axes and set only the free-axis components of m_displacement.
    // The locked axis stays at 0 which is correct — set_relative translate
    // moves from the selection's cached start position, and the locked axis
    // starts at 0 displacement (no movement from cache on that axis).
    else if (m_hover_id == PLANE_ID_YZ) {
        Vec3d delta   = calc_plane_projection(data, m_plane_drag_normal);
        Vec3d local_y = (m_grabbers[0].matrix.linear() * Vec3d::UnitY()).normalized();
        Vec3d local_z = (m_grabbers[0].matrix.linear() * Vec3d::UnitZ()).normalized();
        m_displacement.y() = delta.dot(local_y);
        m_displacement.z() = delta.dot(local_z);
        // x stays 0: selection cache for this drag has x at the correct position
    }
    else if (m_hover_id == PLANE_ID_XZ) {
        Vec3d delta   = calc_plane_projection(data, m_plane_drag_normal);
        Vec3d local_x = (m_grabbers[0].matrix.linear() * Vec3d::UnitX()).normalized();
        Vec3d local_z = (m_grabbers[0].matrix.linear() * Vec3d::UnitZ()).normalized();
        m_displacement.x() = delta.dot(local_x);
        m_displacement.z() = delta.dot(local_z);
        // y stays 0
    }
    else if (m_hover_id == PLANE_ID_XY) {
        Vec3d delta   = calc_plane_projection(data, m_plane_drag_normal);
        Vec3d local_x = (m_grabbers[0].matrix.linear() * Vec3d::UnitX()).normalized();
        Vec3d local_y = (m_grabbers[0].matrix.linear() * Vec3d::UnitY()).normalized();
        m_displacement.x() = delta.dot(local_x);
        m_displacement.y() = delta.dot(local_y);
        // z stays 0
    }

    Selection& selection = m_parent.get_selection();
    TransformationType trafo_type;
    trafo_type.set_relative();
    switch (wxGetApp().obj_manipul()->get_coordinates_type())
    {
    case ECoordinatesType::Instance: { trafo_type.set_instance(); break; }
    case ECoordinatesType::Local:    { trafo_type.set_local();    break; }
    default: { break; }
    }
    selection.translate(m_displacement, trafo_type);
}

void GLGizmoMove3D::on_render()
{
    const Selection& selection = m_parent.get_selection();

    // ORCA: render the drag plane overlay BEFORE clearing depth so it
    // is occluded by the ground plane and other scene geometry, matching
    // how the model itself clips through the bed.
    if (m_dragging && m_hover_id >= PLANE_ID_YZ) {
        // We need base_matrix and bounding box for the overlay, but they are
        // computed below. Re-query here so the overlay has correct transforms.
        const auto& [box_pre, box_trafo_pre] = selection.get_bounding_box_in_current_reference_system();
        m_bounding_box = box_pre;
        render_drag_plane_overlay(box_trafo_pre);
    }

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    const auto &[box, box_trafo]  = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box                = box;
    m_center                      = box_trafo.translation();
    if (m_object_manipulation)
        m_object_manipulation->cs_center = box_trafo.translation();

    const Transform3d base_matrix = box_trafo;
    float space_size = 20.f * INV_ZOOM;

    for (int i = 0; i < 6; ++i)
        m_grabbers[i].matrix = base_matrix;

    const Vec3d zero = Vec3d::Zero();

    // Axis grabber positions (unchanged)
    m_grabbers[0].center = { m_bounding_box.max.x() + space_size, 0, 0 };
    m_grabbers[1].center = { 0, m_bounding_box.max.y() + space_size, 0 };
    m_grabbers[2].center = { 0, 0, m_bounding_box.max.z() + space_size };

    for (int i = 0; i < 3; ++i) {
        m_grabbers[i].color       = AXES_COLOR[i];
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i];
    }

    // ORCA: reload preferences each frame so Preferences dialog changes
    // take effect without restarting. Force a quad rebuild if anything changed.
    {
        const PlaneHandlePrefs new_prefs = PlaneHandlePrefs::load();
        if (new_prefs != m_plane_prefs) {
            m_plane_prefs = new_prefs;
            // Reset all quad models so rebuild_plane_quads() regenerates them
            for (auto& ph : m_plane_handles) {
                ph.quad_model.reset();
                ph.border_model.reset();
            }
        }
    }

    // ORCA: plane handle grabber positions, computed from the current style pref.
    // Each quad is centered on the corresponding bbox face, at the face midpoint.
    // This matches Blender's convention: the square sits just inside the face corner
    // formed by the two axis arrows, inset by a fixed fraction of the face size.
    {
        const Vec3d hs = 0.5 * m_bounding_box.size();
        // sz_world: rendered square half-size in local units, same formula as rebuild_plane_quads
        const double sz = std::max({ hs.x(), hs.y(), hs.z() }) * PLANE_SQUARE_SIZE
                          * (std::max(0.0f, m_plane_prefs.size_pct) / 100.0f);

        Vec3d pos_yz, pos_xz, pos_xy;

        switch (m_plane_prefs.position) {
        case PlaneHandlePosition::AtIntersection:
            // Near corner of the square aligns with the gizmo origin (0,0,0).
            // The square extends sz along each free axis, so its center is at (sz, sz)
            // from the origin, sitting on the bbox face along the constrained axis.
            pos_yz = { hs.x(), sz, sz };
            pos_xz = { sz,     hs.y(), sz };
            pos_xy = { sz,     sz,     hs.z() };
            break;
        case PlaneHandlePosition::Midpoint:
            // Center on the axis arrow, halfway between origin and arrow tip.
            // Arrow tip is at hs + space_size; midpoint is roughly at hs * 0.5.
            pos_yz = { hs.x(), hs.y() * 0.5, hs.z() * 0.5 };
            pos_xz = { hs.x() * 0.5, hs.y(), hs.z() * 0.5 };
            pos_xy = { hs.x() * 0.5, hs.y() * 0.5, hs.z() };
            break;
        default: // AtArrowEnd — centered on the bbox face, matching Blender
            pos_yz = { hs.x(), 0.0, 0.0 };
            pos_xz = { 0.0,    hs.y(), 0.0 };
            pos_xy = { 0.0,    0.0,    hs.z() };
            break;
        }

        m_grabbers[PLANE_ID_YZ].center = pos_yz;
        m_grabbers[PLANE_ID_XZ].center = pos_xz;
        m_grabbers[PLANE_ID_XY].center = pos_xy;

        m_grabbers[PLANE_ID_YZ].color       = AXES_COLOR[0];
        m_grabbers[PLANE_ID_YZ].hover_color = AXES_HOVER_COLOR[0];
        m_grabbers[PLANE_ID_XZ].color       = AXES_COLOR[1];
        m_grabbers[PLANE_ID_XZ].hover_color = AXES_HOVER_COLOR[1];
        m_grabbers[PLANE_ID_XY].color       = AXES_COLOR[2];
        m_grabbers[PLANE_ID_XY].hover_color = AXES_HOVER_COLOR[2];
    }

    // Rebuild the quad geometry whenever the bounding box changes
    rebuild_plane_quads();

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
#endif

    auto render_grabber_connection = [this, &zero](unsigned int id) {
        if (m_grabbers[id].enabled) {
            m_grabber_connections[id].old_center = m_grabbers[id].center;
            m_grabber_connections[id].model.reset();

            GLModel::Geometry init_data;
            init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
            init_data.color  = AXES_COLOR[id];
            init_data.reserve_vertices(2);
            init_data.reserve_indices(2);
            init_data.add_vertex((Vec3f)zero.cast<float>());
            init_data.add_vertex((Vec3f)m_grabbers[id].center.cast<float>());
            init_data.add_line(0, 1);
            m_grabber_connections[id].model.init_from(std::move(init_data));

#if !SLIC3R_OPENGL_ES
            if (!OpenGLManager::get_gl_info().is_core_profile()) {
                glLineStipple(1, 0x0FFF);
                glEnable(GL_LINE_STIPPLE);
            }
#endif
            m_grabber_connections[id].model.render();
#if !SLIC3R_OPENGL_ES
            if (!OpenGLManager::get_gl_info().is_core_profile())
                glDisable(GL_LINE_STIPPLE);
#endif
        }
    };

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile()
        ? wxGetApp().get_shader("dashed_thick_lines")
        : wxGetApp().get_shader("flat");
#endif
    if (shader != nullptr) {
        shader->start_using();
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * base_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif
            const std::array<int, 4>& viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.25f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif
        for (unsigned int i = 0; i < 3; ++i)
            render_grabber_connection(i);
        shader->stop_using();
    }

    // Draw axis grabbers only (0, 1, 2).
    // ORCA: temporarily disable plane grabbers so render_grabbers(box) doesn't
    // render a stray cube for each of them at the wrong position.
    m_grabbers[PLANE_ID_YZ].enabled = false;
    m_grabbers[PLANE_ID_XZ].enabled = false;
    m_grabbers[PLANE_ID_XY].enabled = false;
    render_grabbers(box);
    // Restore — plane handles are always enabled unless Z is locked (wipe tower).
    // data_changed() is the authoritative place for the wipe-tower check, so
    // here we just restore to enabled unconditionally.
    m_grabbers[PLANE_ID_YZ].enabled = true;
    m_grabbers[PLANE_ID_XZ].enabled = m_grabbers[2].enabled;
    m_grabbers[PLANE_ID_XY].enabled = m_grabbers[2].enabled;

    // ORCA: draw plane handle quads on top
    render_plane_handles(base_matrix);

    // ORCA: during an active plane drag, show the full constraint plane
    if (m_dragging && m_hover_id >= PLANE_ID_YZ)
        render_drag_plane_overlay(base_matrix);

    if (m_object_manipulation->is_instance_coordinates()) {
#if SLIC3R_OPENGL_ES
        GLShaderProgram* shader2 = wxGetApp().get_shader("dashed_lines");
#else
        GLShaderProgram* shader2 = OpenGLManager::get_gl_info().is_core_profile()
            ? wxGetApp().get_shader("dashed_thick_lines")
            : wxGetApp().get_shader("flat");
#endif
        if (shader2 != nullptr) {
            shader2->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();

            Geometry::Transformation cur_tran;
            if (auto mi = m_parent.get_selection().get_selected_single_intance())
                cur_tran = mi->get_transformation();
            else
                cur_tran = selection.get_first_volume()->get_instance_transformation();

            shader2->set_uniform("view_model_matrix", camera.get_view_matrix() * cur_tran.get_matrix());
            shader2->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader2->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader2->set_uniform("width", 0.5f);
                shader2->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif
            render_cross_mark(Vec3f::Zero(), true);
            shader2->stop_using();
        }
    }
}

void GLGizmoMove3D::on_register_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(true);
}

void GLGizmoMove3D::on_unregister_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(false);
}

void GLGizmoMove3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_move_window(m_imgui, "Move", x, y, bottom_limit);
}

// ---------------------------------------------------------------------------
// ORCA: plane handle geometry helpers
// ---------------------------------------------------------------------------

void GLGizmoMove3D::rebuild_plane_quads()
{
    // Each plane handle is a square centered on the grabber's center position.
    // We build both a filled quad (for picking and the semi-transparent fill)
    // and a line-loop border.
    //
    // All coordinates are in local (bounding-box) space; base_matrix is
    // applied at render time via the shader uniform.

    const Vec3d hs = 0.5 * m_bounding_box.size();
    // Apply size_pct preference: 100% = default size, 0-500% range
    const double size_scale = std::max(0.0f, m_plane_prefs.size_pct) / 100.0f;
    const double sz = std::max({ hs.x(), hs.y(), hs.z() }) * PLANE_SQUARE_SIZE * size_scale;

    // Number of segments for the circle approximation
    static constexpr int CIRCLE_SEGS = 32;
    
    // Lambda: build one plane handle quad.
    // c  = center in local space
    // u,v = two in-plane unit axes
    auto build_square = [&](PlaneHandle& ph, const Vec3d& c,
                             const Vec3d& u, const Vec3d& v,
                             const ColorRGBA& col)
    {
        // Corners of the square
        const Vec3d c0 = c - u * sz - v * sz;
        const Vec3d c1 = c + u * sz - v * sz;
        const Vec3d c2 = c + u * sz + v * sz;
        const Vec3d c3 = c - u * sz + v * sz;

        // Normal (for lighting, just use the cross product)
        const Vec3f n = (Vec3f)(u.cross(v).normalized().cast<float>());

        // --- filled quad (two triangles) ---
        {
            ph.quad_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                         GLModel::Geometry::EVertexLayout::P3N3 };
            ColorRGBA fill = col; fill.a(0.35f);
            g.color = fill;
            g.reserve_vertices(4); g.reserve_indices(6);
            g.add_vertex((Vec3f)c0.cast<float>(), n);
            g.add_vertex((Vec3f)c1.cast<float>(), n);
            g.add_vertex((Vec3f)c2.cast<float>(), n);
            g.add_vertex((Vec3f)c3.cast<float>(), n);
            g.add_triangle(0, 1, 2); g.add_triangle(0, 2, 3);
            ph.quad_model.init_from(std::move(g));
        }
        // Border
        {
            ph.border_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Lines,
                         GLModel::Geometry::EVertexLayout::P3 };
            g.color = col;
            g.reserve_vertices(4); g.reserve_indices(8);
            g.add_vertex((Vec3f)c0.cast<float>()); g.add_vertex((Vec3f)c1.cast<float>());
            g.add_vertex((Vec3f)c2.cast<float>()); g.add_vertex((Vec3f)c3.cast<float>());
            g.add_line(0,1); g.add_line(1,2); g.add_line(2,3); g.add_line(3,0);
            ph.border_model.init_from(std::move(g));
        }
    };

    auto build_circle = [&](PlaneHandle& ph, const Vec3d& c,
                             const Vec3d& u, const Vec3d& v,
                             const ColorRGBA& col)
    {
        const Vec3f normal = (Vec3f)(u.cross(v).normalized().cast<float>());

        // Fan-triangulated filled disc
        {
            ph.quad_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                         GLModel::Geometry::EVertexLayout::P3N3 };
            ColorRGBA fill = col; fill.a(0.35f);
            g.color = fill;
            g.reserve_vertices(CIRCLE_SEGS + 1);
            g.reserve_indices(CIRCLE_SEGS * 3);
            // Centre vertex
            g.add_vertex((Vec3f)c.cast<float>(), normal);
            for (int i = 0; i < CIRCLE_SEGS; ++i) {
                const double a = 2.0 * M_PI * i / CIRCLE_SEGS;
                const Vec3d  p = c + u * (sz * std::cos(a)) + v * (sz * std::sin(a));
                g.add_vertex((Vec3f)p.cast<float>(), normal);
            }
            for (int i = 0; i < CIRCLE_SEGS; ++i) {
                const int next = (i + 1) % CIRCLE_SEGS;
                g.add_triangle(0, i + 1, next + 1);
            }
            ph.quad_model.init_from(std::move(g));
        }

        // --- border (line loop) ---
        {
            ph.border_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Lines,
                         GLModel::Geometry::EVertexLayout::P3 };
            g.color = col;
            g.reserve_vertices(CIRCLE_SEGS);
            g.reserve_indices(CIRCLE_SEGS * 2);
            for (int i = 0; i < CIRCLE_SEGS; ++i) {
                const double a = 2.0 * M_PI * i / CIRCLE_SEGS;
                const Vec3d  p = c + u * (sz * std::cos(a)) + v * (sz * std::sin(a));
                g.add_vertex((Vec3f)p.cast<float>());
            }
            for (int i = 0; i < CIRCLE_SEGS; ++i)
                g.add_line(i, (i + 1) % CIRCLE_SEGS);
            ph.border_model.init_from(std::move(g));
        }
    };

    const bool use_circle = (m_plane_prefs.shape == PlaneHandleShape::Circle);
    auto build = [&](PlaneHandle& ph, const Vec3d& c,
                     const Vec3d& u, const Vec3d& v, const ColorRGBA& col) {
        if (use_circle) build_circle(ph, c, u, v, col);
        else            build_square(ph, c, u, v, col);
    };

    // Winding: u × v must point AWAY from bbox center (outward normal).
    // YZ: UnitY × UnitZ = +UnitX ✓
    build(m_plane_handles[0], m_grabbers[PLANE_ID_YZ].center,
          Vec3d::UnitY(), Vec3d::UnitZ(), m_grabbers[PLANE_ID_YZ].color);
    // XZ: UnitZ × UnitX = +UnitY ✓  (swap from naive UnitX,UnitZ)
    build(m_plane_handles[1], m_grabbers[PLANE_ID_XZ].center,
          Vec3d::UnitZ(), Vec3d::UnitX(), m_grabbers[PLANE_ID_XZ].color);
    // XY: UnitX × UnitY = +UnitZ ✓
    build(m_plane_handles[2], m_grabbers[PLANE_ID_XY].center,
          Vec3d::UnitX(), Vec3d::UnitY(), m_grabbers[PLANE_ID_XY].color);
}

void GLGizmoMove3D::render_plane_handles(const Transform3d& base_matrix)
{
    // Map plane handle index to the corresponding grabber ID so we can read
    // enabled / hover state.
    const int plane_grabber_ids[3] = { PLANE_ID_YZ, PLANE_ID_XZ, PLANE_ID_XY };

    const Camera& camera = wxGetApp().plater()->get_camera();

    // --- Filled semi-transparent quads ---
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("view_model_matrix",  camera.get_view_matrix() * base_matrix);
        shader->set_uniform("projection_matrix",  camera.get_projection_matrix());
        shader->set_uniform("emission_factor",    0.2f);

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        // Disable depth write so the transparent fill doesn't occlude other gizmo parts
        glsafe(::glDepthMask(GL_FALSE));
        // Double-sided: disable back-face culling so the quad is visible from
        // both sides regardless of viewing angle
        glsafe(::glDisable(GL_CULL_FACE));

        for (int i = 0; i < 3; ++i) {
            if (!m_grabbers[plane_grabber_ids[i]].enabled)
                continue;

            // If hovered, brighten the fill slightly
            if (m_hover_id == plane_grabber_ids[i]) {
                ColorRGBA hover_col = m_grabbers[plane_grabber_ids[i]].hover_color;
                hover_col.a(0.55f);
                m_plane_handles[i].quad_model.set_color(hover_col);
            } else {
                ColorRGBA base_col = m_grabbers[plane_grabber_ids[i]].color;
                base_col.a(0.35f);
                m_plane_handles[i].quad_model.set_color(base_col);
            }
            m_plane_handles[i].quad_model.render();
        }

        glsafe(::glDepthMask(GL_TRUE));
        glsafe(::glEnable(GL_CULL_FACE));
        glsafe(::glDisable(GL_BLEND));
        shader->stop_using();
    }

    // --- Opaque border lines ---
#if SLIC3R_OPENGL_ES
    GLShaderProgram* line_shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* line_shader = OpenGLManager::get_gl_info().is_core_profile()
        ? wxGetApp().get_shader("dashed_thick_lines")
        : wxGetApp().get_shader("flat");
#endif
    if (line_shader != nullptr) {
        line_shader->start_using();
        line_shader->set_uniform("view_model_matrix", camera.get_view_matrix() * base_matrix);
        line_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif
            const std::array<int, 4>& vp = camera.get_viewport();
            line_shader->set_uniform("viewport_size", Vec2d(double(vp[2]), double(vp[3])));
            line_shader->set_uniform("width",    1.5f);
            line_shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif

        for (int i = 0; i < 3; ++i) {
            if (!m_grabbers[plane_grabber_ids[i]].enabled)
                continue;

            const ColorRGBA& border_col = (m_hover_id == plane_grabber_ids[i])
                ? m_grabbers[plane_grabber_ids[i]].hover_color
                : m_grabbers[plane_grabber_ids[i]].color;
            m_plane_handles[i].border_model.set_color(border_col);
            m_plane_handles[i].border_model.render();
        }

        line_shader->stop_using();
    }
}

// ---------------------------------------------------------------------------
// ORCA: drag constraint plane overlay
// ---------------------------------------------------------------------------

void GLGizmoMove3D::render_drag_plane_overlay(const Transform3d& base_matrix)
{
    // Build a large quad spanning the full bbox face for the active drag plane.
    // Rebuilt when bbox or active plane changes.
    const Vec3d hs = 0.5 * m_bounding_box.size();

    if (!m_drag_plane_model.is_initialized() || !m_drag_plane_last_hs.isApprox(hs)) {
        m_drag_plane_last_hs = hs;
        m_drag_plane_model.reset();

        // Determine which plane is active and build its full-face quad.
        // We use a large enough quad to span the visible area; 3× the bbox
        // half-size on each free axis gives a generous visible extent.
        const double ext = 3.0;
        Vec3d c, u, v;
        Vec3f n;
        ColorRGBA col;

        if (m_hover_id == PLANE_ID_YZ) {
            c = { hs.x(), 0.0, 0.0 };
            u = Vec3d::UnitY(); v = Vec3d::UnitZ(); n = Vec3f::UnitX();
            col = AXES_COLOR[0];
        } else if (m_hover_id == PLANE_ID_XZ) {
            c = { 0.0, hs.y(), 0.0 };
            u = Vec3d::UnitZ(); v = Vec3d::UnitX(); n = Vec3f::UnitY();
            col = AXES_COLOR[1];
        } else {
            c = { 0.0, 0.0, hs.z() };
            u = Vec3d::UnitX(); v = Vec3d::UnitY(); n = Vec3f::UnitZ();
            col = AXES_COLOR[2];
        }

        const double eu = std::max({ hs.x(), hs.y(), hs.z() }) * ext;

        const Vec3d p0 = c - u * eu - v * eu;
        const Vec3d p1 = c + u * eu - v * eu;
        const Vec3d p2 = c + u * eu + v * eu;
        const Vec3d p3 = c - u * eu + v * eu;

        GLModel::Geometry g;
        g.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                     GLModel::Geometry::EVertexLayout::P3N3 };
        col.a(0.12f); // base alpha — overridden at render time by drag_plane_opacity pref
        g.color = col;
        g.reserve_vertices(4); g.reserve_indices(6);
        g.add_vertex((Vec3f)p0.cast<float>(), n);
        g.add_vertex((Vec3f)p1.cast<float>(), n);
        g.add_vertex((Vec3f)p2.cast<float>(), n);
        g.add_vertex((Vec3f)p3.cast<float>(), n);
        g.add_triangle(0, 1, 2); g.add_triangle(0, 2, 3);
        m_drag_plane_model.init_from(std::move(g));
    }

    const Camera& camera = wxGetApp().plater()->get_camera();
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (!shader) return;

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * base_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("emission_factor", 0.3f);

    // Apply opacity from prefs without rebuilding geometry
    {
        ColorRGBA c = m_drag_plane_model.get_color();
        c.a(m_plane_prefs.drag_plane_opacity);
        m_drag_plane_model.set_color(c);
    }

    // Render with full depth test so the plane is clipped by the ground
    // plane and other scene geometry (same depth buffer state the scene
    // left behind before on_render cleared it).
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_FALSE));   // don't write depth so scene isn't polluted
    glsafe(::glDisable(GL_CULL_FACE));

    m_drag_plane_model.render();

    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));
    shader->stop_using();
}



Vec3d GLGizmoMove3D::calc_plane_projection(const UpdateData& data,
                                            const Vec3d& plane_normal) const
{
    // Find where the mouse ray intersects the plane that passes through the
    // drag start position and has the given normal.
    // Uses the standard ray-plane formula:
    //   t = (n · (p0 - a)) / (n · d)
    //   intersection = a + t * d
    const Vec3d mouse_dir = data.mouse_ray.unit_vector();
    const double denom = plane_normal.dot(mouse_dir);

    // Guard: ray nearly parallel to plane — return zero delta
    if (std::abs(denom) < 1e-6)
        return Vec3d::Zero();

    const double t = plane_normal.dot(m_starting_drag_position - data.mouse_ray.a) / denom;
    const Vec3d intersection = data.mouse_ray.a + t * mouse_dir;

    // Raw delta from drag start to current intersection point
    Vec3d delta = intersection - m_starting_drag_position;

    // Optional shift-snap: snap the magnitude to m_snap_step
    if (wxGetKeyState(WXK_SHIFT)) {
        double mag = delta.norm();
        if (mag > 1e-6) {
            mag = m_snap_step * std::round(mag / m_snap_step);
            delta = delta.normalized() * mag;
        }
    }

    return delta;
}

// ---------------------------------------------------------------------------
// Existing helpers (unchanged)
// ---------------------------------------------------------------------------

double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    const Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    const double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = data.mouse_ray.unit_vector();
        const Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) * mouse_dir;
        const Vec3d inters_vec = inters - m_starting_drag_position;
        projection = inters_vec.dot(starting_vec.normalized());
    }

    if (wxGetKeyState(WXK_SHIFT))
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}

void GLGizmoMove3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx)
        return;
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_use_object_cs(false);
    } else if (model_volume) {
        m_object_manipulation->set_use_object_cs(true);
    } else {
        m_object_manipulation->set_use_object_cs(false);
    }
    if (m_object_manipulation->get_use_object_cs())
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Instance);
    else
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
}

} // namespace GUI
} // namespace Slic3r
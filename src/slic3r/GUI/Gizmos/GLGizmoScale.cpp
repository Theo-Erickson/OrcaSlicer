#include "GLGizmoScale.hpp"
#include "PlaneHandlePrefs.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <glad/gl.h>
#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

const float GLGizmoScale3D::Offset = 5.0f;

// ORCA: plane handle grabber IDs (appended after existing 0-9)
// 10 = YZ plane scale (locks X, scales Y+Z together)
// 11 = XZ plane scale (locks Y, scales X+Z together)
// 12 = XY plane scale (locks Z, scales X+Y together)
static constexpr int SCALE_PLANE_ID_YZ = 10;
static constexpr int SCALE_PLANE_ID_XZ = 11;
static constexpr int SCALE_PLANE_ID_XY = 12;

// Placement: fraction of bbox half-size inward from the face where the square sits
static constexpr double SCALE_PLANE_OFFSET_FACTOR = 0.4;
// Square half-side as fraction of mean bbox extent
static constexpr double SCALE_PLANE_SQUARE_SIZE   = 0.20;

// get intersection of ray and plane (retained from original)
Vec3d GetIntersectionOfRayAndPlane(Vec3d ray_position, Vec3d ray_dir, Vec3d plane_position, Vec3d plane_normal)
{
    double t = (plane_normal.dot(plane_position) - plane_normal.dot(ray_position)) / (plane_normal.dot(ray_dir));
    return ray_position + t * ray_dir;
}

//BBS: GUI refactor: add obj manipulation
GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_offset(Vec3d::Zero())
    , m_snap_step(0.05)
    , m_object_manipulation(obj_manipulation)
{
    m_grabber_connections[0].grabber_indices = { 0, 1 };
    m_grabber_connections[1].grabber_indices = { 2, 3 };
    m_grabber_connections[2].grabber_indices = { 4, 5 };
    m_grabber_connections[3].grabber_indices = { 6, 7 };
    m_grabber_connections[4].grabber_indices = { 7, 8 };
    m_grabber_connections[5].grabber_indices = { 8, 9 };
    m_grabber_connections[6].grabber_indices = { 9, 6 };
}

const Vec3d& GLGizmoScale3D::get_scale()
{
    if (m_object_manipulation) {
        Vec3d cache_scale = m_object_manipulation->get_cache().scale.cwiseQuotient(Vec3d(100, 100, 100));
        Vec3d temp_scale  = cache_scale.cwiseProduct(m_scale);
        m_object_manipulation->limit_scaling_ratio(temp_scale);
        m_scale = temp_scale.cwiseQuotient(cache_scale);
    }
    return m_scale;
}

std::string GLGizmoScale3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();

    bool single_instance = selection.is_single_full_instance();
    bool single_volume   = selection.is_single_volume_or_modifier();

    Vec3f scale = 100.0f * Vec3f::Ones();
    if (single_instance)
        scale = 100.0f * selection.get_first_volume()->get_instance_scaling_factor().cast<float>();
    else if (single_volume)
        scale = 100.0f * selection.get_first_volume()->get_volume_scaling_factor().cast<float>();

    if (m_hover_id == 0 || m_hover_id == 1 || m_grabbers[0].dragging || m_grabbers[1].dragging)
        return "X: " + format(scale.x(), 4) + "%";
    else if (m_hover_id == 2 || m_hover_id == 3 || m_grabbers[2].dragging || m_grabbers[3].dragging)
        return "Y: " + format(scale.y(), 4) + "%";
    else if (m_hover_id == 4 || m_hover_id == 5 || m_grabbers[4].dragging || m_grabbers[5].dragging)
        return "Z: " + format(scale.z(), 4) + "%";
    else if (m_hover_id == 6 || m_hover_id == 7 || m_hover_id == 8 || m_hover_id == 9 ||
             m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging) {
        return "X: " + format(scale.x(), 2) + "%\nY: " + format(scale.y(), 2) + "%\nZ: " + format(scale.z(), 2) + "%";
    }
    // ORCA: plane scale tooltips — show all 3 axes, locked axis in [brackets]
    else if (m_hover_id == SCALE_PLANE_ID_YZ || m_grabbers[SCALE_PLANE_ID_YZ].dragging) {
        // X is locked
        return "[X: " + format(scale.x(), 2) + "%]\n"
             +  "Y: " + format(scale.y(), 2) + "%\n"
             +  "Z: " + format(scale.z(), 2) + "%";
    }
    else if (m_hover_id == SCALE_PLANE_ID_XZ || m_grabbers[SCALE_PLANE_ID_XZ].dragging) {
        // Y is locked
        return  "X: " + format(scale.x(), 2) + "%\n"
             + "[Y: " + format(scale.y(), 2) + "%]\n"
             +  "Z: " + format(scale.z(), 2) + "%";
    }
    else if (m_hover_id == SCALE_PLANE_ID_XY || m_grabbers[SCALE_PLANE_ID_XY].dragging) {
        // Z is locked
        return  "X: " + format(scale.x(), 2) + "%\n"
             +  "Y: " + format(scale.y(), 2) + "%\n"
             + "[Z: " + format(scale.z(), 2) + "%]";
    }
    else
        return "";
}

// ORCA: ray-triangle intersection (Möller-Trumbore) — shared logic
static bool scale_ray_intersects_quad(const Linef3& ray,
                                       const Vec3d& c0, const Vec3d& c1,
                                       const Vec3d& c2, const Vec3d& c3,
                                       double& t_out)
{
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

bool GLGizmoScale3D::on_mouse(const wxMouseEvent &mouse_event)
{
    // ORCA: keep mouse position current for snap tick proximity testing
    m_last_mouse_pos = { mouse_event.GetX(), mouse_event.GetY() };

    // ORCA: apply scale during drag (existing logic, runs for all grabbers)
    if (mouse_event.Dragging() && m_dragging) {
        // For plane-handle drags, on_dragging is called below. For axis/uniform
        // grabs the existing path via use_grabbers handles it. We apply the
        // scale transform here only for non-plane drags to avoid double-apply.
        if (m_hover_id < SCALE_PLANE_ID_YZ) {
            TransformationType transformation_type;
            if (wxGetApp().obj_manipul()->is_local_coordinates())
                transformation_type.set_local();
            else if (wxGetApp().obj_manipul()->is_instance_coordinates())
                transformation_type.set_instance();
            transformation_type.set_relative();
            if (mouse_event.AltDown())
                transformation_type.set_independent();
            Selection& selection = m_parent.get_selection();
            selection.scale_and_translate(get_scale(), get_offset(), transformation_type);
        }
    }

    // ORCA: manually hit-test plane handle quads
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Linef3  ray    = wxGetApp().plater()->canvas3D()->mouse_ray(Point(mouse_event.GetX(), mouse_event.GetY()));

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

    const Vec3d hs   = 0.5 * m_bounding_box.size();
    const double mean = (hs.x() + hs.y() + hs.z()) / 3.0;
    const double world_scale = m_grabbers[0].matrix.linear().col(0).norm();
    const double size_scale_om = std::max(0.0f, m_plane_prefs.size_pct) / 100.0f;
    const double sz  = mean * SCALE_PLANE_SQUARE_SIZE * world_scale * size_scale_om;

    const bool axis_dragging = m_dragging && m_hover_id >= 0 && m_hover_id < SCALE_PLANE_ID_YZ;
    if (!axis_dragging) {
        int    hit_plane_id = -1;
        double best_t       = std::numeric_limits<double>::max();

        struct PlaneTest { int id; Vec3d u, v; };
        const PlaneTest tests[3] = {
            { SCALE_PLANE_ID_YZ, Vec3d::UnitY(), Vec3d::UnitZ() },
            { SCALE_PLANE_ID_XZ, Vec3d::UnitX(), Vec3d::UnitZ() },
            { SCALE_PLANE_ID_XY, Vec3d::UnitX(), Vec3d::UnitY() },
        };

        for (const auto& t : tests) {
            if (!m_grabbers[t.id].enabled) continue;
            Vec3d c0, c1, c2, c3;
            world_quad(t.id, t.u, t.v, sz, c0, c1, c2, c3);
            double hit_t = 0.0;
            if (scale_ray_intersects_quad(ray, c0, c1, c2, c3, hit_t) && hit_t < best_t) {
                best_t       = hit_t;
                hit_plane_id = t.id;
            }
        }

        if (mouse_event.Moving()) {
            if (hit_plane_id != -1)
                set_hover_id(hit_plane_id);
            else if (m_hover_id >= SCALE_PLANE_ID_YZ)
                set_hover_id(-1);
            if (m_hover_id >= SCALE_PLANE_ID_YZ) {
                set_dirty();
                return true;
            }
        }
        else if (mouse_event.LeftDown() && hit_plane_id != -1) {
            set_hover_id(hit_plane_id);
            // Refresh selection cache so set_relative scale uses current
            // position as baseline, not the previous drag's baseline.
            m_parent.get_selection().setup_cache();
            m_dragging = true;
            on_start_dragging();
            set_dirty();
            return true;
        }
        else if (mouse_event.LeftUp() && m_dragging && m_hover_id >= SCALE_PLANE_ID_YZ) {
            on_stop_dragging();
            m_dragging = false;
            set_dirty();
            return true;
        }
        else if (mouse_event.Dragging() && m_dragging && m_hover_id >= SCALE_PLANE_ID_YZ) {
            const UpdateData data(ray, { mouse_event.GetX(), mouse_event.GetY() });
            on_dragging(data);
            // Apply the scale to the selection.
            // ORCA: use m_scale directly instead of get_scale() to bypass
            // limit_scaling_ratio, which would clamp non-uniform plane scales
            // at 100% when Uniform scale is checked.
            TransformationType transformation_type;
            if (wxGetApp().obj_manipul()->is_local_coordinates())
                transformation_type.set_local();
            else if (wxGetApp().obj_manipul()->is_instance_coordinates())
                transformation_type.set_instance();
            transformation_type.set_relative();
            m_parent.get_selection().scale_and_translate(m_scale, get_offset(), transformation_type);
            set_dirty();
            return true;
        }
    }

    return use_grabbers(mouse_event);
}

void GLGizmoScale3D::data_changed(bool is_serializing)
{
    const Selection& selection        = m_parent.get_selection();
    bool             enable_scale_xyz = selection.is_single_full_instance() ||
                                        selection.is_single_volume_or_modifier();
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable_scale_xyz;

    for (int i = SCALE_PLANE_ID_YZ; i <= SCALE_PLANE_ID_XY; ++i)
        m_grabbers[i].enabled = enable_scale_xyz;

    set_scale(Vec3d::Ones());
    // ORCA: rebuild ticks on next render
    m_ticks_built = false;
    change_cs_by_selection();
}

void GLGizmoScale3D::enable_ununiversal_scale(bool enable)
{
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable;
    // ORCA: keep plane handles in sync
    for (int i = SCALE_PLANE_ID_YZ; i <= SCALE_PLANE_ID_XY; ++i)
        m_grabbers[i].enabled = enable;
}

bool GLGizmoScale3D::on_init()
{
    // Existing 10 grabbers
    for (int i = 0; i < 10; ++i)
        m_grabbers.push_back(Grabber());

    // ORCA: 3 plane handle grabbers (picking only, rendering is manual)
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
        m_grabbers.back().extensions = GLGizmoBase::EGrabberExtension::None;
    }

    m_grabbers[4].enabled = false;
    m_shortcut_key = WXK_CONTROL_S;

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off)
        return _u8L("Scale") + ":\n" + _u8L("Please select at least one object.");
    return _u8L("Scale");
}

bool GLGizmoScale3D::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower();
}

void GLGizmoScale3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
}

static int constraint_id(int grabber_id)
{
    static const std::vector<int> id_map = { 1, 0, 3, 2, 5, 4, 8, 9, 6, 7 };
    return (0 <= grabber_id && grabber_id < (int)id_map.size()) ? id_map[grabber_id] : -1;
}

void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id == -1)
        return;

    auto grabbers_transform    = m_grabbers_tran.get_matrix();
    m_starting.drag_position   = grabbers_transform * m_grabbers[m_hover_id].center;
    m_starting.plane_center    = grabbers_transform * m_grabbers[4].center;
    m_starting.plane_nromal    = (grabbers_transform * m_grabbers[5].center
                                 - grabbers_transform * m_grabbers[4].center).normalized();
    m_starting.ctrl_down       = wxGetKeyState(WXK_CONTROL);
    m_starting.box             = m_bounding_box;
    m_starting.center          = m_center;
    m_starting.instance_center = m_instance_center;

    const Vec3d box_half_size = 0.5 * m_bounding_box.size();
    m_starting.local_pivots[0] = Vec3d( box_half_size.x(), 0.0, -box_half_size.z());
    m_starting.local_pivots[1] = Vec3d(-box_half_size.x(), 0.0, -box_half_size.z());
    m_starting.local_pivots[2] = Vec3d(0.0,  box_half_size.y(), -box_half_size.z());
    m_starting.local_pivots[3] = Vec3d(0.0, -box_half_size.y(), -box_half_size.z());
    m_starting.local_pivots[4] = Vec3d(0.0, 0.0,  box_half_size.z());
    m_starting.local_pivots[5] = Vec3d(0.0, 0.0, -box_half_size.z());
    for (size_t i = 0; i < 6; i++)
        m_starting.pivots[i] = grabbers_transform * m_starting.local_pivots[i];

    m_starting.constraint_position = grabbers_transform * m_grabbers[constraint_id(m_hover_id)].center;
    m_scale   = m_starting.scale = Vec3d::Ones();
    m_offset  = Vec3d::Zero();

    // ORCA: capture the plane normal for plane-scale drags
    if (m_hover_id == SCALE_PLANE_ID_YZ)
        m_plane_drag_normal = (grabbers_transform.linear() * Vec3d::UnitX()).normalized();
    else if (m_hover_id == SCALE_PLANE_ID_XZ)
        m_plane_drag_normal = (grabbers_transform.linear() * Vec3d::UnitY()).normalized();
    else if (m_hover_id == SCALE_PLANE_ID_XY)
        m_plane_drag_normal = (grabbers_transform.linear() * Vec3d::UnitZ()).normalized();
}

void GLGizmoScale3D::on_stop_dragging()
{
    m_parent.do_scale(L("Gizmo-Scale"));
    m_starting.ctrl_down = false;
    m_snap_ticks.clear_snap();
}

void GLGizmoScale3D::on_dragging(const UpdateData& data)
{
    if      ((m_hover_id == 0) || (m_hover_id == 1)) do_scale_along_axis(X, data);
    else if ((m_hover_id == 2) || (m_hover_id == 3)) do_scale_along_axis(Y, data);
    else if ((m_hover_id == 4) || (m_hover_id == 5)) do_scale_along_axis(Z, data);
    else if (m_hover_id >= 6 && m_hover_id <= 9)     do_scale_uniform(data);
    else if (m_hover_id == SCALE_PLANE_ID_YZ)        do_scale_on_plane(X, data);
    else if (m_hover_id == SCALE_PLANE_ID_XZ)        do_scale_on_plane(Y, data);
    else if (m_hover_id == SCALE_PLANE_ID_XY)        do_scale_on_plane(Z, data);
    // ORCA: scale tick snap override -- to be reimplemented with new tick API
}

void GLGizmoScale3D::update_grabbers_data()
{
    const Selection& selection = m_parent.get_selection();
    const auto& [box, box_trafo] = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box    = box;
    m_center          = box_trafo.translation();
    m_grabbers_tran.set_matrix(box_trafo);
    m_instance_center = (selection.is_single_full_instance() || selection.is_single_volume_or_modifier())
        ? selection.get_first_volume()->get_instance_offset()
        : m_center;

    const Vec3d box_half_size = 0.5 * m_bounding_box.size();
    bool        ctrl_down     = wxGetKeyState(WXK_CONTROL);

    // --- existing axis + uniform grabbers (unchanged) ---
    m_grabbers[0].center = Vec3d(-box_half_size.x(), 0.0, -box_half_size.z());
    m_grabbers[0].color  = (ctrl_down && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = Vec3d( box_half_size.x(), 0.0, -box_half_size.z());
    m_grabbers[1].color  = (ctrl_down && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];

    m_grabbers[2].center = Vec3d(0.0, -box_half_size.y(), -box_half_size.z());
    m_grabbers[2].color  = (ctrl_down && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = Vec3d(0.0,  box_half_size.y(), -box_half_size.z());
    m_grabbers[3].color  = (ctrl_down && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];

    m_grabbers[4].center  = Vec3d(0.0, 0.0, -box_half_size.z());
    m_grabbers[4].enabled = false;
    m_grabbers[5].center  = Vec3d(0.0, 0.0,  box_half_size.z());
    m_grabbers[5].color   = (ctrl_down && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];

    m_grabbers[6].center = Vec3d(-box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[6].color  = (ctrl_down && m_hover_id == 8) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[7].center = Vec3d( box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[7].color  = (ctrl_down && m_hover_id == 9) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[8].center = Vec3d( box_half_size.x(),  box_half_size.y(), -box_half_size.z());
    m_grabbers[8].color  = (ctrl_down && m_hover_id == 6) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[9].center = Vec3d(-box_half_size.x(),  box_half_size.y(), -box_half_size.z());
    m_grabbers[9].color  = (ctrl_down && m_hover_id == 7) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;

    for (int i = 0; i < 6; ++i)
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i / 2];
    for (int i = 6; i < 10; ++i)
        m_grabbers[i].hover_color = GRABBER_UNIFORM_HOVER_COL;

    for (int i = 0; i < 10; ++i)
        m_grabbers[i].matrix = m_grabbers_tran.get_matrix();

    // ORCA: plane handle grabber centers, driven by position preference.
    {
        const double mean = (box_half_size.x() + box_half_size.y() + box_half_size.z()) / 3.0;
        const double sz   = mean * SCALE_PLANE_SQUARE_SIZE;

        Vec3d pos_yz, pos_xz, pos_xy;
        switch (m_plane_prefs.position) {
        case PlaneHandlePosition::AtIntersection:
            // Near corner of square at gizmo origin, extends sz along each free axis
            pos_yz = { box_half_size.x(), sz, sz };
            pos_xz = { sz, box_half_size.y(), sz };
            pos_xy = { sz, sz, box_half_size.z() };
            break;
        case PlaneHandlePosition::Midpoint:
            pos_yz = { box_half_size.x(), box_half_size.y() * 0.5, box_half_size.z() * 0.5 };
            pos_xz = { box_half_size.x() * 0.5, box_half_size.y(), box_half_size.z() * 0.5 };
            pos_xy = { box_half_size.x() * 0.5, box_half_size.y() * 0.5, box_half_size.z() };
            break;
        default: // AtArrowEnd — centered on face
            pos_yz = { box_half_size.x(), 0.0, 0.0 };
            pos_xz = { 0.0, box_half_size.y(), 0.0 };
            pos_xy = { 0.0, 0.0, box_half_size.z() };
            break;
        }

        m_grabbers[SCALE_PLANE_ID_YZ].center     = pos_yz;
        m_grabbers[SCALE_PLANE_ID_YZ].color       = AXES_COLOR[0];
        m_grabbers[SCALE_PLANE_ID_YZ].hover_color = AXES_HOVER_COLOR[0];
        m_grabbers[SCALE_PLANE_ID_XZ].center     = pos_xz;
        m_grabbers[SCALE_PLANE_ID_XZ].color       = AXES_COLOR[1];
        m_grabbers[SCALE_PLANE_ID_XZ].hover_color = AXES_HOVER_COLOR[1];
        m_grabbers[SCALE_PLANE_ID_XY].center     = pos_xy;
        m_grabbers[SCALE_PLANE_ID_XY].color       = AXES_COLOR[2];
        m_grabbers[SCALE_PLANE_ID_XY].hover_color = AXES_HOVER_COLOR[2];
    }

    for (int i = SCALE_PLANE_ID_YZ; i <= SCALE_PLANE_ID_XY; ++i)
        m_grabbers[i].matrix = m_grabbers_tran.get_matrix();
}

void GLGizmoScale3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume* model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx)
        return;
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object())
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    else if (model_volume)
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Local);
}

void GLGizmoScale3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    // ORCA: reload prefs each frame, force rebuild if changed
    {
        const PlaneHandlePrefs new_prefs = PlaneHandlePrefs::load();
        if (new_prefs != m_plane_prefs) {
            m_plane_prefs = new_prefs;
            for (auto& ph : m_plane_handles) {
                ph.quad_model.reset();
                ph.border_model.reset();
            }
        }
    }

    update_grabbers_data();

    // Rebuild plane quad geometry after grabber positions are updated
    rebuild_plane_quads();

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
#endif

    const float grabber_mean_size = (float)((m_bounding_box.size().x() + m_bounding_box.size().y() + m_bounding_box.size().z()) / 3.0);

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
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_tran.get_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif
            const std::array<int, 4>& viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width",    0.25f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif
        if (m_grabbers[4].enabled && m_grabbers[5].enabled)
            render_grabbers_connection(4, 5, m_grabbers[4].color);
        render_grabbers_connection(6, 7, m_grabbers[2].color);
        render_grabbers_connection(7, 8, m_grabbers[2].color);
        render_grabbers_connection(8, 9, m_grabbers[0].color);
        render_grabbers_connection(9, 6, m_grabbers[0].color);
        shader->stop_using();
    }

    // Draw existing grabbers (0-9 only).
    // ORCA: disable plane grabbers so render_grabbers(size) doesn't draw
    // stray cubes for them at incorrect positions.
    m_grabbers[SCALE_PLANE_ID_YZ].enabled = false;
    m_grabbers[SCALE_PLANE_ID_XZ].enabled = false;
    m_grabbers[SCALE_PLANE_ID_XY].enabled = false;
    shader = wxGetApp().get_shader("gouraud_light");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("emission_factor", 0.1f);
        render_grabbers(grabber_mean_size);
        shader->stop_using();
    }
    // Restore: use grabber[0] as representative of the per-axis enabled state.
    // data_changed() / enable_ununiversal_scale() are the authoritative setters.
    {
        bool axis_enabled = m_grabbers[0].enabled;
        m_grabbers[SCALE_PLANE_ID_YZ].enabled = axis_enabled;
        m_grabbers[SCALE_PLANE_ID_XZ].enabled = axis_enabled;
        m_grabbers[SCALE_PLANE_ID_XY].enabled = axis_enabled;
    }

    // ORCA: draw plane handle quads
    render_plane_handles(m_grabbers_tran.get_matrix());

    // ORCA: snap tick rendering and dwell timer
    if (m_plane_prefs.ticks_enabled) {
        const auto  now = std::chrono::steady_clock::now();
        const float dt  = m_ticks_built
            ? std::chrono::duration<float>(now - m_last_render_time).count()
            : 0.0f;
        m_last_render_time = now;

        // Rebuild scale ticks once on activation
        if (!m_ticks_built) {
            const Vec3d world_pos = m_parent.get_selection().get_bounding_box().center();
            const bool off_bed = world_pos.z() > 0.1;
            m_snap_ticks.build_scale_ticks(m_bounding_box, off_bed);
            m_ticks_built = true;
        }

        // Update proximity and timer
        m_snap_ticks.update_proximity(m_last_mouse_pos,
                                      wxGetApp().plater()->get_camera());
        if (m_snap_ticks.update_timer(dt))
            set_dirty();

        // Render ticks in world space
        m_snap_ticks.render(wxGetApp().plater()->get_camera(),
                            m_plane_prefs.tick_style);

        // Tooltip
        const SnapTick* ht = m_snap_ticks.hovered_tick();
        if (ht) m_parent.set_tooltip(ht->label + "\n(click to snap)");
    }
}

void GLGizmoScale3D::on_register_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(true);
}

void GLGizmoScale3D::on_unregister_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(false);
}

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color)
{
    auto grabber_connection = [this](unsigned int id_1, unsigned int id_2) {
        for (int i = 0; i < int(m_grabber_connections.size()); ++i)
            if (m_grabber_connections[i].grabber_indices.first == id_1 &&
                m_grabber_connections[i].grabber_indices.second == id_2)
                return i;
        return -1;
    };

    const int id = grabber_connection(id_1, id_2);
    if (id == -1) return;

    if (!m_grabber_connections[id].model.is_initialized() ||
        !m_grabber_connections[id].old_v1.isApprox(m_grabbers[id_1].center) ||
        !m_grabber_connections[id].old_v2.isApprox(m_grabbers[id_2].center)) {

        m_grabber_connections[id].old_v1 = m_grabbers[id_1].center;
        m_grabber_connections[id].old_v2 = m_grabbers[id_2].center;
        m_grabber_connections[id].model.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);
        init_data.add_vertex((Vec3f)m_grabbers[id_1].center.cast<float>());
        init_data.add_vertex((Vec3f)m_grabbers[id_2].center.cast<float>());
        init_data.add_line(0, 1);
        m_grabber_connections[id].model.init_from(std::move(init_data));
    }

    m_grabber_connections[id].model.set_color(color);
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

void GLGizmoScale3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_scale_input_window(m_imgui, "Scale", x, y, bottom_limit);
}

// ---------------------------------------------------------------------------
// ORCA: plane handle geometry helpers
// ---------------------------------------------------------------------------

void GLGizmoScale3D::rebuild_plane_quads()
{
    const Vec3d hs    = 0.5 * m_bounding_box.size();
    const double mean = (hs.x() + hs.y() + hs.z()) / 3.0;
    const double size_scale = std::max(0.0f, m_plane_prefs.size_pct) / 100.0f;
    const double sz   = mean * SCALE_PLANE_SQUARE_SIZE * size_scale;
    static constexpr int CIRCLE_SEGS = 32;

    auto build_square = [&](PlaneHandle& ph, const Vec3d& c,
                             const Vec3d& u, const Vec3d& v,
                             const ColorRGBA& col)
    {
        const Vec3d c0 = c - u*sz - v*sz, c1 = c + u*sz - v*sz;
        const Vec3d c2 = c + u*sz + v*sz, c3 = c - u*sz + v*sz;
        const Vec3f n  = (Vec3f)(u.cross(v).normalized().cast<float>());
        {
            ph.quad_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                         GLModel::Geometry::EVertexLayout::P3N3 };
            ColorRGBA fill = col; fill.a(0.35f); g.color = fill;
            g.reserve_vertices(4); g.reserve_indices(6);
            g.add_vertex((Vec3f)c0.cast<float>(), n); g.add_vertex((Vec3f)c1.cast<float>(), n);
            g.add_vertex((Vec3f)c2.cast<float>(), n); g.add_vertex((Vec3f)c3.cast<float>(), n);
            g.add_triangle(0,1,2); g.add_triangle(0,2,3);
            ph.quad_model.init_from(std::move(g));
        }
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
        {
            ph.quad_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                         GLModel::Geometry::EVertexLayout::P3N3 };
            ColorRGBA fill = col; fill.a(0.35f); g.color = fill;
            g.reserve_vertices(CIRCLE_SEGS + 1);
            g.reserve_indices(CIRCLE_SEGS * 3);
            g.add_vertex((Vec3f)c.cast<float>(), normal);
            for (int i = 0; i < CIRCLE_SEGS; ++i) {
                const double a = 2.0 * M_PI * i / CIRCLE_SEGS;
                g.add_vertex((Vec3f)(c + u*(sz*std::cos(a)) + v*(sz*std::sin(a))).cast<float>(), normal);
            }
            for (int i = 0; i < CIRCLE_SEGS; ++i)
                g.add_triangle(0, i+1, (i+1)%CIRCLE_SEGS + 1);
            ph.quad_model.init_from(std::move(g));
        }
        {
            ph.border_model.reset();
            GLModel::Geometry g;
            g.format = { GLModel::Geometry::EPrimitiveType::Lines,
                         GLModel::Geometry::EVertexLayout::P3 };
            g.color = col;
            g.reserve_vertices(CIRCLE_SEGS); g.reserve_indices(CIRCLE_SEGS * 2);
            for (int i = 0; i < CIRCLE_SEGS; ++i) {
                const double a = 2.0 * M_PI * i / CIRCLE_SEGS;
                g.add_vertex((Vec3f)(c + u*(sz*std::cos(a)) + v*(sz*std::sin(a))).cast<float>());
            }
            for (int i = 0; i < CIRCLE_SEGS; ++i) g.add_line(i, (i+1)%CIRCLE_SEGS);
            ph.border_model.init_from(std::move(g));
        }
    };

    const bool use_circle = (m_plane_prefs.shape == PlaneHandleShape::Circle);
    auto build = [&](PlaneHandle& ph, const Vec3d& c,
                     const Vec3d& u, const Vec3d& v, const ColorRGBA& col) {
        if (use_circle) build_circle(ph, c, u, v, col);
        else            build_square(ph, c, u, v, col);
    };

    // Winding: u × v = outward normal for each face
    build(m_plane_handles[0], m_grabbers[SCALE_PLANE_ID_YZ].center,
          Vec3d::UnitY(), Vec3d::UnitZ(), m_grabbers[SCALE_PLANE_ID_YZ].color);
    build(m_plane_handles[1], m_grabbers[SCALE_PLANE_ID_XZ].center,
          Vec3d::UnitZ(), Vec3d::UnitX(), m_grabbers[SCALE_PLANE_ID_XZ].color); // swapped for +UnitY normal
    build(m_plane_handles[2], m_grabbers[SCALE_PLANE_ID_XY].center,
          Vec3d::UnitX(), Vec3d::UnitY(), m_grabbers[SCALE_PLANE_ID_XY].color);
}

void GLGizmoScale3D::render_plane_handles(const Transform3d& base_matrix)
{
    const int plane_grabber_ids[3] = { SCALE_PLANE_ID_YZ, SCALE_PLANE_ID_XZ, SCALE_PLANE_ID_XY };
    const Camera& camera = wxGetApp().plater()->get_camera();

    // Filled semi-transparent quads
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * base_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("emission_factor",   0.2f);

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glDepthMask(GL_FALSE));

        for (int i = 0; i < 3; ++i) {
            if (!m_grabbers[plane_grabber_ids[i]].enabled)
                continue;
            if (m_hover_id == plane_grabber_ids[i]) {
                ColorRGBA c = m_grabbers[plane_grabber_ids[i]].hover_color;
                c.a(0.55f);
                m_plane_handles[i].quad_model.set_color(c);
            } else {
                ColorRGBA c = m_grabbers[plane_grabber_ids[i]].color;
                c.a(0.35f);
                m_plane_handles[i].quad_model.set_color(c);
            }
            m_plane_handles[i].quad_model.render();
        }

        glsafe(::glDepthMask(GL_TRUE));
        glsafe(::glEnable(GL_CULL_FACE));
        glsafe(::glDisable(GL_BLEND));
        shader->stop_using();
    }
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
            const ColorRGBA& c = (m_hover_id == plane_grabber_ids[i])
                ? m_grabbers[plane_grabber_ids[i]].hover_color
                : m_grabbers[plane_grabber_ids[i]].color;
            m_plane_handles[i].border_model.set_color(c);
            m_plane_handles[i].border_model.render();
        }
        line_shader->stop_using();
    }
}

// ---------------------------------------------------------------------------
// ORCA: plane-constrained scale
// ---------------------------------------------------------------------------

void GLGizmoScale3D::do_scale_on_plane(Axis locked_axis, const UpdateData& data)
{
    // calc_ratio is designed for 1D radial scaling and breaks down for plane
    // handles because starting_vec ends up nearly perpendicular to the free
    // axes. Instead we compute the ratio as:
    //   ratio = dist(pivot, current_mouse_in_plane) / dist(pivot, drag_start)
    // where all distances are measured within the handle's plane.

    auto grabbers_transform = m_grabbers_tran.get_matrix();
    // Pivot = bbox center in world space
    const Vec3d pivot       = grabbers_transform * Vec3d::Zero();
    const Vec3d drag_start  = m_starting.drag_position; // world, set at on_start_dragging
    const double start_dist = (drag_start - pivot).norm();

    if (start_dist < 1e-6)
        return;

    // Find where the current mouse ray intersects the drag plane
    const Vec3d mouse_dir = data.mouse_ray.unit_vector();
    const double denom    = m_plane_drag_normal.dot(mouse_dir);
    if (std::abs(denom) < 1e-6)
        return;

    const double t        = m_plane_drag_normal.dot(drag_start - data.mouse_ray.a) / denom;
    const Vec3d current   = data.mouse_ray.a + t * mouse_dir;
    const double cur_dist = (current - pivot).norm();

    double ratio = cur_dist / start_dist;
    if (ratio <= 0.0)
        return;

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * std::round(ratio / m_snap_step);

    switch (locked_axis) {
    case X:
        m_scale.x() = m_starting.scale.x();
        m_scale.y() = m_starting.scale.y() * ratio;
        m_scale.z() = m_starting.scale.z() * ratio;
        break;
    case Y:
        m_scale.x() = m_starting.scale.x() * ratio;
        m_scale.y() = m_starting.scale.y();
        m_scale.z() = m_starting.scale.z() * ratio;
        break;
    case Z:
        m_scale.x() = m_starting.scale.x() * ratio;
        m_scale.y() = m_starting.scale.y() * ratio;
        m_scale.z() = m_starting.scale.z();
        break;
    default:
        break;
    }

    m_offset = Vec3d::Zero();
}

// ---------------------------------------------------------------------------
// Existing scale helpers (unchanged)
// ---------------------------------------------------------------------------

void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        m_scale(axis) = m_starting.scale(axis) * ratio;
        if (m_starting.ctrl_down && std::abs(ratio - 1.0f) > 0.001) {
            double local_offset = 0.5 * (m_scale(axis) - m_starting.scale(axis)) * m_starting.box.size()(axis);
            if (m_hover_id == 2 * axis)
                local_offset *= -1.0;
            Vec3d local_offset_vec;
            switch (axis) {
            case X: local_offset_vec = local_offset * Vec3d::UnitX(); break;
            case Y: local_offset_vec = local_offset * Vec3d::UnitY(); break;
            case Z: local_offset_vec = local_offset * Vec3d::UnitZ(); break;
            default: break;
            }
            if (m_object_manipulation->is_world_coordinates())
                m_offset = local_offset_vec;
            else
                m_offset = m_grabbers_tran.get_matrix_no_offset() * local_offset_vec;
        } else {
            m_offset = Vec3d::Zero();
        }
    }
}

void GLGizmoScale3D::do_scale_uniform(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
    {
        m_scale = m_starting.scale * ratio;
        if (m_starting.ctrl_down && abs(ratio-1.0f)>0.001) {
            m_scale.z() = m_starting.scale.z();
            double local_offset_x = 0.5 * (m_scale.x() - m_starting.scale.x()) * m_starting.box.size().x();
            double local_offset_y = 0.5 * (m_scale.y() - m_starting.scale.y()) * m_starting.box.size().y();
            
            Vec3d local_offset_vec = Vec3d::Zero();
            switch (m_hover_id)
            {
                case 6: { local_offset_vec = Vec3d(-local_offset_x, -local_offset_y, 0.0); break; }
                case 7: { local_offset_vec = Vec3d( local_offset_x, -local_offset_y, 0.0); break; }
                case 8: { local_offset_vec = Vec3d( local_offset_x,  local_offset_y, 0.0); break; }
                case 9: { local_offset_vec = Vec3d(-local_offset_x,  local_offset_y, 0.0); break; }
                default: break;
            }
            
            if (m_object_manipulation->is_world_coordinates()) {
                m_offset = local_offset_vec;
            } else {
                m_offset = m_grabbers_tran.get_matrix_no_offset() * local_offset_vec;
            }
        } else {
            m_offset = Vec3d::Zero();
        }
    }
}

double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;

    Vec3d  pivot        = (m_starting.ctrl_down && (m_hover_id < 6))
                           ? m_starting.constraint_position
                           : m_starting.plane_center;
    Vec3d  starting_vec = m_starting.drag_position - pivot;
    double len_starting_vec = starting_vec.norm();

    if (len_starting_vec != 0.0) {
        Vec3d mouse_dir    = data.mouse_ray.unit_vector();
        Vec3d plane_normal = m_starting.plane_nromal;
        if (m_hover_id == 5) {
            Vec3d plane_vec = mouse_dir.cross(m_starting.plane_nromal);
            plane_normal    = plane_vec.cross(m_starting.plane_nromal);
        }
        plane_normal = plane_normal.normalized();

        auto dot_value = plane_normal.dot(mouse_dir);
        auto angle     = Geometry::rad2deg(acos(dot_value));
        if (std::abs(angle) < 95 && std::abs(angle) > 85)
            return 1;

        Vec3d inters     = GetIntersectionOfRayAndPlane(data.mouse_ray.a, mouse_dir, m_starting.drag_position, plane_normal);
        Vec3d inters_vec = inters - m_starting.drag_position;
        double proj      = inters_vec.dot(starting_vec.normalized());
        ratio = (len_starting_vec + proj) / len_starting_vec;
    }

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * (double)std::round(ratio / m_snap_step);

    return ratio;
}

} // namespace GUI
} // namespace Slic3r
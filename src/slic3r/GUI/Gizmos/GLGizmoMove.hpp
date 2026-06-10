#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"
#include "../PlaneHandlePrefs.hpp"


namespace Slic3r {
namespace GUI {

//BBS: GUI refactor: add object manipulation
class GizmoObjectManipulation;
class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_displacement{ Vec3d::Zero() };
    Vec3d m_center{ Vec3d::Zero() };
    BoundingBoxf3 m_bounding_box;
    double m_snap_step{ 1.0 };
    Vec3d m_starting_drag_position{ Vec3d::Zero() };
    Vec3d m_starting_box_center{ Vec3d::Zero() };
    Vec3d m_starting_box_bottom_center{ Vec3d::Zero() };

    // ORCA: plane handles — store the plane normal used at drag start
    Vec3d m_plane_drag_normal{ Vec3d::Zero() };
    // ORCA: actual mouse-ray hit point on the plane at drag start
    Vec3d m_plane_drag_start_hit{ Vec3d::Zero() };
    // ORCA: displacement from the previous frame, used to compute per-frame
    // incremental deltas for plane drags (avoids Selection cache baseline issue)
    Vec3d m_prev_plane_displacement{ Vec3d::Zero() };

    struct GrabberConnection
    {
        GLModel model;
        Vec3d old_center{ Vec3d::Zero() };
    };
    std::array<GrabberConnection, 3> m_grabber_connections;

    // ORCA: plane handle quad geometry, one per plane (YZ=0, XZ=1, XY=2)
    struct PlaneHandle
    {
        GLModel quad_model;
        GLModel border_model;
    };
    std::array<PlaneHandle, 3> m_plane_handles;

    // ORCA: cached plane handle preferences
    PlaneHandlePrefs m_plane_prefs;

    // ORCA: full-face plane overlay shown during active plane-constrained drag
    GLModel m_drag_plane_model;
    Vec3d   m_drag_plane_last_hs{ Vec3d::Zero() }; // rebuilt when bbox changes

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;

public:
    //BBS: add obj manipulation logic
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);
    virtual ~GLGizmoMove3D() = default;

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    std::string get_tooltip() const override;

    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    virtual void on_set_state() override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData& data) override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    double calc_projection(const UpdateData& data) const;
    // ORCA: plane-constrained drag — projects mouse ray onto a plane and
    // returns the 2D displacement in the plane's local axes
    Vec3d  calc_plane_projection(const UpdateData& data, const Vec3d& plane_normal) const;
    void   rebuild_plane_quads();
    void   render_plane_handles(const Transform3d& base_matrix);
    void   render_drag_plane_overlay(const Transform3d& base_matrix);
    void   change_cs_by_selection();

private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
#pragma once
// ORCA: CrashSlicer -- GizmoSnapTicks
// Click-to-snap tick marks for the Move gizmo.
// Ticks are computed once when the gizmo activates and frozen in world space.
// Clicking within PROXIMITY_PX of a tick teleports the object there.

#include "GizmoEnums.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"

#include <string>
#include <vector>

namespace Slic3r { class GLShaderProgram; }

namespace Slic3r {
namespace GUI {

struct Camera;

enum class TickAxis     { X = 0, Y = 1, Z = 2 };
enum class TickCategory { BboxMultiple, PlateReference, PlateCenter };

// A snap target frozen in world space
struct SnapTick
{
    TickCategory category;

    // World position of the snap point
    Vec3d world_pos { Vec3d::Zero() };

    // Which axes this tick constrains when snapping
    // Single-axis: only axis[0] is used
    // XY center: both axes[0]=X and axes[1]=Y are set
    int  axes[2]  { -1, -1 }; // -1 = unused
    int  axes_count { 1 };

    std::string label;
    int         mul { 0 }; // bbox multiplier for BboxMultiple, 0 otherwise
};

class GizmoSnapTicks
{
public:
    static constexpr float PROXIMITY_PX       = 16.0f;
    static constexpr float PULSE_SECONDS      = 0.3f;

    GizmoSnapTicks();
    ~GizmoSnapTicks();

    // Build tick list. Called once when gizmo activates or selection changes.
    // world_pos: current object center in world space (used to place bbox ticks)
    void build_move_ticks(const BoundingBoxf3& bbox,
                          const Vec2d&         plate_size,
                          const Vec3d&         world_pos);

    void build_scale_ticks(const BoundingBoxf3& bbox, bool include_negative_z);

    void clear();

    // Call each frame. Projects ticks to screen, finds nearest to mouse.
    // Returns index of hovered tick, or -1.
    int  update_proximity(const Point&  mouse_pos,
                          const Camera& camera);

    // Advance pulse animation. Returns true if repaint needed.
    bool update_timer(float delta_seconds);

    // Call when the user clicks -- if hovering a tick, activate snap and
    // return the tick. Caller applies the teleport.
    const SnapTick* try_click_snap();

    bool            is_snapped()    const { return m_snapped_idx >= 0; }
    int             hovered_index() const { return m_hovered_idx; }
    int             snapped_index() const { return m_snapped_idx; }
    const SnapTick* snapped_tick()  const;
    const SnapTick* hovered_tick()  const;

    void clear_snap();

    // Render tick marks in world space
    void render(const Camera& camera, TickStyle style);

    const std::vector<SnapTick>& ticks() const { return m_ticks; }

private:
    std::vector<SnapTick> m_ticks;
    std::vector<Vec2d>    m_screen_positions;

    int   m_hovered_idx { -1 };
    int   m_snapped_idx { -1 };
    float m_pulse_t     { 0.0f };

    struct Impl;
    Impl* m_impl { nullptr };
    bool  m_models_dirty { true };

    void invalidate_models();
    void rebuild_models_if_needed();

    static Vec2d     project_to_screen(const Vec3d& world_pt, const Camera& camera);
    static ColorRGBA category_color(TickCategory cat, bool hover);
};

} // namespace GUI
} // namespace Slic3r
#include "GizmoSnapTicks.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GLModel.hpp"

#include <glad/gl.h>
#include <wx/utils.h>
#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// Pimpl
// ---------------------------------------------------------------------------

struct GizmoSnapTicks::Impl
{
    struct TickModel { GLModel geom; };
    std::vector<TickModel> models;
};

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

static const ColorRGBA COL_BBOX     = { 0.55f, 0.55f, 0.55f, 1.0f }; // grey
static const ColorRGBA COL_BBOX_HOV = { 0.90f, 0.90f, 0.90f, 1.0f };
static const ColorRGBA COL_PLATE    = { 0.937f, 0.624f, 0.153f, 1.0f }; // amber
static const ColorRGBA COL_PLATE_HOV= { 1.0f,  0.820f, 0.400f, 1.0f };
static const ColorRGBA COL_CENTER   = { 0.3f,  0.8f,   1.0f,   1.0f }; // cyan
static const ColorRGBA COL_CTR_HOV  = { 0.6f,  1.0f,   1.0f,   1.0f };

ColorRGBA GizmoSnapTicks::category_color(TickCategory cat, bool hover)
{
    switch (cat) {
    case TickCategory::BboxMultiple:  return hover ? COL_BBOX_HOV  : COL_BBOX;
    case TickCategory::PlateReference:return hover ? COL_PLATE_HOV : COL_PLATE;
    case TickCategory::PlateCenter:   return hover ? COL_CTR_HOV   : COL_CENTER;
    default: return hover ? COL_BBOX_HOV : COL_BBOX;
    }
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

GizmoSnapTicks::GizmoSnapTicks()  : m_impl(new Impl()) {}
GizmoSnapTicks::~GizmoSnapTicks() { delete m_impl; }

void GizmoSnapTicks::clear()            { m_ticks.clear(); invalidate_models(); }
void GizmoSnapTicks::invalidate_models(){ m_models_dirty = true; }

void GizmoSnapTicks::clear_snap()
{
    m_snapped_idx = -1;
    m_hovered_idx = -1;
    m_pulse_t     = 0.0f;
}

const SnapTick* GizmoSnapTicks::snapped_tick() const
{
    if (m_snapped_idx < 0 || m_snapped_idx >= (int)m_ticks.size()) return nullptr;
    return &m_ticks[m_snapped_idx];
}

const SnapTick* GizmoSnapTicks::hovered_tick() const
{
    if (m_hovered_idx < 0 || m_hovered_idx >= (int)m_ticks.size()) return nullptr;
    return &m_ticks[m_hovered_idx];
}

// ---------------------------------------------------------------------------
// Build move ticks -- all stored as frozen world positions
// ---------------------------------------------------------------------------

void GizmoSnapTicks::build_move_ticks(const BoundingBoxf3& bbox,
                                       const Vec2d&         plate_size,
                                       const Vec3d&         world_pos)
{
    m_ticks.clear();
    invalidate_models();

    const Vec3d size = bbox.size();
    const double cx  = plate_size.x() * 0.5;
    const double cy  = plate_size.y() * 0.5;

    // Helper: add a single-axis bbox multiple tick at a world position
    auto add_bbox = [&](int axis, double world_coord, int mul, bool positive) {
        SnapTick t;
        t.category   = TickCategory::BboxMultiple;
        t.world_pos  = world_pos; // start from object center
        t.world_pos(axis) = world_coord;
        t.axes[0]    = axis; t.axes[1] = -1; t.axes_count = 1;
        t.mul        = mul;
        const std::string sign = positive ? "+" : "-";
        t.label = sign + std::to_string(mul) + "\xC3\x97"
                + " (" + (axis==0?"X":axis==1?"Y":"Z") + ")";
        m_ticks.push_back(t);
    };

    // Bbox multiples: 1x-5x per axis, positive and negative
    for (int a = 0; a < 3; ++a) {
        const double dim = size(a);
        if (dim < 1e-4) continue;
        for (int mul = 1; mul <= 5; ++mul) {
            add_bbox(a, world_pos(a) + dim * mul, mul, true);
            add_bbox(a, world_pos(a) - dim * mul, mul, false);
        }
    }

    // --- Plate reference ticks ---

    // X-axis center line (object snaps to plate center X, Y stays)
    {
        SnapTick t;
        t.category = TickCategory::PlateReference;
        t.world_pos = world_pos; t.world_pos.x() = cx;
        t.axes[0] = 0; t.axes[1] = -1; t.axes_count = 1;
        t.label = "Plate center X";
        m_ticks.push_back(t);
    }
    // Y-axis center line
    {
        SnapTick t;
        t.category = TickCategory::PlateReference;
        t.world_pos = world_pos; t.world_pos.y() = cy;
        t.axes[0] = 1; t.axes[1] = -1; t.axes_count = 1;
        t.label = "Plate center Y";
        m_ticks.push_back(t);
    }

    // XY center (snaps both X and Y simultaneously)
    {
        SnapTick t;
        t.category = TickCategory::PlateCenter;
        t.world_pos = world_pos;
        t.world_pos.x() = cx; t.world_pos.y() = cy;
        t.axes[0] = 0; t.axes[1] = 1; t.axes_count = 2;
        t.label = "Plate center";
        m_ticks.push_back(t);
    }

    // Plate edges X
    {
        SnapTick t;
        t.category = TickCategory::PlateReference;
        t.world_pos = world_pos; t.world_pos.x() = 0.0;
        t.axes[0] = 0; t.axes_count = 1;
        t.label = "Plate edge X\xe2\x86\x90"; // ←
        m_ticks.push_back(t);
    }
    {
        SnapTick t;
        t.category = TickCategory::PlateReference;
        t.world_pos = world_pos; t.world_pos.x() = plate_size.x();
        t.axes[0] = 0; t.axes_count = 1;
        t.label = "Plate edge X\xe2\x86\x92"; // →
        m_ticks.push_back(t);
    }
    // Plate edges Y
    {
        SnapTick t;
        t.category = TickCategory::PlateReference;
        t.world_pos = world_pos; t.world_pos.y() = 0.0;
        t.axes[0] = 1; t.axes_count = 1;
        t.label = "Plate edge Y\xe2\x86\x90";
        m_ticks.push_back(t);
    }
    {
        SnapTick t;
        t.category = TickCategory::PlateReference;
        t.world_pos = world_pos; t.world_pos.y() = plate_size.y();
        t.axes[0] = 1; t.axes_count = 1;
        t.label = "Plate edge Y\xe2\x86\x92";
        m_ticks.push_back(t);
    }

    m_screen_positions.resize(m_ticks.size(), Vec2d::Zero());
}

// ---------------------------------------------------------------------------
// Build scale ticks
// ---------------------------------------------------------------------------

void GizmoSnapTicks::build_scale_ticks(const BoundingBoxf3& bbox,
                                        bool                 include_negative_z)
{
    m_ticks.clear();
    invalidate_models();
    // Scale ticks are handled differently -- to be implemented
    m_screen_positions.resize(m_ticks.size(), Vec2d::Zero());
}

// ---------------------------------------------------------------------------
// Screen projection
// ---------------------------------------------------------------------------

Vec2d GizmoSnapTicks::project_to_screen(const Vec3d& world_pt, const Camera& camera)
{
    const Matrix4d vp = camera.get_projection_matrix().matrix()
                      * camera.get_view_matrix().matrix();
    const Vec4d clip = vp * Vec4d(world_pt.x(), world_pt.y(), world_pt.z(), 1.0);
    if (std::abs(clip.w()) < 1e-8) return Vec2d(-1e6, -1e6);
    const Vec3d ndc    = clip.head<3>() / clip.w();
    const auto& vp_arr = camera.get_viewport();
    return {
        vp_arr[0] + (ndc.x() + 1.0) * 0.5 * vp_arr[2],
        vp_arr[1] + (1.0 - ndc.y()) * 0.5 * vp_arr[3]
    };
}

// ---------------------------------------------------------------------------
// Per-frame proximity update
// ---------------------------------------------------------------------------

int GizmoSnapTicks::update_proximity(const Point& mouse_pos, const Camera& camera)
{
    if (m_ticks.empty()) return -1;
    m_screen_positions.resize(m_ticks.size(), Vec2d::Zero());

    // Project each tick's world_pos to screen
    for (size_t i = 0; i < m_ticks.size(); ++i)
        m_screen_positions[i] = project_to_screen(m_ticks[i].world_pos, camera);

    const Vec2d mp(mouse_pos.x(), mouse_pos.y());
    float best = PROXIMITY_PX;
    int   best_idx = -1;
    for (size_t i = 0; i < m_ticks.size(); ++i) {
        const float d = static_cast<float>((m_screen_positions[i] - mp).norm());
        if (d < best) { best = d; best_idx = static_cast<int>(i); }
    }

    if (best_idx != m_hovered_idx)
        m_hovered_idx = best_idx;

    return m_hovered_idx;
}

// ---------------------------------------------------------------------------
// Click snap
// ---------------------------------------------------------------------------

const SnapTick* GizmoSnapTicks::try_click_snap()
{
    if (m_hovered_idx < 0) return nullptr;
    m_snapped_idx = m_hovered_idx;
    m_pulse_t     = 0.0f;
    return snapped_tick();
}

// ---------------------------------------------------------------------------
// Pulse timer
// ---------------------------------------------------------------------------

bool GizmoSnapTicks::update_timer(float delta_seconds)
{
    if (m_pulse_t < 1.0f && m_snapped_idx >= 0) {
        m_pulse_t = std::min(1.0f, m_pulse_t + delta_seconds / PULSE_SECONDS);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// GL model rebuild
// ---------------------------------------------------------------------------

void GizmoSnapTicks::rebuild_models_if_needed()
{
    if (!m_models_dirty) return;
    m_models_dirty = false;
    m_impl->models.clear();
    m_impl->models.resize(m_ticks.size());

    static constexpr float TICK_HALF  = 5.0f;
    static constexpr float PLATE_S    = 7.0f;
    static constexpr float INSET      = 6.0f;
    static constexpr float CTR_S      = 8.0f;

    for (size_t i = 0; i < m_ticks.size(); ++i) {
        const SnapTick& t = m_ticks[i];
        const ColorRGBA col = category_color(t.category, false);

        GLModel::Geometry g;
        g.format = { GLModel::Geometry::EPrimitiveType::Lines,
                     GLModel::Geometry::EVertexLayout::P3 };
        g.color = col;

        if (t.category == TickCategory::BboxMultiple) {
            // Short perpendicular tick on the relevant axis
            const int a = t.axes[0];
            // Perpendicular in XY plane: X ticks get Y perp, Y ticks get X perp, Z gets X
            Vec3d perp = Vec3d::Zero();
            perp((a == 0) ? 1 : 0) = TICK_HALF;
            g.reserve_vertices(2); g.reserve_indices(2);
            g.add_vertex((Vec3f)(-perp).cast<float>());
            g.add_vertex((Vec3f)( perp).cast<float>());
            g.add_line(0, 1);

        } else if (t.category == TickCategory::PlateCenter) {
            // XY center: flat diamond in XY plane + cross
            // Diamond
            const float s = CTR_S;
            g.reserve_vertices(8); g.reserve_indices(8);
            g.add_vertex(Vec3f( s,  0,  0)); // 0
            g.add_vertex(Vec3f( 0,  s,  0)); // 1
            g.add_vertex(Vec3f(-s,  0,  0)); // 2
            g.add_vertex(Vec3f( 0, -s,  0)); // 3
            g.add_line(0,1); g.add_line(1,2); g.add_line(2,3); g.add_line(3,0);
            // Cross
            g.add_vertex(Vec3f(-s*0.6f, 0, 0)); // 4
            g.add_vertex(Vec3f( s*0.6f, 0, 0)); // 5
            g.add_vertex(Vec3f(0, -s*0.6f, 0)); // 6
            g.add_vertex(Vec3f(0,  s*0.6f, 0)); // 7
            g.add_line(4,5); g.add_line(6,7);

        } else {
            // Plate edge or single-axis center: bracket in XY plane
            const int a = t.axes[0];
            // Determine free (spine) axis and inward direction
            const int free_ax = (a == 0) ? 1 : 0;
            // Inward: toward plate interior
            // For X edges: X=0 edge points right (+X), X=max points left (-X)
            // For Y edges: Y=0 points up (+Y), Y=max points down (-Y)
            // For center lines: no inward arm, just a spine + small tick
            const bool is_edge = (t.label.find("edge") != std::string::npos);

            Vec3d spine = Vec3d::Zero(); spine(free_ax) = PLATE_S;
            Vec3d arm   = Vec3d::Zero();

            if (is_edge) {
                // Inward direction from edge toward center
                const double plate_dim = (a == 0) ? 256.0 : 256.0; // fallback
                const bool near_zero = (t.world_pos(a) < 1.0);
                arm(a) = near_zero ? INSET : -INSET;

                g.reserve_vertices(6); g.reserve_indices(6);
                // Spine
                g.add_vertex((Vec3f)(-spine).cast<float>()); // 0
                g.add_vertex((Vec3f)( spine).cast<float>()); // 1
                // Top arm
                g.add_vertex((Vec3f)(spine).cast<float>());          // 2
                g.add_vertex((Vec3f)(spine + arm).cast<float>());    // 3
                // Bottom arm
                g.add_vertex((Vec3f)(-spine).cast<float>());         // 4
                g.add_vertex((Vec3f)(-spine + arm).cast<float>());   // 5
                g.add_line(0,1); g.add_line(2,3); g.add_line(4,5);
            } else {
                // Center line on one axis: small cross-tick
                g.reserve_vertices(2); g.reserve_indices(2);
                g.add_vertex((Vec3f)(-spine).cast<float>());
                g.add_vertex((Vec3f)( spine).cast<float>());
                g.add_line(0, 1);
            }
        }

        m_impl->models[i].geom.init_from(std::move(g));
    }
}

// ---------------------------------------------------------------------------
// Render -- everything in world space using identity matrix per tick
// ---------------------------------------------------------------------------

void GizmoSnapTicks::render(const Camera& camera, TickStyle /*style*/)
{
    if (m_ticks.empty()) return;
    rebuild_models_if_needed();
    if (m_impl->models.empty()) return;

    ::Slic3r::GLShaderProgram* shader = nullptr;
#if SLIC3R_OPENGL_ES
    shader = wxGetApp().get_shader("dashed_lines");
#else
    if (OpenGLManager::get_gl_info().is_core_profile())
        shader = wxGetApp().get_shader("dashed_thick_lines");
    else
        shader = wxGetApp().get_shader("flat");
#endif
    if (!shader) return;

    shader->start_using();

    const auto& vp = camera.get_viewport();

    for (size_t i = 0; i < m_ticks.size(); ++i) {
        const SnapTick& t       = m_ticks[i];
        const bool      hovered = (static_cast<int>(i) == m_hovered_idx);
        const bool      snapped = (static_cast<int>(i) == m_snapped_idx);

        ColorRGBA col = category_color(t.category, hovered || snapped);

        // Pulse on snap
        if (snapped && m_pulse_t < 1.0f) {
            const float pulse = std::sin(m_pulse_t * float(M_PI));
            col.r(std::min(1.0f, col.r() + pulse * 0.4f));
            col.g(std::min(1.0f, col.g() + pulse * 0.4f));
            col.b(std::min(1.0f, col.b() + pulse * 0.4f));
        }

        // Each tick renders at its frozen world_pos
        Transform3d m = Transform3d::Identity();
        m.translation() = t.world_pos;

        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile())
#endif
        {
            shader->set_uniform("viewport_size", Vec2d(double(vp[2]), double(vp[3])));
            shader->set_uniform("width",    hovered ? 3.0f : 1.5f);
            shader->set_uniform("gap_size", 0.0f);
        }

        m_impl->models[i].geom.set_color(col);
        m_impl->models[i].geom.render();
    }

    shader->stop_using();
}

} // namespace GUI
} // namespace Slic3r
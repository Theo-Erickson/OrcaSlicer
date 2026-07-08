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

void GizmoSnapTicks::build_move_ticks(const BoundingBoxf3&    bbox,
                                       const Vec2d&            plate_size,
                                       const Vec3d&            world_pos,
                                       const PlaneHandlePrefs& prefs)
{
    m_ticks.clear();
    invalidate_models();

    const Vec3d  size = bbox.size();
    const Vec3d  hs   = size * 0.5;
    const double cx   = plate_size.x() * 0.5;
    const double cy   = plate_size.y() * 0.5;

    // Helper: add a tick at an absolute world coord on a single axis
    auto add_single = [&](TickCategory cat, int axis,
                          double world_coord, const std::string& lbl, int mul = 0)
    {
        SnapTick t;
        t.category       = cat;
        t.world_pos      = world_pos;
        t.world_pos(axis)= world_coord;
        t.axes[0]        = axis; t.axes[1] = -1; t.axes_count = 1;
        t.label          = lbl;  t.mul      = mul;
        m_ticks.push_back(t);
    };

    // --- Axis bbox-multiple ticks ---
    if (prefs.axis_ticks_enabled) {
        const bool do_axis[3] = {
            prefs.axis_tick_x,
            prefs.axis_tick_y,
            prefs.axis_tick_z
        };
        for (int a = 0; a < 3; ++a) {
            if (!do_axis[a]) continue;
            const double dim = size(a);
            if (dim < 1e-4) continue;
            for (int n = 0; n < prefs.axis_tick_count; ++n) {
                const float  mul_f = prefs.axis_tick_start
                                   + n * prefs.axis_tick_increment;
                const std::string axis_name = (a==0?"X":a==1?"Y":"Z");
                // Format multiplier cleanly: "1x", "1.5x" etc.
                char buf[16];
                if (std::abs(mul_f - std::round(mul_f)) < 0.01f)
                    std::snprintf(buf, sizeof(buf), "%d", (int)std::round(mul_f));
                else
                    std::snprintf(buf, sizeof(buf), "%.1f", mul_f);
                const std::string lbl_base = std::string(buf) + " x bounding box (" + axis_name + ")";
                add_single(TickCategory::BboxMultiple, a,
                           world_pos(a) + dim * mul_f,  "+" + lbl_base, n+1);
                add_single(TickCategory::BboxMultiple, a,
                           world_pos(a) - dim * mul_f,  "-" + lbl_base, -(n+1));
            }
        }
    }

    // --- Plate ticks ---
    if (prefs.plate_ticks_enabled) {
        // X edges: snap object FACE to plate edge (offset by bbox half-width)
        if (prefs.plate_tick_x_edges) {
            add_single(TickCategory::PlateReference, 0,
                       hs.x(),                    "Plate edge X-");
            add_single(TickCategory::PlateReference, 0,
                       plate_size.x() - hs.x(),   "Plate edge X+");
        }
        // Y edges
        if (prefs.plate_tick_y_edges) {
            add_single(TickCategory::PlateReference, 1,
                       hs.y(),                    "Plate edge Y-");
            add_single(TickCategory::PlateReference, 1,
                       plate_size.y() - hs.y(),   "Plate edge Y+");
        }

        // Plate origin / center ticks
        if (prefs.plate_tick_origin) {
            // Use PlateCenter category so X/Y center spines are also cyan
            add_single(TickCategory::PlateCenter, 0, cx, "Plate center X");
            add_single(TickCategory::PlateCenter, 1, cy, "Plate center Y");
            // Plate bottom (Z): a cyan tick sitting on the bed directly below
            // the object.  Snapping drops the object so its base rests on the
            // plate (bottom at Z=0, i.e. Z position reads 0.0).  The special
            // "drop-to-bed" handling lives in GLGizmoMove's snap application.
            add_single(TickCategory::PlateCenter, 2, 0.0, "Plate bottom Z");
            // XY center: snaps both axes simultaneously
            SnapTick t;
            t.category      = TickCategory::PlateCenter;
            t.world_pos     = world_pos;
            t.world_pos.x() = cx; t.world_pos.y() = cy;
            t.axes[0] = 0; t.axes[1] = 1; t.axes_count = 2;
            t.label = "Plate center";
            m_ticks.push_back(t);
        }

        // Plate division ticks: N interior points per HALF of the plate.
        // With N=2: left_edge, 1/3, 2/3, center, 4/3, 5/3, right_edge
        // i.e. each half [0, cx] and [cx, plate_size] gets N evenly-spaced
        // interior points, giving N+1 segments per half.
        if (prefs.plate_divisions > 0) {
            const int N = prefs.plate_divisions;
            // X axis: divide [0,cx] into N+1 segments and [cx,plate_x] into N+1
            if (prefs.plate_tick_x_edges) {
                // Left half: points at cx * k/(N+1) for k=1..N
                for (int k = 1; k <= N; ++k) {
                    const double t_pos = cx * k / (N + 1);
                    const int pct = static_cast<int>(t_pos / plate_size.x() * 100.0 + 0.5);
                    add_single(TickCategory::PlateReference, 0,
                               t_pos, std::to_string(pct) + "% X");
                }
                // Right half: points at cx + (plate_x-cx)*k/(N+1) for k=1..N
                for (int k = 1; k <= N; ++k) {
                    const double t_pos = cx + (plate_size.x() - cx) * k / (N + 1);
                    const int pct = static_cast<int>(t_pos / plate_size.x() * 100.0 + 0.5);
                    add_single(TickCategory::PlateReference, 0,
                               t_pos, std::to_string(pct) + "% X");
                }
            }
            // Y axis: same split around cy
            if (prefs.plate_tick_y_edges) {
                for (int k = 1; k <= N; ++k) {
                    const double t_pos = cy * k / (N + 1);
                    const int pct = static_cast<int>(t_pos / plate_size.y() * 100.0 + 0.5);
                    add_single(TickCategory::PlateReference, 1,
                               t_pos, std::to_string(pct) + "% Y");
                }
                for (int k = 1; k <= N; ++k) {
                    const double t_pos = cy + (plate_size.y() - cy) * k / (N + 1);
                    const int pct = static_cast<int>(t_pos / plate_size.y() * 100.0 + 0.5);
                    add_single(TickCategory::PlateReference, 1,
                               t_pos, std::to_string(pct) + "% Y");
                }
            }
        }
    }

    // Apply display mode: OnPlate = force Z to 0 for that tick group
    for (auto& t : m_ticks) {
        if (t.category == TickCategory::BboxMultiple &&
            prefs.axis_ticks_enabled &&
            prefs.axis_tick_display == TickDisplayMode::OnPlate)
            t.world_pos.z() = 0.0;
        if ((t.category == TickCategory::PlateReference ||
             t.category == TickCategory::PlateCenter) &&
            prefs.plate_ticks_enabled &&
            prefs.plate_tick_display == TickDisplayMode::OnPlate)
            t.world_pos.z() = 0.0;
    }

    // Cache opacity values for use during render
    m_axis_opacity  = prefs.axis_tick_opacity;
    m_plate_opacity = prefs.plate_tick_opacity;

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

        } else if (t.category == TickCategory::PlateCenter && t.axes_count == 2) {
            // XY center diamond + cross (flat in XY plane)
            const float s = CTR_S;
            g.reserve_vertices(8); g.reserve_indices(8);
            g.add_vertex(Vec3f( s,  0,  0)); // 0
            g.add_vertex(Vec3f( 0,  s,  0)); // 1
            g.add_vertex(Vec3f(-s,  0,  0)); // 2
            g.add_vertex(Vec3f( 0, -s,  0)); // 3
            g.add_line(0,1); g.add_line(1,2); g.add_line(2,3); g.add_line(3,0);
            g.add_vertex(Vec3f(-s*0.6f, 0, 0)); // 4
            g.add_vertex(Vec3f( s*0.6f, 0, 0)); // 5
            g.add_vertex(Vec3f(0, -s*0.6f, 0)); // 6
            g.add_vertex(Vec3f(0,  s*0.6f, 0)); // 7
            g.add_line(4,5); g.add_line(6,7);

        } else if (t.category == TickCategory::PlateCenter && t.axes_count == 1 &&
                   t.axes[0] == 2) {
            // Plate bottom Z: flat cross marker lying on the bed (XY plane),
            // marking the drop-to-bed target directly under the object.
            const float s = CTR_S;
            g.reserve_vertices(4); g.reserve_indices(4);
            g.add_vertex(Vec3f(-s, 0, 0)); // 0
            g.add_vertex(Vec3f( s, 0, 0)); // 1
            g.add_vertex(Vec3f( 0,-s, 0)); // 2
            g.add_vertex(Vec3f( 0, s, 0)); // 3
            g.add_line(0, 1); g.add_line(2, 3);

        } else if (t.category == TickCategory::PlateCenter && t.axes_count == 1) {
            // Single-axis center (X-only or Y-only): spine perpendicular to that axis
            const int a      = t.axes[0];
            const int free_ax = (a == 0) ? 1 : 0;
            Vec3d spine = Vec3d::Zero(); spine(free_ax) = PLATE_S;
            g.reserve_vertices(2); g.reserve_indices(2);
            g.add_vertex((Vec3f)(-spine).cast<float>());
            g.add_vertex((Vec3f)( spine).cast<float>());
            g.add_line(0, 1);

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
                // Label suffix tells us which edge:
                // "X-" or "Y-" = near-origin edge, arm points inward (+axis)
                // "X+" or "Y+" = far edge, arm points inward (-axis)
                const bool is_near_edge = (t.label.back() == '-');
                arm(a) = is_near_edge ? INSET : -INSET;

                g.reserve_vertices(6); g.reserve_indices(6);
                g.add_vertex((Vec3f)(-spine).cast<float>()); // 0 spine start
                g.add_vertex((Vec3f)( spine).cast<float>()); // 1 spine end
                g.add_vertex((Vec3f)(spine).cast<float>());         // 2 top arm base
                g.add_vertex((Vec3f)(spine + arm).cast<float>());   // 3 top arm tip
                g.add_vertex((Vec3f)(-spine).cast<float>());        // 4 bot arm base
                g.add_vertex((Vec3f)(-spine + arm).cast<float>()); // 5 bot arm tip
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
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    const auto& vp = camera.get_viewport();

    for (size_t i = 0; i < m_ticks.size(); ++i) {
        const SnapTick& t       = m_ticks[i];
        const bool      hovered = (static_cast<int>(i) == m_hovered_idx);
        const bool      snapped = (static_cast<int>(i) == m_snapped_idx);

        ColorRGBA col = category_color(t.category, hovered || snapped);

        // Apply opacity based on tick category
        const float opacity = (t.category == TickCategory::BboxMultiple)
                              ? m_axis_opacity : m_plate_opacity;
        // On hover/snap, always show fully opaque so the indicator is clear
        col.a((hovered || snapped) ? 1.0f : opacity);

        // Pulse on snap
        if (snapped && m_pulse_t < 1.0f) {
            const float pulse = std::sin(m_pulse_t * float(M_PI));
            col.r(std::min(1.0f, col.r() + pulse * 0.4f));
            col.g(std::min(1.0f, col.g() + pulse * 0.4f));
            col.b(std::min(1.0f, col.b() + pulse * 0.4f));
            col.a(1.0f);
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
    glsafe(::glDisable(GL_BLEND));
}

} // namespace GUI
} // namespace Slic3r
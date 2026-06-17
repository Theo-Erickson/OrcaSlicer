#include "PlaneHandlePrefs.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/AppConfig.hpp"

#include <glad/gl.h>
#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// AppConfig key names
// ---------------------------------------------------------------------------
static constexpr const char* K_POSITION         = "plane_handle_position";
static constexpr const char* K_SHAPE            = "plane_handle_shape";
static constexpr const char* K_SIZE_PCT         = "plane_handle_size_pct";
static constexpr const char* K_PLANE_ALPHA      = "plane_handle_drag_opacity";
// Snap ticks global
static constexpr const char* K_TICKS_ON         = "gizmo_snap_ticks_enabled";
static constexpr const char* K_TICK_STYLE       = "gizmo_snap_tick_style";
// Axis ticks
static constexpr const char* K_AXIS_ON          = "snap_axis_ticks_enabled";
static constexpr const char* K_AXIS_X           = "snap_axis_tick_x";
static constexpr const char* K_AXIS_Y           = "snap_axis_tick_y";
static constexpr const char* K_AXIS_Z           = "snap_axis_tick_z";
static constexpr const char* K_AXIS_DISPLAY     = "snap_axis_tick_display";
static constexpr const char* K_AXIS_START       = "snap_axis_tick_start";
static constexpr const char* K_AXIS_INCREMENT   = "snap_axis_tick_increment";
static constexpr const char* K_AXIS_COUNT       = "snap_axis_tick_count";
static constexpr const char* K_AXIS_OPACITY     = "snap_axis_tick_opacity";
// Plate ticks
static constexpr const char* K_PLATE_ON         = "snap_plate_ticks_enabled";
static constexpr const char* K_PLATE_X_EDGES    = "snap_plate_tick_x_edges";
static constexpr const char* K_PLATE_Y_EDGES    = "snap_plate_tick_y_edges";
static constexpr const char* K_PLATE_ORIGIN     = "snap_plate_tick_origin";
static constexpr const char* K_PLATE_DISPLAY    = "snap_plate_tick_display";
static constexpr const char* K_PLATE_DIVISIONS  = "snap_plate_divisions";
static constexpr const char* K_PLATE_OPACITY    = "snap_plate_tick_opacity";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool cfg_bool(AppConfig* cfg, const char* key, bool def)
{
    const std::string v = cfg->get(key);
    if (v.empty()) return def;
    return !(v == "0" || v == "false");
}

static float cfg_float(AppConfig* cfg, const char* key, float def)
{
    const std::string v = cfg->get(key);
    if (v.empty()) return def;
    try { return std::stof(v); } catch (...) { return def; }
}

static int cfg_int(AppConfig* cfg, const char* key, int def)
{
    const std::string v = cfg->get(key);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

PlaneHandlePrefs PlaneHandlePrefs::load()
{
    PlaneHandlePrefs p;
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) return p;

    // Plane handle
    const std::string pos = cfg->get(K_POSITION);
    if      (pos == "intersection") p.position = PlaneHandlePosition::AtIntersection;
    else if (pos == "midpoint")     p.position = PlaneHandlePosition::Midpoint;
    else                            p.position = PlaneHandlePosition::AtArrowEnd;

    p.shape = (cfg->get(K_SHAPE) == "circle")
              ? PlaneHandleShape::Circle : PlaneHandleShape::Square;

    p.size_pct = std::max(0.0f, std::min(500.0f,
                     cfg_float(cfg, K_SIZE_PCT, 100.0f)));

    {   // Opacity: stored as 0-100 int, loaded as 0-1 float
        const std::string a = cfg->get(K_PLANE_ALPHA);
        if (!a.empty()) {
            try {
                float v = std::stof(a);
                if (v > 1.0f) v /= 100.0f;
                p.drag_plane_opacity = std::max(0.0f, std::min(1.0f, v));
            } catch (...) {}
        }
    }

    // Snap ticks global
    p.ticks_enabled = cfg_bool(cfg, K_TICKS_ON, true);
    p.tick_style    = (cfg->get(K_TICK_STYLE) == "floating")
                      ? TickStyle::FloatingLabel : TickStyle::OnArrow;

    // Axis ticks
    p.axis_ticks_enabled  = cfg_bool (cfg, K_AXIS_ON,        true);
    p.axis_tick_x         = cfg_bool (cfg, K_AXIS_X,         true);
    p.axis_tick_y         = cfg_bool (cfg, K_AXIS_Y,         true);
    p.axis_tick_z         = cfg_bool (cfg, K_AXIS_Z,         true);
    p.axis_tick_display   = (cfg->get(K_AXIS_DISPLAY) == "on_plate")
                            ? TickDisplayMode::OnPlate
                            : TickDisplayMode::InlineWithObject;
    p.axis_tick_start     = std::max(0.1f, cfg_float(cfg, K_AXIS_START,     1.0f));
    // Increment stored as integer x10 by the spinctrl (e.g. 5 = 0.5x)
    {
        const int inc_i = cfg_int(cfg, K_AXIS_INCREMENT, 10);
        p.axis_tick_increment = std::max(0.1f, static_cast<float>(inc_i) / 10.0f);
    }
    p.axis_tick_count     = std::max(1, std::min(20, cfg_int(cfg, K_AXIS_COUNT, 5)));
    p.axis_tick_opacity   = std::max(0.0f, std::min(1.0f,
                                cfg_int(cfg, K_AXIS_OPACITY, 100) / 100.0f));

    // Plate ticks
    p.plate_ticks_enabled = cfg_bool(cfg, K_PLATE_ON,       true);
    p.plate_tick_x_edges  = cfg_bool(cfg, K_PLATE_X_EDGES,  true);
    p.plate_tick_y_edges  = cfg_bool(cfg, K_PLATE_Y_EDGES,  true);
    p.plate_tick_origin   = cfg_bool(cfg, K_PLATE_ORIGIN,   true);
    p.plate_tick_display  = (cfg->get(K_PLATE_DISPLAY) == "on_plate")
                            ? TickDisplayMode::OnPlate
                            : TickDisplayMode::InlineWithObject;
    p.plate_divisions     = std::max(0, std::min(10, cfg_int(cfg, K_PLATE_DIVISIONS, 2)));
    p.plate_tick_opacity  = std::max(0.0f, std::min(1.0f,
                                cfg_int(cfg, K_PLATE_OPACITY, 100) / 100.0f));

    return p;
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

void PlaneHandlePrefs::save() const
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) return;

    // Plane handle
    switch (position) {
    case PlaneHandlePosition::AtIntersection: cfg->set(K_POSITION, "intersection"); break;
    case PlaneHandlePosition::Midpoint:       cfg->set(K_POSITION, "midpoint");     break;
    default:                                  cfg->set(K_POSITION, "at_arrow_end"); break;
    }
    cfg->set(K_SHAPE,       shape == PlaneHandleShape::Circle ? "circle" : "square");
    cfg->set(K_SIZE_PCT,    std::to_string(size_pct));
    cfg->set(K_PLANE_ALPHA, std::to_string(drag_plane_opacity));

    // Snap ticks global
    cfg->set(K_TICKS_ON,   ticks_enabled ? "1" : "0");
    cfg->set(K_TICK_STYLE, tick_style == TickStyle::FloatingLabel ? "floating" : "on_arrow");

    // Axis ticks
    cfg->set(K_AXIS_ON,        axis_ticks_enabled  ? "1" : "0");
    cfg->set(K_AXIS_X,         axis_tick_x         ? "1" : "0");
    cfg->set(K_AXIS_Y,         axis_tick_y         ? "1" : "0");
    cfg->set(K_AXIS_Z,         axis_tick_z         ? "1" : "0");
    cfg->set(K_AXIS_DISPLAY,   axis_tick_display == TickDisplayMode::OnPlate
                               ? "on_plate" : "inline");
    cfg->set(K_AXIS_START,     std::to_string(static_cast<int>(axis_tick_start + 0.5f)));
    cfg->set(K_AXIS_INCREMENT, std::to_string(static_cast<int>(axis_tick_increment * 10.0f + 0.5f)));
    cfg->set(K_AXIS_COUNT,     std::to_string(axis_tick_count));
    cfg->set(K_AXIS_OPACITY,   std::to_string(static_cast<int>(axis_tick_opacity  * 100.0f + 0.5f)));

    // Plate ticks
    cfg->set(K_PLATE_ON,       plate_ticks_enabled ? "1" : "0");
    cfg->set(K_PLATE_X_EDGES,  plate_tick_x_edges  ? "1" : "0");
    cfg->set(K_PLATE_Y_EDGES,  plate_tick_y_edges  ? "1" : "0");
    cfg->set(K_PLATE_ORIGIN,   plate_tick_origin   ? "1" : "0");
    cfg->set(K_PLATE_DISPLAY,  plate_tick_display == TickDisplayMode::OnPlate
                               ? "on_plate" : "inline");
    cfg->set(K_PLATE_DIVISIONS, std::to_string(plate_divisions));
    cfg->set(K_PLATE_OPACITY,  std::to_string(static_cast<int>(plate_tick_opacity * 100.0f + 0.5f)));
}

} // namespace GUI
} // namespace Slic3r
#include "PlaneHandlePrefs.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/AppConfig.hpp"

#include <glad/gl.h>
#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// PlaneHandlePrefs load / save
// ---------------------------------------------------------------------------

static constexpr const char* KEY_POSITION    = "plane_handle_position";
static constexpr const char* KEY_SHAPE       = "plane_handle_shape";
static constexpr const char* KEY_SIZE_PCT    = "plane_handle_size_pct";
static constexpr const char* KEY_PLANE_ALPHA = "plane_handle_drag_opacity";
static constexpr const char* KEY_TICKS_ON    = "gizmo_snap_ticks_enabled";
static constexpr const char* KEY_TICK_STYLE  = "gizmo_snap_tick_style";

PlaneHandlePrefs PlaneHandlePrefs::load()
{
    PlaneHandlePrefs prefs;
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) return prefs;

    const std::string pos_str = cfg->get(KEY_POSITION);
    if      (pos_str == "intersection") prefs.position = PlaneHandlePosition::AtIntersection;
    else if (pos_str == "midpoint")     prefs.position = PlaneHandlePosition::Midpoint;
    else                                prefs.position = PlaneHandlePosition::AtArrowEnd;

    prefs.shape = (cfg->get(KEY_SHAPE) == "circle")
                  ? PlaneHandleShape::Circle : PlaneHandleShape::Square;

    const std::string size_str = cfg->get(KEY_SIZE_PCT);
    if (!size_str.empty()) {
        try {
            float v = std::stof(size_str);
            prefs.size_pct = std::max(0.0f, std::min(500.0f, v));
        } catch (...) {}
    }

    const std::string alpha_str = cfg->get(KEY_PLANE_ALPHA);
    if (!alpha_str.empty()) {
        try {
            float v = std::stof(alpha_str);
            if (v > 1.0f) v /= 100.0f;
            prefs.drag_plane_opacity = std::max(0.0f, std::min(1.0f, v));
        } catch (...) {}
    }

    const std::string ticks_str = cfg->get(KEY_TICKS_ON);
    prefs.ticks_enabled = !(ticks_str == "0" || ticks_str == "false");

    prefs.tick_style = (cfg->get(KEY_TICK_STYLE) == "floating")
                       ? TickStyle::FloatingLabel : TickStyle::OnArrow;

    return prefs;
}

void PlaneHandlePrefs::save() const
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) return;

    switch (position) {
    case PlaneHandlePosition::AtIntersection: cfg->set(KEY_POSITION, "intersection"); break;
    case PlaneHandlePosition::Midpoint:       cfg->set(KEY_POSITION, "midpoint");     break;
    default:                                  cfg->set(KEY_POSITION, "at_arrow_end"); break;
    }

    cfg->set(KEY_SHAPE,       shape == PlaneHandleShape::Circle ? "circle" : "square");
    cfg->set(KEY_SIZE_PCT,    std::to_string(size_pct));
    cfg->set(KEY_PLANE_ALPHA, std::to_string(drag_plane_opacity));
    cfg->set(KEY_TICKS_ON,    ticks_enabled ? "1" : "0");
    cfg->set(KEY_TICK_STYLE,  tick_style == TickStyle::FloatingLabel ? "floating" : "on_arrow");
}

// ---------------------------------------------------------------------------
// GizmoSnapTicks color helpers
// ---------------------------------------------------------------------------

static const ColorRGBA AXES_COL[3] = {
    { 0.878f, 0.122f, 0.122f, 1.0f },
    { 0.122f, 0.698f, 0.122f, 1.0f },
    { 0.122f, 0.541f, 0.855f, 1.0f },
};
static const ColorRGBA AXES_HOV[3] = {
    { 1.0f,  0.4f,  0.4f,  1.0f },
    { 0.4f,  1.0f,  0.4f,  1.0f },
    { 0.4f,  0.75f, 1.0f,  1.0f },
};
static const ColorRGBA PLATE_COL     = { 0.937f, 0.624f, 0.153f, 1.0f };
static const ColorRGBA PLATE_HOV_COL = { 1.0f,   0.780f, 0.353f, 1.0f };

} // namespace GUI
} // namespace Slic3r
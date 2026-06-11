#include "PlaneHandlePrefs.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r {
namespace GUI {

// AppConfig key names
static constexpr const char* KEY_POSITION    = "plane_handle_position";
static constexpr const char* KEY_SHAPE       = "plane_handle_shape";
static constexpr const char* KEY_SIZE_PCT    = "plane_handle_size_pct";
static constexpr const char* KEY_PLANE_ALPHA = "plane_handle_drag_opacity";

PlaneHandlePrefs PlaneHandlePrefs::load()
{
    PlaneHandlePrefs prefs;
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg)
        return prefs;

    // Position
    const std::string pos_str = cfg->get(KEY_POSITION);
    if      (pos_str == "intersection") prefs.position = PlaneHandlePosition::AtIntersection;
    else if (pos_str == "midpoint")     prefs.position = PlaneHandlePosition::Midpoint;
    else                                prefs.position = PlaneHandlePosition::AtArrowEnd; // default

    // Shape
    const std::string shape_str = cfg->get(KEY_SHAPE);
    if (shape_str == "circle")  prefs.shape = PlaneHandleShape::Circle;
    else                        prefs.shape = PlaneHandleShape::Square; // default
    
    // Size percentage (default 100, clamped 0-500)
    const std::string size_str = cfg->get(KEY_SIZE_PCT);
    if (!size_str.empty()) {
        try {
            float v = std::stof(size_str);
            prefs.size_pct = std::max(0.0f, std::min(500.0f, v));
        } catch (...) {}
    }

    // Drag plane opacity stored as 0-100 integer by the spinctrl, converted to 0.0-1.0
    const std::string alpha_str = cfg->get(KEY_PLANE_ALPHA);
    if (!alpha_str.empty()) {
        try {
            // Handle both old float format ("0.1") and new integer format ("10")
            float v = std::stof(alpha_str);
            if (v > 1.0f) v /= 100.0f; // integer percent -> fraction
            prefs.drag_plane_opacity = std::max(0.0f, std::min(1.0f, v));
        } catch (...) {}
    }

    return prefs;
}

void PlaneHandlePrefs::save() const
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg)
        return;

    switch (position) {
    case PlaneHandlePosition::AtIntersection: cfg->set(KEY_POSITION, "intersection"); break;
    case PlaneHandlePosition::Midpoint:       cfg->set(KEY_POSITION, "midpoint");     break;
    default:                                  cfg->set(KEY_POSITION, "at_arrow_end"); break;
    }

    switch (shape) {
    case PlaneHandleShape::Circle: cfg->set(KEY_SHAPE, "circle"); break;
    default:                       cfg->set(KEY_SHAPE, "square"); break;
    }

    cfg->set(KEY_SHAPE,       shape == PlaneHandleShape::Circle ? "circle" : "square");
    cfg->set(KEY_SIZE_PCT,    std::to_string(size_pct));
    cfg->set(KEY_PLANE_ALPHA, std::to_string(drag_plane_opacity));
}

} // namespace GUI
} // namespace Slic3r
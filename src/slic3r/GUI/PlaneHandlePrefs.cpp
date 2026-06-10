#include "PlaneHandlePrefs.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r {
namespace GUI {

// AppConfig key names
static constexpr const char* KEY_POSITION = "plane_handle_position";
static constexpr const char* KEY_SHAPE    = "plane_handle_shape";

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
}

} // namespace GUI
} // namespace Slic3r
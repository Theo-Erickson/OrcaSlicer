#pragma once
// ORCA: CrashSlicer plane handle visual preferences
// Consumed by GLGizmoMove3D and GLGizmoScale3D.

namespace Slic3r {
namespace GUI {

// Where the plane handle square is placed relative to the gizmo.
enum class PlaneHandlePosition
{
    // Square floats just outside the bbox face, inset toward the axis corner
    // (the current default and the Blender-style placement).
    AtArrowEnd,

    // Square sits at the intersection of two axis lines, with one edge on
    // each axis. The near corner of the square is at the gizmo origin; the
    // two free edges run along the Y and Z axes (for the YZ handle, etc.).
    AtIntersection,

    // Square is centred at the midpoint between the gizmo origin and the
    // axis arrow tip — halfway along each free axis arrow.
    Midpoint,
};

// Visual shape used for the clickable plane handle.
enum class PlaneHandleShape
{
    Square,
    Circle,
};

// Lightweight value-type container.  GLGizmo*3D instances keep one copy
// and call PlaneHandlePrefs::load() at the start of on_render().
struct PlaneHandlePrefs
{
    PlaneHandlePosition position { PlaneHandlePosition::AtArrowEnd };
    PlaneHandleShape    shape    { PlaneHandleShape::Square };

    // Load current values from AppConfig.  Safe to call every frame; the
    // AppConfig get() path is a map lookup so it is cheap.
    static PlaneHandlePrefs load();

    // Persist current values to AppConfig (called from Preferences dialog).
    void save() const;

    bool operator==(const PlaneHandlePrefs& o) const
    {
        return position == o.position && shape == o.shape;
    }
    bool operator!=(const PlaneHandlePrefs& o) const { return !(*this == o); }
};

} // namespace GUI
} // namespace Slic3r
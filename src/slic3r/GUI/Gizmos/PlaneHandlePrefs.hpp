#pragma once
// ORCA: CrashSlicer -- PlaneHandlePrefs and GizmoSnapTicks
// Both defined here so gizmo headers only need one include.

#include "../GLModel.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

#include "GizmoEnums.hpp"

#include <chrono>
#include <string>
#include <vector>

// GLShaderProgram lives in ::Slic3r, not ::Slic3r::GUI
namespace Slic3r { class GLShaderProgram; }

namespace Slic3r {
namespace GUI {

class Camera;

// ---------------------------------------------------------------------------
// Plane handle visual preferences
// ---------------------------------------------------------------------------

struct PlaneHandlePrefs
{
    PlaneHandlePosition position           { PlaneHandlePosition::AtArrowEnd };
    PlaneHandleShape    shape              { PlaneHandleShape::Square };
    float               size_pct          { 100.0f };
    float               drag_plane_opacity{ 0.10f  };
    bool                ticks_enabled     { true   };
    TickStyle           tick_style        { TickStyle::OnArrow };

    static PlaneHandlePrefs load();
    void save() const;

    bool operator==(const PlaneHandlePrefs& o) const
    {
        return position           == o.position
            && shape              == o.shape
            && size_pct           == o.size_pct
            && drag_plane_opacity == o.drag_plane_opacity
            && ticks_enabled      == o.ticks_enabled
            && tick_style         == o.tick_style;
    }
    bool operator!=(const PlaneHandlePrefs& o) const { return !(*this == o); }
};

} // namespace GUI
} // namespace Slic3r
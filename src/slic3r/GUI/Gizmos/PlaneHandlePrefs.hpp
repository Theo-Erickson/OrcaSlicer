#pragma once
// ORCA: CrashSlicer plane handle and snap tick visual preferences.

#include "GizmoEnums.hpp"

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// Plane handle visual preferences
// ---------------------------------------------------------------------------

struct PlaneHandlePrefs
{
    // --- Plane handle ---
    PlaneHandlePosition position           { PlaneHandlePosition::AtArrowEnd };
    PlaneHandleShape    shape              { PlaneHandleShape::Square };
    float               size_pct          { 100.0f };
    float               drag_plane_opacity{ 0.10f  };

    // --- Snap ticks: global ---
    bool                ticks_enabled        { true  };
    TickStyle           tick_style           { TickStyle::OnArrow };

    // --- Snap ticks: axis ticks ---
    bool                axis_ticks_enabled   { true  };
    bool                axis_tick_x          { true  };
    bool                axis_tick_y          { true  };
    bool                axis_tick_z          { true  };
    TickDisplayMode     axis_tick_display    { TickDisplayMode::InlineWithObject };
    float               axis_tick_start      { 1.0f  }; // first multiple
    float               axis_tick_increment  { 1.0f  }; // step between ticks
    int                 axis_tick_count      { 5     }; // ticks per direction
    float               axis_tick_opacity    { 1.0f  }; // 0-1

    // --- Snap ticks: plate ticks ---
    bool                plate_ticks_enabled  { true  };
    bool                plate_tick_x_edges   { true  };
    bool                plate_tick_y_edges   { true  };
    bool                plate_tick_origin    { true  }; // XY center point
    TickDisplayMode     plate_tick_display   { TickDisplayMode::InlineWithObject };
    int                 plate_divisions      { 2     }; // interior divisions per half (0-10)
    float               plate_tick_opacity   { 1.0f  }; // 0-1

    static PlaneHandlePrefs load();
    void save() const;

    bool operator==(const PlaneHandlePrefs& o) const
    {
        return position            == o.position
            && shape               == o.shape
            && size_pct            == o.size_pct
            && drag_plane_opacity  == o.drag_plane_opacity
            && ticks_enabled       == o.ticks_enabled
            && tick_style          == o.tick_style
            && axis_ticks_enabled  == o.axis_ticks_enabled
            && axis_tick_x         == o.axis_tick_x
            && axis_tick_y         == o.axis_tick_y
            && axis_tick_z         == o.axis_tick_z
            && axis_tick_display   == o.axis_tick_display
            && axis_tick_start     == o.axis_tick_start
            && axis_tick_increment == o.axis_tick_increment
            && axis_tick_count     == o.axis_tick_count
            && axis_tick_opacity   == o.axis_tick_opacity
            && plate_ticks_enabled == o.plate_ticks_enabled
            && plate_tick_x_edges  == o.plate_tick_x_edges
            && plate_tick_y_edges  == o.plate_tick_y_edges
            && plate_tick_origin   == o.plate_tick_origin
            && plate_tick_display  == o.plate_tick_display
            && plate_divisions     == o.plate_divisions
            && plate_tick_opacity  == o.plate_tick_opacity;
    }
    bool operator!=(const PlaneHandlePrefs& o) const { return !(*this == o); }
};

} // namespace GUI
} // namespace Slic3r
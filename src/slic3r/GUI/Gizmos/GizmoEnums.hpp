#pragma once
// ORCA: Shared enums for gizmo plane handles and snap ticks.
// Kept in a separate header with no heavy dependencies so both
// GizmoSnapTicks.hpp and PlaneHandlePrefs.hpp can include it safely.

namespace Slic3r {
namespace GUI {

enum class PlaneHandlePosition { AtArrowEnd, AtIntersection, Midpoint };
enum class PlaneHandleShape    { Square, Circle };
enum class TickStyle           { OnArrow, FloatingLabel };
enum class TickDisplayMode     { InlineWithObject, OnPlate };


} // namespace GUI
} // namespace Slic3r
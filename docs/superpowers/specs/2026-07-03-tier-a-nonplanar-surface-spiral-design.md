# Tier A — Nonplanar Surface Spiral (Design Spec)

Date: 2026-07-03
Branch: `NonPlanarSlicingFeature`
Status: Approved for implementation (phased)

## Goal

Add a `SurfaceSpiral` value to `nonplanar_mode` that replaces the top-cap toolpaths of an
auto-detected rotationally-symmetric dome with a single continuous Archimedean spiral whose
Z follows the mesh surface. Target hardware: standard 3-axis Cartesian (Bambu P1S, Prusa MK4).
Risky by nature; guarded, not prevented.

## Non-negotiable guarantee

With `nonplanar_slicing` disabled — or in any mode other than `SurfaceSpiral` — g-code output
is **byte-identical** to today. Enforced by a regression test. (AGENTS.md: features gated by
options must not affect behavior when disabled.)

## Modes (nonplanar_mode)

| Value | Behavior | Kind |
|-------|----------|------|
| standard | normal slicing (default) | — |
| normal_interpolation | Z-warp from surface normal angle | warp planar paths |
| surface_raycast | Z-warp from actual surface height (real vertical ray) | warp planar paths |
| **surface_spiral** (new) | generate Archimedean climbing spiral on detected dome cap | new toolpaths |

The first three lift existing planar rings; only `surface_spiral` generates new toolpaths.

## Architecture (Option 3: dedicated module + thin export hook)

- `libslic3r/NonplanarSurface.{hpp,cpp}` — surface Z queries. **Moved out of GUI**, real vertical ray.
- `libslic3r/NonplanarSpiral.{hpp,cpp}` (new) — pure geometry: `detect_region()`, `generate_spiral()`,
  `clearance_check()`. No g-code, no GUI. Unit-testable on a synthetic hemisphere.
- `libslic3r/GCode.cpp` — thin hook: build spiral once in `_do_export`; in the layer loop, skip
  normal cap emission for region layers and inject the spiral, bracketed by `NP flatZ=` / `NP end`.
- `libslic3r/GCode/GCodeProcessor.cpp` — classify `NonplanarExtrusion` moves (dedupe existing handler).
- GUI — `LibVGCodeWrapper` distinct color; design-time GLCanvas overlay of region + collision hot-spots.

## Region detection

```
struct NonplanarTopRegion { int first_layer, last_layer; Vec2d center; double base_radius, apex_z; };
```
Auto: scan top-down; a layer qualifies if its dominant outer contour is a single island with
circularity `4*pi*area/perimeter^2 > 0.85` and radius non-increasing upward. Take the maximal
consecutive run. `center` = mean centroid; `base_radius` = radius at lowest qualifying layer.
Manual override config forces/adjusts center, base_radius, layer range (and can disable auto).
No region + no override → no-op, normal slicing, one-line g-code note.

## Spiral generation

Archimedean, θ ∈ [0, 2π·N], revolutions `N = base_radius / line_width` (radial pitch = line width,
fills the cap). `r(θ)` shrinks linearly base_radius → ~0 at apex; angular step `2π / points_per_rev`
(default 360). Each point: `x,y = center + r(cosθ,sinθ)`, `z = surface_z_at(x,y)` (vertical ray from
above the mesh bbox). Starts at the base seam, blends flat→surface over `transition_revs` (default 0.5),
climbs to apex. E per segment from **3D** arc length × flow (`line_width × layer_height` bead area —
acknowledged Tier A approximation on slopes). Returns `std::vector<Vec3d>` in plate-space mm.

## Collision clearance

Nozzle = tip + cone (taper) + cylinder (heater/shroud) of radius `clearance_radius`, height
`clearance_height` (new config, default from nozzle geometry). For each spiral point, sample mesh
surface height within the cylinder footprint via the AABB tree; if surrounding printed surface rises
above the nozzle-body clearance plane, flag collision. Response: raise (clamp) that point's Z to clear
and mark it; if flagged points exceed a threshold, shrink region / reduce max slope and emit a prominent
warning in the g-code header and UI. Heuristic, not a proof.

## Viewer

- Distinct color: stop aliasing `NonplanarExtrusion → Extrude` in `LibVGCodeWrapper`; own legend entry.
  Exact mechanism (extend libvgcode move-type/color table vs. role-color override) confirmed during impl.
- Design-time overlay: GLCanvas overlay showing detected cap (shaded + base circle + apex marker) and
  red markers at collision-flagged points; toggled from nonplanar settings. Data from `NonplanarSpiral`.
- Compare workflow: slice with `nonplanar_slicing` off vs on and compare previews (no synchronized
  side-by-side viewer in Tier A — out of scope).

## Config additions

`nonplanar_mode += surface_spiral`; `nonplanar_spiral_points_per_rev` (int, 360);
`nonplanar_spiral_transition_revs` (float, 0.5); `nonplanar_spiral_auto_detect` (bool, true) + override
fields (center x/y, base radius, layer range); `nonplanar_clearance_radius` / `nonplanar_clearance_height`
(mm); GUI-only overlay toggle.

## Foundation fixes (Phase 0, prerequisite)

1. Move `NonplanarSurface.{hpp,cpp}` → `src/libslic3r/`; update both CMakeLists; fix includes in
   `GCode.hpp`/`GCode.cpp` (drop duplicate/relative include). Unblocks `tests/libslic3r` linking.
2. `raycast_surface_z`: real `intersect_ray_first_hit` straight down; delete lateral-tolerance hack.
3. Lifted-move E computed from 3D length (parity with `z_contoured`/`sloped` branches).
4. Downgrade per-point `BOOST_LOG_TRIVIAL(warning)` spam to `trace`; remove duplicate NP tag handler.
5. Re-point `PrintObject` invalidation for `nonplanar_*` keys to `psGCodeExport` (not `posSlice`).

## Testing

- Unit tests in `tests/libslic3r` (enabled by Phase 0 move): hemisphere detection finds cap; every
  spiral point on the sphere within tolerance; radius monotonic; path continuous.
- Ray-vs-sloped-plane test (old closest-point code would fail it).
- Collision test: tall thin dome flags; shallow dome doesn't.
- Regression: disabled output byte-identical.
- GUI acceptance: slice `nonplanar_test_models/*.stl`, inspect climbing Z in preview.

## Implementation phasing

Phase 0 foundation → 1 region detection → 2 spiral → 3 clearance → 4 emission hook → 5 viewer color
→ 6 overlay. Each independently testable, each gated by `surface_spiral`.

## Test models

`nonplanar_test_models/`: hemisphere.stl, sphere.stl, dome_on_cylinder.stl (should spiral);
cone.stl (apex edge case); cube.stl (should not trigger — fallback).

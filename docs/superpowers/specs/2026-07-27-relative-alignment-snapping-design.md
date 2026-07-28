# Relative Alignment & Snapping — Design Spec

**Date:** 2026-07-27
**Branch:** RelativeAlignmentFeature
**Status:** Approved design, pending implementation plan

## Summary

Add an object-alignment snapping system to OrcaSlicer's 3D plate view. When enabled,
dragging an object around the bed snaps its axis-aligned bounding box to nearby objects —
aligning edges and centers, butting objects into contact, and continuing existing evenly
spaced rows. On-screen guides (alignment lines, ghosted neighbor boxes, spacing badges)
show what the drag is aligning to. A toolbar button opens a settings panel controlling the
behavior, and individual objects can be opted out via the right-click menu.

The feature is fully inert when disabled: with the master toggle off (or **Alt** held during
a drag), dragging behaves exactly as it does today.

## Scope

**In scope**
- XY bed-plane alignment only (top-down footprint of each object's bounding box). No Z/stacking.
- Snap references: **edge alignment** (collinear same-side edges), **center alignment**
  (shared centerline), **contact** (edges touching, 0 gap).
- **Spacing propagation (row extend):** detect an existing evenly spaced, aligned row of
  objects and snap the dragged object to continue the same spacing on the same row.
- Live snapping while dragging, with **Alt** held to suppress.
- Settings panel (master toggle, sensitivity, strength, per-feature toggles) behind a
  toolbar button.
- Per-object opt-out via right-click context menu.
- On-screen guides: alignment lines, ghosted neighbor bounding boxes, snapped highlight,
  spacing badges.

**Out of scope (explicitly deferred)**
- Z-axis / 3D snapping and stacking.
- Fixed-gap adjacency (snapping to a configured non-zero spacing).
- Snapping to the build plate itself (its edges/center).
- Inserting *between* existing row members (only end-extension of a row is supported now).
  Noted as an easy future add-on.

## Requirements traceability

| # | User requirement | Covered by |
|---|------------------|-----------|
| 1 | Snap when bounding boxes touch | Contact snap (Section 2) |
| 2 | Align edges or centers across a gap | Edge & center align (Section 2) |
| 3 | Visualize nearby object bounding box while dragging | Ghost boxes (Sections 2, 3) |
| 4 | Indicator lines showing what/where you're aligning to | Alignment lines + badges (Sections 2, 3) |
| 5 | Settings for sensitivity and strength | Settings panel (Section 4) |
| 6 | Per-object enable/disable for snapping | Object metadata + context menu (Sections 1, 5) |
| 7 | Propagate an existing alignment (row) | Spacing propagation (Section 2) |

## Architecture

**Approach: dedicated snap module with a pure-geometry core plus a thin GUI integration
layer.** Chosen over inlining into `GLCanvas3D`/`Selection` (untestable, bloats a ~10k-line
file) and over implementing as a Gizmo (wrong interaction shape — snapping must work during
normal free-drag, not a separate mode).

- **Pure geometry core** — no OpenGL/wx dependencies; takes bounding boxes + settings,
  returns a corrected translation delta and a list of guides. Unit-testable with Catch2.
- **GUI integration layer** — GLCanvas3D calls the core at two seams (drag correction,
  render), plus the settings panel, toolbar button, and context-menu toggle.

## Section 1 — Data model & settings

### `SnapSettings` (persisted to `AppConfig`, mirroring `ArrangeSettings`)

| Field | Meaning | Default |
|-------|---------|---------|
| `enabled` | Master on/off (panel checkbox) | on |
| `sensitivity_px` | Capture distance, in **screen pixels** | conservative (e.g. 8 px) |
| `strength_px` | Extra break-away margin for hysteresis, in pixels | e.g. 6 px |
| `edge_align` | Edge alignment toggle | on |
| `center_align` | Center alignment toggle | on |
| `contact` | Contact/adjacency toggle | on |
| `spacing_propagation` | Row-extend toggle (req #7) | on |

Stored under `snap_align_*` keys in AppConfig. Defaults are safe; because the master toggle
governs all behavior, existing users see no change until they enable it. (Default master
state — on vs off out of the box — to be finalized in the plan; leaning **off** for a new
opt-in feature.)

### Per-object exclusion flag (req #6)

Stored as an object **metadata key** `snap_alignment_enabled` in the `.3mf`, defaulting to
**true when absent**. Rationale: backward compatibility. Old project files simply lack the
key and behave as "enabled" — no migration, no `.3mf` format break. The right-click toggle
reads/writes this key.

### Runtime drag state (not serialized)

Lives in the snap module: the currently-engaged snap on each axis (for hysteresis across
drag frames) and the list of guides to render for the current frame. Reset on mouse-up.

## Section 2 — Geometry core (pure, testable)

Unit `AlignmentSnap` in `src/slic3r/GUI/`, core function free of GL/wx deps:

```
SnapResult compute_snap(
    const BoundingBox2& mover,            // dragged selection's XY footprint
    const std::vector<Neighbor>& targets, // other objects' XY boxes (excluded ones removed)
    const Vec2d& raw_delta,               // unsnapped translation this frame
    const SnapSettings& s,
    double px_per_mm,                     // pixel<->mm at current zoom
    SnapState& state);                    // in/out: engaged snaps, for hysteresis
```

**Algorithm — X and Y resolved independently** (so a drag can be edge-aligned in X and
contact-snapped in Y simultaneously):

1. **Candidate lines.** Mover key values `{min, center, max}` per axis; same per target.
   Generate candidates:
   - **Edge align:** `min↔min`, `max↔max`
   - **Center align:** `center↔center`
   - **Contact:** `mover.min↔target.max` and `mover.max↔target.min`, **gated on
     perpendicular-axis overlap** so it only fires when boxes are genuinely side-by-side.
2. **Filter by sensitivity.** Convert each candidate's offset to pixels via `px_per_mm`;
   keep those within `sensitivity_px`.
3. **Pick best per axis.** Smallest offset wins on X; independently on Y.
4. **Hysteresis (strength).** If an axis was engaged last frame, keep it engaged until the
   raw position drifts beyond `sensitivity_px + strength_px`. This is the "stickiness."
5. **Spacing propagation (req #7).** Among targets forming an aligned **row** on an axis
   (sharing a perpendicular edge/center — the "require aligned" rule), detect equal spacing
   between consecutive members, project the next slot, and if the mover is within
   sensitivity add it as a candidate and emit spacing badges. **Extend-only** for now.

**Output `SnapResult`:**
- Corrected translation delta.
- `guides` — typed list of `{alignment_line, spacing_badge, ghost_box}` entries carrying the
  geometry needed to draw each. **Ghost boxes** (req #3) are emitted for any target within a
  proximity radius (a few × sensitivity) so nearby boxes reveal as the drag approaches.

Deterministic; fully unit-testable with fabricated boxes.

## Section 3 — GUI integration (two seams in GLCanvas3D)

**Seam 1 — Drag correction.** In `GLCanvas3D::on_mouse`, the free-drag branch
(`GLCanvas3D.cpp:4603`) currently calls
`m_selection.translate(cur_pos - start_position_3D, trafo_type)`. Insert one step before it:
- If `SnapSettings.enabled` and **Alt not held**: build the mover box from the selection's
  XY bbox, gather neighbor boxes (all other object instances on the current plate, minus
  excluded), call `compute_snap`, and replace the raw XY delta with the corrected one. Z is
  untouched.
- If disabled or Alt held: unchanged raw behavior — feature fully inert when off.
- Returned `guides` are stashed on the canvas for the render pass; cleared on mouse-up.
- **Neighbor boxes are gathered once at drag-start** (only the dragged object moves), then
  only the mover updates per frame — bounding per-frame cost.

**Seam 2 — Rendering.** A new `render_snap_guides()` runs after volumes are drawn:
- Alignment lines and ghost bounding boxes as GL line primitives on the bed plane (z just
  above bed), via existing line-rendering helpers.
- Spacing badges / distance labels via the existing ImGui overlay text path.
- All colors and line thicknesses come from **named constants in one place**, so restyling
  is a one-line change (per the "keep colors adjustable" note).

## Section 4 — Settings panel & toolbar button

- **Toolbar button "Snapping"** on the top toolbar (alongside Arrange/Add). Opens the
  settings panel; reflects master-enabled state (highlighted when on).
- **Settings panel** — ImGui popup rendered from GLCanvas3D, modeled on the existing
  arrange-settings window (`GLCanvas3D.cpp:5988`). Contents:
  - ☑ Enable snap alignment (master)
  - Sensitivity slider (px), Strength slider (px)
  - ☑ Edge alignment  ☑ Center alignment  ☑ Contact
  - ☑ Propagate spacing (row)
  - Hint: "Hold **Alt** while dragging to disable snapping."
  - Changes write straight through to `AppConfig` (immediate-persist, no OK/Cancel).

## Section 5 — Per-object exclusion UI

- Checkable **"Enable snap alignment"** item on the object context menu (right-click in the
  object list / on the plate), operating on the whole current selection.
- Checked = participating (default); unchecked = fully opted out as both target and mover.
- Reads/writes `snap_alignment_enabled` object metadata (Section 1).
- Seam 1's neighbor-gathering filters out excluded objects; dragging an excluded object
  skips snapping entirely.

## Section 6 — Testing & verification

**Catch2 unit tests** on the geometry core (no GL):
- Edge align, center align, contact (incl. perpendicular-overlap gating).
- Independent X/Y resolution.
- Hysteresis break-away.
- Spacing propagation: row detection + end-extension.
- Excluded-object filtering.
- No candidate → raw delta returned unchanged.

**Manual verification checklist** (in the PR) for GL/ImGui pieces:
- Alt-suppress during drag.
- Guide rendering (lines, ghost boxes, badges), ghost-box reveal on approach.
- Toolbar button + panel; settings persist across app restart.
- `.3mf` round-trip with new metadata; old file loads as enabled.

**Backward-compat checks:**
- Feature off → drag behaves identically to today.
- Old `.3mf` loads without the key and defaults to enabled.

## Cross-platform notes

Geometry core is portable (no platform deps). Rendering uses existing GL/ImGui helpers
already cross-platform. AppConfig and 3mf metadata paths are shared across Windows, macOS,
Linux. No platform-specific code anticipated.

## Open items to resolve in the plan

- Final default of the master toggle (out-of-box on vs off) — leaning off (opt-in).
- Exact default values for `sensitivity_px` / `strength_px`.
- Toolbar icon asset for the Snapping button.

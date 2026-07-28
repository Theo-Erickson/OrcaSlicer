# Relative Alignment & Snapping — Manual Verification

Feature branch: `RelativeAlignmentFeature`. Spec + plan under `docs/superpowers/`.

## Automated (done)
- [x] `libslic3r_tests.exe "[AlignmentSnap]"` — 15 cases / 36 assertions pass (geometry core).
- [x] `libslic3r_gui` compiles clean (GUI wiring).
- [ ] Full `OrcaSlicer` app links (run once; see build log).

Note: the `test_3mf.cpp` "2D convex hull" case fails on Windows due to an unconditional
`/tmp/orca.ascii` write (`test_3mf.cpp:149`) — pre-existing/environmental, unrelated to this feature.

## Settings panel & persistence
- [ ] A "Snapping" button appears on the top toolbar; clicking it opens the "Snap options" panel.
- [ ] Panel shows: Enable snap alignment, Sensitivity (px), Strength (px), Edge/Center/Contact,
      Propagate spacing (row), and the "Hold Alt..." hint.
- [ ] Toggling options then restarting OrcaSlicer preserves values (AppConfig group `snap_align`).
- [ ] With the master toggle OFF (fresh-config default), dragging behaves exactly as before this feature.

## Drag behavior (enable snapping first)
- [ ] Edge align: drag a cube so an edge nears a neighbor's edge → snaps flush.
- [ ] Center align: two different-size boxes share a centerline.
- [ ] Contact: boxes butt together with no gap; does NOT fire when they don't overlap perpendicular.
- [ ] Row propagation: two aligned, evenly spaced boxes → a third snaps to continue the spacing;
      pink spacing badges show the gap value.
- [ ] Hold **Alt** during a drag → no snapping (free placement).
- [ ] Raise Strength → an engaged snap is harder to break away from.

## Guides (screen-space overlay during drag)
- [ ] Green alignment line appears through the aligned edge/center.
- [ ] Gray ghost rectangles reveal for nearby neighbor footprints.
- [ ] Pink spacing badges + distance text during row snapping.
- [ ] All guides clear on mouse-up.

## Per-object opt-out
- [ ] Right-click an object → "Enable snap alignment" appears, checked by default.
- [ ] Uncheck it → that object is ignored as a snap target, and dragging it does not snap.
- [ ] Re-check restores participation.

## Backward compatibility
- [ ] Old `.3mf` (no key) loads with all objects enabled.
- [ ] Save a project with one object opted out, reload → the opt-out persists.
- [ ] Default `.3mf` output for untouched objects is unchanged (the key is written only when disabled).

## Known follow-ups (not blocking)
- Toolbar icon is a copy of the arrange icon (`toolbar_snap*.svg`) — replace with bespoke art.
- Guide lines/ghosts use the ImGui foreground draw list projected via `CameraUtils::project`
  (screen-space), rather than GL bed-plane primitives — revisit if a perspective camera shows skew.
- Insert-between-a-row (vs only extending) deferred; fixed-gap and plate snapping out of scope.

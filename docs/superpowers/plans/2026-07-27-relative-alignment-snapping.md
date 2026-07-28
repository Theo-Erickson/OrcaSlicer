# Relative Alignment & Snapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in bounding-box snapping/alignment when dragging objects on the OrcaSlicer bed — edge align, center align, contact, and row-spacing propagation — with on-screen guides, a settings panel, and per-object opt-out.

**Architecture:** A dependency-free geometry core in `libslic3r` (`AlignmentSnap`) computes a corrected drag delta plus a list of guides from bounding boxes and settings; it is unit-tested with Catch2. A thin GUI layer in `slic3r/GUI` calls the core at two seams in `GLCanvas3D` (drag correction, guide rendering), adds a toolbar button + ImGui settings panel mirroring the existing Arrange menu, and a right-click context-menu opt-out backed by a persisted `ModelObject` flag.

**Tech Stack:** C++17, Eigen (`Vec2d`, `BoundingBoxf`), wxWidgets, ImGui (`ImGuiWrapper`), Catch2, CMake, AppConfig, 3mf format.

---

## Design reference

Spec: `docs/superpowers/specs/2026-07-27-relative-alignment-snapping-design.md`

## File structure

**Create:**
- `src/libslic3r/AlignmentSnap.hpp` — core types + `compute_snap` declaration.
- `src/libslic3r/AlignmentSnap.cpp` — core implementation (no GL/wx).
- `tests/libslic3r/test_alignment_snap.cpp` — Catch2 unit tests for the core.

**Modify:**
- `src/libslic3r/CMakeLists.txt` — add the two core source files.
- `tests/libslic3r/CMakeLists.txt` — add the test source file.
- `src/libslic3r/Model.hpp` / `Model.cpp` — `ModelObject::snap_alignment_enabled` member + copy handling.
- `src/libslic3r/Format/3mf.cpp` — persist/restore the per-object flag.
- `src/slic3r/GUI/GLCanvas3D.hpp` / `GLCanvas3D.cpp` — `SnapSettings`, load/save, toolbar item, `_render_snap_menu`, drag seam, `render_snap_guides`, neighbor cache.
- `src/slic3r/GUI/GUI_Factories.cpp` — context-menu opt-out item.

## Type reference (used across tasks — names are fixed here)

```cpp
// namespace Slic3r::AlignmentSnap
struct SnapSettings {
    bool   enabled             = false; // master toggle (opt-in: default OFF)
    double sensitivity_px      = 8.0;   // capture distance in screen pixels
    double strength_px         = 6.0;   // extra break-away margin (hysteresis)
    bool   edge_align          = true;
    bool   center_align        = true;
    bool   contact             = true;
    bool   spacing_propagation = true;
};
struct Neighbor { BoundingBoxf bbox; int id = -1; };
enum class Axis { X = 0, Y = 1 };
struct GuideLine   { Axis axis; double coord; double span_lo; double span_hi; };
struct SpacingBadge{ Vec2d a; Vec2d b; double value_mm; };
struct GhostBox    { BoundingBoxf bbox; };
struct SnapResult  {
    Vec2d corrected_delta { 0.0, 0.0 };
    std::vector<GuideLine>    lines;
    std::vector<SpacingBadge> badges;
    std::vector<GhostBox>     ghosts;
    bool engaged[2] = { false, false };
};
struct SnapState   { bool engaged[2] = { false, false }; double coord[2] = { 0.0, 0.0 }; };

SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state);
```

**Conventions used by every test:** boxes are given in mm; tests pass `px_per_mm = 1.0` so pixel thresholds equal mm thresholds and assertions stay readable. `BoundingBoxf` has `Vec2d min, max`; `bbox.center()` returns the midpoint.

---

## Task 1: Core header + skeleton + build wiring

**Files:**
- Create: `src/libslic3r/AlignmentSnap.hpp`
- Create: `src/libslic3r/AlignmentSnap.cpp`
- Create: `tests/libslic3r/test_alignment_snap.cpp`
- Modify: `src/libslic3r/CMakeLists.txt`
- Modify: `tests/libslic3r/CMakeLists.txt`

- [ ] **Step 1: Write the header**

`src/libslic3r/AlignmentSnap.hpp`:
```cpp
#ifndef slic3r_AlignmentSnap_hpp_
#define slic3r_AlignmentSnap_hpp_

#include <vector>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace AlignmentSnap {

struct SnapSettings {
    bool   enabled             = false;
    double sensitivity_px      = 8.0;
    double strength_px         = 6.0;
    bool   edge_align          = true;
    bool   center_align        = true;
    bool   contact             = true;
    bool   spacing_propagation = true;
};

struct Neighbor { BoundingBoxf bbox; int id = -1; };

enum class Axis { X = 0, Y = 1 };

struct GuideLine    { Axis axis; double coord; double span_lo; double span_hi; };
struct SpacingBadge { Vec2d a; Vec2d b; double value_mm; };
struct GhostBox     { BoundingBoxf bbox; };

struct SnapResult {
    Vec2d corrected_delta { 0.0, 0.0 };
    std::vector<GuideLine>    lines;
    std::vector<SpacingBadge> badges;
    std::vector<GhostBox>     ghosts;
    bool engaged[2] = { false, false };
};

struct SnapState { bool engaged[2] = { false, false }; double coord[2] = { 0.0, 0.0 }; };

// Returns raw_delta corrected so the moved bbox snaps to targets, plus guides to draw.
SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state);

}} // namespace Slic3r::AlignmentSnap
#endif
```

- [ ] **Step 2: Write the skeleton implementation (returns raw delta, no snapping yet)**

`src/libslic3r/AlignmentSnap.cpp`:
```cpp
#include "libslic3r/AlignmentSnap.hpp"

namespace Slic3r { namespace AlignmentSnap {

SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state)
{
    SnapResult r;
    r.corrected_delta = raw_delta;
    (void)mover_start; (void)targets; (void)s; (void)px_per_mm; (void)state;
    return r;
}

}} // namespace Slic3r::AlignmentSnap
```

- [ ] **Step 3: Add the sources to the libslic3r build**

In `src/libslic3r/CMakeLists.txt`, find the alphabetical group of source entries (each line looks like `    Point.cpp\n    Point.hpp`). Add, matching indentation, near the other `A*` files:
```
    AlignmentSnap.cpp
    AlignmentSnap.hpp
```

- [ ] **Step 4: Add the test file to the test build**

In `tests/libslic3r/CMakeLists.txt`, find the `add_executable(${_TEST_NAME}_tests ... )` list of `test_*.cpp` files and add, matching indentation:
```
    test_alignment_snap.cpp
```

- [ ] **Step 5: Write the first (smoke) test**

`tests/libslic3r/test_alignment_snap.cpp`:
```cpp
#include <catch2/catch.hpp>
#include "libslic3r/AlignmentSnap.hpp"

using namespace Slic3r;
using namespace Slic3r::AlignmentSnap;

static BoundingBoxf box(double x0, double y0, double x1, double y1) {
    return BoundingBoxf(Vec2d(x0, y0), Vec2d(x1, y1));
}

TEST_CASE("no targets returns raw delta unchanged", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true;
    SnapState st;
    SnapResult r = compute_snap(box(0,0,10,10), Vec2d(3.0, 4.0), {}, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(3.0));
    REQUIRE(r.corrected_delta.y() == Approx(4.0));
}
```

- [ ] **Step 6: Configure + build the test target, run it**

Run (Windows):
```bash
cmake --build build --config RelWithDebInfo --target libslic3r_tests -- -m
```
Expected: build succeeds; then run
```bash
ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure
```
Expected: 1 test passes.

- [ ] **Step 7: Commit**

```bash
git add src/libslic3r/AlignmentSnap.hpp src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp src/libslic3r/CMakeLists.txt tests/libslic3r/CMakeLists.txt
git commit -m "feat(align-snap): core module skeleton + build wiring"
```

---

## Task 2: Edge alignment (single axis)

**Files:**
- Modify: `src/libslic3r/AlignmentSnap.cpp`
- Test: `tests/libslic3r/test_alignment_snap.cpp`

- [ ] **Step 1: Write the failing tests**

Append to the test file:
```cpp
TEST_CASE("left edges align across a gap", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    // target left edge at x=0; mover left edge would land at x=2 (2mm away) -> snap to 0.
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(0,40,10,50), 1 } };
    SnapResult r = compute_snap(mover, Vec2d(2.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(0.0)); // pulled back to align left edges
    REQUIRE(r.engaged[0] == true);
}

TEST_CASE("right edges align across a gap", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    // mover right edge starts at 10; target right edge at 13; raw delta +2 -> 12, within 5 of 13 -> snap.
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(3,40,13,50), 1 } };
    SnapResult r = compute_snap(mover, Vec2d(2.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(3.0)); // right edge -> 13
    REQUIRE(r.engaged[0] == true);
}

TEST_CASE("edge beyond sensitivity does not snap", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(0,40,10,50), 1 } };
    SnapResult r = compute_snap(mover, Vec2d(20.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(20.0));
    REQUIRE(r.engaged[0] == false);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: FAIL (edges not snapped — corrected_delta still raw).

- [ ] **Step 3: Implement edge alignment**

Replace the body of `compute_snap` in `AlignmentSnap.cpp` with:
```cpp
namespace {
    struct Cand { double correction; double dist_px; };
    inline double lo(const BoundingBoxf& b, int a){ return a==0 ? b.min.x() : b.min.y(); }
    inline double hi(const BoundingBoxf& b, int a){ return a==0 ? b.max.x() : b.max.y(); }
    inline double ctr(const BoundingBoxf& b,int a){ return a==0 ? b.center().x() : b.center().y(); }
}

SnapResult compute_snap(const BoundingBoxf& mover_start,
                        const Vec2d& raw_delta,
                        const std::vector<Neighbor>& targets,
                        const SnapSettings& s,
                        double px_per_mm,
                        SnapState& state)
{
    SnapResult r;
    r.corrected_delta = raw_delta;
    if (!s.enabled || targets.empty() || px_per_mm <= 0.0)
        return r;

    const double thresh_mm = s.sensitivity_px / px_per_mm;

    // Mover box at the raw (unsnapped) position.
    BoundingBoxf mover = mover_start;
    mover.min += raw_delta; mover.max += raw_delta;

    for (int a = 0; a < 2; ++a) {
        Cand best { 0.0, s.sensitivity_px + 1.0 };
        bool found = false;
        for (const Neighbor& n : targets) {
            auto consider = [&](double mover_key, double target_key) {
                double correction = target_key - mover_key;      // mm to add on this axis
                double dpx = std::abs(correction) * px_per_mm;
                if (dpx <= s.sensitivity_px && dpx < best.dist_px) {
                    best = { correction, dpx }; found = true;
                }
            };
            if (s.edge_align) {
                consider(lo(mover,a), lo(n.bbox,a)); // min <-> min
                consider(hi(mover,a), hi(n.bbox,a)); // max <-> max
            }
        }
        if (found) {
            if (a == 0) r.corrected_delta.x() += best.correction;
            else        r.corrected_delta.y() += best.correction;
            r.engaged[a] = true;
        }
    }
    (void)state;
    return r;
}
```
Add `#include <cmath>` and `#include <algorithm>` at the top of the file.

- [ ] **Step 4: Run to verify pass**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: all edge tests PASS, smoke test still PASS.

- [ ] **Step 5: Commit**

```bash
git add src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp
git commit -m "feat(align-snap): edge alignment on both axes"
```

---

## Task 3: Center alignment

**Files:**
- Modify: `src/libslic3r/AlignmentSnap.cpp`
- Test: `tests/libslic3r/test_alignment_snap.cpp`

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("centers align when centerlines are close", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.edge_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    // mover center x = 5; target (width 20) center x = 6; raw 0 -> snap center to 6 (correction +1)
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(-4,40,16,50), 1 } }; // center x = 6
    SnapResult r = compute_snap(mover, Vec2d(0.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(1.0));
    REQUIRE(r.engaged[0] == true);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `ctest --test-dir build/tests/libslic3r -R "centers align" --output-on-failure`
Expected: FAIL (correction 0.0, not 1.0).

- [ ] **Step 3: Implement — add center candidates**

In `compute_snap`, inside the `for (const Neighbor& n : targets)` loop, after the `if (s.edge_align) { ... }` block add:
```cpp
            if (s.center_align)
                consider(ctr(mover,a), ctr(n.bbox,a)); // center <-> center
```

- [ ] **Step 4: Run to verify pass**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**
```bash
git add src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp
git commit -m "feat(align-snap): center alignment"
```

---

## Task 4: Contact snapping with perpendicular-overlap gating

**Files:**
- Modify: `src/libslic3r/AlignmentSnap.cpp`
- Test: `tests/libslic3r/test_alignment_snap.cpp`

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("contact snaps edges to touch when overlapping perpendicular", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.edge_align = false; s.center_align = false; s.spacing_propagation = false;
    SnapState st;
    // target occupies x[13..23], y[0..10]; mover x[0..10] y[0..10]; raw +2 -> right edge 12, target left 13 -> touch.
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(13,0,23,10), 1 } };
    SnapResult r = compute_snap(mover, Vec2d(2.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(3.0)); // mover right edge -> 13
    REQUIRE(r.engaged[0] == true);
}

TEST_CASE("contact does NOT snap without perpendicular overlap", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.edge_align = false; s.center_align = false; s.spacing_propagation = false;
    SnapState st;
    // same X proximity but target is far in Y (no vertical overlap) -> no contact snap.
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(13,80,23,90), 1 } };
    SnapResult r = compute_snap(mover, Vec2d(2.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(2.0));
    REQUIRE(r.engaged[0] == false);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `ctest --test-dir build/tests/libslic3r -R contact --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement — add gated contact candidates**

In `compute_snap`, add a helper near the anonymous-namespace helpers:
```cpp
namespace {
    inline bool overlaps_perp(const BoundingBoxf& m, const BoundingBoxf& t, int axis) {
        int p = axis ^ 1; // perpendicular axis
        return lo(m,p) < hi(t,p) && lo(t,p) < hi(m,p);
    }
}
```
Then inside the neighbor loop, after the center block add:
```cpp
            if (s.contact && overlaps_perp(mover, n.bbox, a)) {
                consider(hi(mover,a), lo(n.bbox,a)); // mover max touches target min
                consider(lo(mover,a), hi(n.bbox,a)); // mover min touches target max
            }
```

- [ ] **Step 4: Run to verify pass**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**
```bash
git add src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp
git commit -m "feat(align-snap): contact snapping gated on perpendicular overlap"
```

---

## Task 5: Independent-axis resolution sanity + disabled passthrough

**Files:**
- Test: `tests/libslic3r/test_alignment_snap.cpp` (no impl change expected — verifies existing behavior)

- [ ] **Step 1: Write the tests**
```cpp
TEST_CASE("x and y snap independently in one call", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    BoundingBoxf mover = box(0,0,10,10);
    // one neighbor aligns left edge in X (x0=0), another aligns bottom edge in Y (y0=0)
    std::vector<Neighbor> t = { { box(0,100,10,110), 1 }, { box(100,0,110,10), 2 } };
    SnapResult r = compute_snap(mover, Vec2d(2.0, 3.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(0.0));
    REQUIRE(r.corrected_delta.y() == Approx(0.0));
    REQUIRE(r.engaged[0] == true);
    REQUIRE(r.engaged[1] == true);
}

TEST_CASE("disabled master returns raw delta", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = false;
    SnapState st;
    std::vector<Neighbor> t = { { box(0,0,10,10), 1 } };
    SnapResult r = compute_snap(box(0,0,10,10), Vec2d(1.0, 1.0), t, s, 1.0, st);
    REQUIRE(r.corrected_delta.x() == Approx(1.0));
    REQUIRE(r.corrected_delta.y() == Approx(1.0));
}
```

- [ ] **Step 2: Run — expected PASS immediately** (validates prior tasks compose correctly)

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: PASS. If the independent-axis test fails, revisit Task 2's per-axis loop.

- [ ] **Step 3: Commit**
```bash
git add tests/libslic3r/test_alignment_snap.cpp
git commit -m "test(align-snap): independent-axis resolution + disabled passthrough"
```

---

## Task 6: Hysteresis (break-away strength)

**Files:**
- Modify: `src/libslic3r/AlignmentSnap.cpp`
- Test: `tests/libslic3r/test_alignment_snap.cpp`

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("stays engaged within break-away margin", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0; s.strength_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(0,40,10,50), 1 } }; // left edge target x=0
    // first frame: within sensitivity -> engage and snap to 0
    SnapResult r1 = compute_snap(mover, Vec2d(2.0,0.0), t, s, 1.0, st);
    REQUIRE(r1.engaged[0] == true);
    REQUIRE(st.engaged[0] == true);
    // second frame: raw delta 8 -> 8mm away (> sensitivity 5) but < sensitivity+strength (10) -> stays snapped
    SnapResult r2 = compute_snap(mover, Vec2d(8.0,0.0), t, s, 1.0, st);
    REQUIRE(r2.engaged[0] == true);
    REQUIRE(r2.corrected_delta.x() == Approx(0.0));
}

TEST_CASE("breaks away beyond margin", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0; s.strength_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    BoundingBoxf mover = box(0,0,10,10);
    std::vector<Neighbor> t = { { box(0,40,10,50), 1 } };
    compute_snap(mover, Vec2d(2.0,0.0), t, s, 1.0, st);       // engage
    SnapResult r = compute_snap(mover, Vec2d(12.0,0.0), t, s, 1.0, st); // 12 > 10 margin -> release
    REQUIRE(r.engaged[0] == false);
    REQUIRE(r.corrected_delta.x() == Approx(12.0));
    REQUIRE(st.engaged[0] == false);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `ctest --test-dir build/tests/libslic3r -R "engaged\|break" --output-on-failure`
Expected: FAIL (state ignored — `st.engaged` never set; second-frame far position not held).

- [ ] **Step 3: Implement hysteresis**

In `compute_snap`, change the per-axis resolution so an already-engaged axis uses the wider `sensitivity_px + strength_px` window and the engaged target coordinate. Replace the per-axis block body with:
```cpp
    for (int a = 0; a < 2; ++a) {
        double mover_lo = lo(mover,a), mover_hi = hi(mover,a), mover_c = ctr(mover,a);

        // If already engaged, test the break-away window against the remembered snap line first.
        if (state.engaged[a]) {
            // distance from each mover key to the engaged coordinate, pick the smallest.
            double d = std::min({ std::abs(state.coord[a] - mover_lo),
                                  std::abs(state.coord[a] - mover_hi),
                                  std::abs(state.coord[a] - mover_c) });
            if (d * px_per_mm <= s.sensitivity_px + s.strength_px) {
                // stay snapped: correct whichever mover key is closest to the engaged line.
                double keys[3] = { mover_lo, mover_hi, mover_c };
                double bestc = state.coord[a] - keys[0];
                for (double k : keys)
                    if (std::abs(state.coord[a]-k) < std::abs(bestc)) bestc = state.coord[a]-k;
                if (a==0) r.corrected_delta.x() += bestc; else r.corrected_delta.y() += bestc;
                r.engaged[a] = true;
                continue;
            }
            state.engaged[a] = false; // fall through to fresh search
        }

        Cand best { 0.0, s.sensitivity_px + 1.0 };
        bool found = false;
        double engaged_line = 0.0;
        for (const Neighbor& n : targets) {
            auto consider = [&](double mover_key, double target_key) {
                double correction = target_key - mover_key;
                double dpx = std::abs(correction) * px_per_mm;
                if (dpx <= s.sensitivity_px && dpx < best.dist_px) {
                    best = { correction, dpx }; found = true; engaged_line = target_key;
                }
            };
            if (s.edge_align)   { consider(mover_lo, lo(n.bbox,a)); consider(mover_hi, hi(n.bbox,a)); }
            if (s.center_align) { consider(mover_c,  ctr(n.bbox,a)); }
            if (s.contact && overlaps_perp(mover, n.bbox, a)) {
                consider(mover_hi, lo(n.bbox,a)); consider(mover_lo, hi(n.bbox,a));
            }
        }
        if (found) {
            if (a==0) r.corrected_delta.x() += best.correction; else r.corrected_delta.y() += best.correction;
            r.engaged[a] = true;
            state.engaged[a] = true;
            state.coord[a]   = engaged_line;
        }
    }
```
Remove the now-duplicated earlier loop body. Keep the anonymous-namespace helpers. Ensure `<algorithm>` is included for `std::min`.

- [ ] **Step 4: Run to verify pass**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: ALL prior + hysteresis tests PASS.

- [ ] **Step 5: Commit**
```bash
git add src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp
git commit -m "feat(align-snap): break-away hysteresis via SnapState"
```

---

## Task 7: Guides — alignment lines + ghost boxes

**Files:**
- Modify: `src/libslic3r/AlignmentSnap.cpp`
- Test: `tests/libslic3r/test_alignment_snap.cpp`

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("emits an alignment line for an engaged axis", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.center_align = false; s.contact = false; s.spacing_propagation = false;
    SnapState st;
    std::vector<Neighbor> t = { { box(0,40,10,50), 1 } };
    SnapResult r = compute_snap(box(0,0,10,10), Vec2d(2.0,0.0), t, s, 1.0, st);
    REQUIRE(r.lines.size() >= 1);
    REQUIRE(r.lines[0].axis == Axis::X);
    REQUIRE(r.lines[0].coord == Approx(0.0)); // the aligned x line
}

TEST_CASE("emits ghost boxes for nearby targets", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    SnapState st;
    // near target (within reveal radius, should ghost) and a far one (should not)
    std::vector<Neighbor> t = { { box(0,15,10,25), 1 }, { box(900,900,910,910), 2 } };
    SnapResult r = compute_snap(box(0,0,10,10), Vec2d(2.0,0.0), t, s, 1.0, st);
    REQUIRE(r.ghosts.size() == 1);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `ctest --test-dir build/tests/libslic3r -R "alignment line\|ghost" --output-on-failure`
Expected: FAIL (empty vectors).

- [ ] **Step 3: Implement guide emission**

At the end of `compute_snap`, before `return r;`, add:
```cpp
    // Ghost boxes: targets whose bbox comes within a reveal radius of the moved box.
    const double reveal_mm = 3.0 * thresh_mm;
    BoundingBoxf mv = mover_start; mv.min += r.corrected_delta; mv.max += r.corrected_delta;
    for (const Neighbor& n : targets) {
        double dx = std::max({ 0.0, n.bbox.min.x() - mv.max.x(), mv.min.x() - n.bbox.max.x() });
        double dy = std::max({ 0.0, n.bbox.min.y() - mv.max.y(), mv.min.y() - n.bbox.max.y() });
        if (std::sqrt(dx*dx + dy*dy) <= reveal_mm)
            r.ghosts.push_back(GhostBox{ n.bbox });
    }
    // Alignment lines for engaged axes: a line at the snapped coordinate spanning mover+partner extents.
    for (int a = 0; a < 2; ++a) {
        if (!r.engaged[a]) continue;
        int p = a ^ 1;
        double span_lo = (p==0 ? mv.min.x() : mv.min.y());
        double span_hi = (p==0 ? mv.max.x() : mv.max.y());
        for (const Neighbor& n : targets) {
            span_lo = std::min(span_lo, (p==0 ? n.bbox.min.x() : n.bbox.min.y()));
            span_hi = std::max(span_hi, (p==0 ? n.bbox.max.x() : n.bbox.max.y()));
        }
        r.lines.push_back(GuideLine{ a==0?Axis::X:Axis::Y, state.coord[a], span_lo, span_hi });
    }
```

- [ ] **Step 4: Run to verify pass**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: ALL PASS.

- [ ] **Step 5: Commit**
```bash
git add src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp
git commit -m "feat(align-snap): emit alignment-line and ghost-box guides"
```

---

## Task 8: Spacing propagation (row extend) + spacing badges

**Files:**
- Modify: `src/libslic3r/AlignmentSnap.cpp`
- Test: `tests/libslic3r/test_alignment_snap.cpp`

**Behavior:** On axis X, find a set of ≥2 targets that (a) are mutually aligned on the perpendicular axis (share min, center, or max within `thresh_mm`) and (b) have equal center-to-center spacing. Project the next slot beyond the extreme member; if the mover center is within sensitivity of that slot, snap and emit two spacing badges. Same for Y. Only runs when `spacing_propagation` is on and that axis is not already edge/center/contact engaged.

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("row spacing propagates to the next slot", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.edge_align = false; s.center_align = false; s.contact = false;
    s.spacing_propagation = true;
    SnapState st;
    // Two aligned boxes: centers at x=5 and x=25 (spacing 20), same y-band. Next slot center = 45.
    std::vector<Neighbor> t = { { box(0,0,10,10), 1 }, { box(20,0,30,10), 2 } };
    // mover width 10 -> center offset 5; place so its center lands near 45: start center 5, raw +38 -> center 43.
    BoundingBoxf mover = box(0,0,10,10);
    SnapResult r = compute_snap(mover, Vec2d(38.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.engaged[0] == true);
    REQUIRE(r.corrected_delta.x() == Approx(40.0)); // center -> 45
    REQUIRE(r.badges.size() >= 1);
}

TEST_CASE("row propagation requires perpendicular alignment", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true; s.sensitivity_px = 5.0;
    s.edge_align = false; s.center_align = false; s.contact = false;
    s.spacing_propagation = true;
    SnapState st;
    // Same spacing but staggered in Y (not a row) -> no propagation.
    std::vector<Neighbor> t = { { box(0,0,10,10), 1 }, { box(20,50,30,60), 2 } };
    SnapResult r = compute_snap(box(0,0,10,10), Vec2d(38.0, 0.0), t, s, 1.0, st);
    REQUIRE(r.engaged[0] == false);
    REQUIRE(r.corrected_delta.x() == Approx(38.0));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `ctest --test-dir build/tests/libslic3r -R "row spacing\|row propagation" --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement spacing propagation**

Add a helper and a post-pass. Near the other helpers:
```cpp
namespace {
    inline bool perp_aligned(const BoundingBoxf& x, const BoundingBoxf& y, int axis, double tol) {
        int p = axis ^ 1;
        return std::abs(lo(x,p)-lo(y,p)) <= tol
            || std::abs(ctr(x,p)-ctr(y,p)) <= tol
            || std::abs(hi(x,p)-hi(y,p)) <= tol;
    }
}
```
Then, after the per-axis alignment loop but before guide emission, add:
```cpp
    if (s.spacing_propagation) {
        for (int a = 0; a < 2; ++a) {
            if (r.engaged[a]) continue; // don't override a hard alignment
            // collect targets that form a mutually perp-aligned row, sorted by center on axis a.
            std::vector<const Neighbor*> row;
            for (const Neighbor& n : targets) row.push_back(&n);
            std::sort(row.begin(), row.end(), [&](const Neighbor* p, const Neighbor* q){
                return ctr(p->bbox,a) < ctr(q->bbox,a); });
            // find the largest run of equal spacing among perp-aligned neighbors
            for (size_t i = 0; i + 1 < row.size(); ++i) {
                if (!perp_aligned(row[i]->bbox, row[i+1]->bbox, a, thresh_mm)) continue;
                double spacing = ctr(row[i+1]->bbox,a) - ctr(row[i]->bbox,a);
                if (spacing <= thresh_mm) continue;
                // next slot beyond row[i+1]
                double slot = ctr(row[i+1]->bbox,a) + spacing;
                double mover_c = (a==0 ? mover.center().x() : mover.center().y());
                if (std::abs(slot - mover_c) * px_per_mm <= s.sensitivity_px) {
                    double correction = slot - mover_c;
                    if (a==0) r.corrected_delta.x() += correction; else r.corrected_delta.y() += correction;
                    r.engaged[a] = true; state.engaged[a] = true; state.coord[a] = slot;
                    // badges between the two known members and between the last member and the new slot
                    Vec2d p0, p1, p2, p3;
                    if (a==0) {
                        double y = ctr(row[i]->bbox,1);
                        p0={ctr(row[i]->bbox,0),y}; p1={ctr(row[i+1]->bbox,0),y};
                        p2={ctr(row[i+1]->bbox,0),y}; p3={slot,y};
                    } else {
                        double x = ctr(row[i]->bbox,0);
                        p0={x,ctr(row[i]->bbox,1)}; p1={x,ctr(row[i+1]->bbox,1)};
                        p2={x,ctr(row[i+1]->bbox,1)}; p3={x,slot};
                    }
                    r.badges.push_back(SpacingBadge{ p0, p1, spacing });
                    r.badges.push_back(SpacingBadge{ p2, p3, spacing });
                    break;
                }
            }
        }
    }
```

- [ ] **Step 4: Run to verify pass**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: ALL PASS.

- [ ] **Step 5: Commit**
```bash
git add src/libslic3r/AlignmentSnap.cpp tests/libslic3r/test_alignment_snap.cpp
git commit -m "feat(align-snap): row spacing propagation + spacing badges"
```

---

## Task 9: SnapSettings on the canvas + AppConfig persistence

**Files:**
- Modify: `src/slic3r/GUI/GLCanvas3D.hpp`
- Modify: `src/slic3r/GUI/GLCanvas3D.cpp`

- [ ] **Step 1: Add member + accessors to the header**

In `GLCanvas3D.hpp`, add the include near the top (with other libslic3r includes):
```cpp
#include "libslic3r/AlignmentSnap.hpp"
```
In the private members region (near `ArrangeSettings m_arrange_settings_fff...`, `GLCanvas3D.hpp:628`), add:
```cpp
    AlignmentSnap::SnapSettings m_snap_settings;
    AlignmentSnap::SnapState    m_snap_state;
    std::vector<AlignmentSnap::Neighbor> m_snap_neighbors; // gathered at drag start
    AlignmentSnap::SnapResult   m_snap_guides;             // guides for current frame
```
In the public methods region (near `load_arrange_settings();`, `GLCanvas3D.hpp:660`), add:
```cpp
    void load_snap_settings();
    void save_snap_settings();
    AlignmentSnap::SnapSettings& get_snap_settings() { return m_snap_settings; }
```

- [ ] **Step 2: Implement load/save in the .cpp**

In `GLCanvas3D.cpp`, after `load_arrange_settings()` (ends near `GLCanvas3D.cpp:1130`), add:
```cpp
void GLCanvas3D::load_snap_settings()
{
    auto* cfg = wxGetApp().app_config;
    auto getf = [&](const char* k, double def) {
        std::string v = cfg->get("snap_align", k);
        return v.empty() ? def : std::stod(v);
    };
    auto getb = [&](const char* k, bool def) {
        std::string v = cfg->get("snap_align", k);
        return v.empty() ? def : (v == "1" || v == "true");
    };
    m_snap_settings.enabled             = getb("enabled", false);
    m_snap_settings.sensitivity_px      = getf("sensitivity_px", 8.0);
    m_snap_settings.strength_px         = getf("strength_px", 6.0);
    m_snap_settings.edge_align          = getb("edge_align", true);
    m_snap_settings.center_align        = getb("center_align", true);
    m_snap_settings.contact             = getb("contact", true);
    m_snap_settings.spacing_propagation = getb("spacing_propagation", true);
}

void GLCanvas3D::save_snap_settings()
{
    auto* cfg = wxGetApp().app_config;
    cfg->set("snap_align", "enabled",             m_snap_settings.enabled ? "1" : "0");
    cfg->set("snap_align", "sensitivity_px",      float_to_string_decimal_point(m_snap_settings.sensitivity_px));
    cfg->set("snap_align", "strength_px",         float_to_string_decimal_point(m_snap_settings.strength_px));
    cfg->set("snap_align", "edge_align",          m_snap_settings.edge_align ? "1" : "0");
    cfg->set("snap_align", "center_align",        m_snap_settings.center_align ? "1" : "0");
    cfg->set("snap_align", "contact",             m_snap_settings.contact ? "1" : "0");
    cfg->set("snap_align", "spacing_propagation", m_snap_settings.spacing_propagation ? "1" : "0");
}
```

- [ ] **Step 3: Call load at startup**

In `GLCanvas3D.cpp`, find the existing `load_arrange_settings();` call inside the canvas init (near `GLCanvas3D.cpp:1208`) and add on the next line:
```cpp
    load_snap_settings();
```

- [ ] **Step 4: Build**

Run: `cmake --build build --config RelWithDebInfo --target OrcaSlicer -- -m`
Expected: compiles (no behavior change yet).

- [ ] **Step 5: Commit**
```bash
git add src/slic3r/GUI/GLCanvas3D.hpp src/slic3r/GUI/GLCanvas3D.cpp
git commit -m "feat(align-snap): SnapSettings on canvas with AppConfig persistence"
```

---

## Task 10: Toolbar button + settings panel (`_render_snap_menu`)

**Files:**
- Modify: `src/slic3r/GUI/GLCanvas3D.hpp` (declare `_render_snap_menu`)
- Modify: `src/slic3r/GUI/GLCanvas3D.cpp`
- Asset: `resources/images/toolbar_snap.svg` and `toolbar_snap_dark.svg`

- [ ] **Step 1: Declare the render method**

In `GLCanvas3D.hpp`, near the private declaration of `_render_arrange_menu` (search `_render_arrange_menu`), add:
```cpp
    bool _render_snap_menu(float left, float right, float bottom, float top);
```

- [ ] **Step 2: Add toolbar icon assets**

Copy `resources/images/toolbar_arrange.svg` to `resources/images/toolbar_snap.svg` and `toolbar_arrange_dark.svg` to `toolbar_snap_dark.svg` as placeholders (a distinct icon can replace these later — tracked as an open item). These files must exist or the toolbar item won't render.

- [ ] **Step 3: Register the toolbar item**

In `GLCanvas3D.cpp`, in the toolbar-init function right after the arrange item is added (`if (!m_main_toolbar.add_item(item)) return false;` following `item.name = "arrange"`, near `GLCanvas3D.cpp:6906`), insert:
```cpp
    item.name = "snap_align";
    item.icon_filename = m_is_dark ? "toolbar_snap_dark.svg" : "toolbar_snap.svg";
    item.tooltip = _utf8(L("Alignment snapping settings"));
    item.sprite_id++;
    item.left.action_callback = []() {};
    item.enabling_callback = []()->bool { return true; };
    item.left.toggable = true;
    item.left.render_callback = [this](float left, float right, float bottom, float top) {
        if (m_canvas != nullptr)
            _render_snap_menu(left, right, bottom, top);
    };
    if (!m_main_toolbar.add_item(item))
        return false;
```

- [ ] **Step 4: Implement the settings panel**

In `GLCanvas3D.cpp`, after `_render_arrange_menu` (ends near `GLCanvas3D.cpp:6110`), add:
```cpp
bool GLCanvas3D::_render_snap_menu(float left, float right, float bottom, float top)
{
    ImGuiWrapper* imgui = wxGetApp().imgui();
    auto canvas_w = float(get_canvas_size().get_width());
    float left_pos = m_main_toolbar.get_item("snap_align")->render_left_pos;
    const float x = (1 + left_pos) * canvas_w / 2;
    imgui->set_next_window_pos(x, m_main_toolbar.get_height(), ImGuiCond_Always, 0.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(get_scale());
    imgui->begin(_L("Snap options"), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    bool dirty = false;
    dirty |= imgui->bbl_checkbox(_L("Enable snap alignment"), m_snap_settings.enabled);
    ImGui::Separator();

    float sens = (float)m_snap_settings.sensitivity_px;
    if (imgui->slider_float(_L("Sensitivity (px)"), &sens, 2.0f, 30.0f, "%.0f")) {
        m_snap_settings.sensitivity_px = sens; dirty = true;
    }
    float str = (float)m_snap_settings.strength_px;
    if (imgui->slider_float(_L("Strength (px)"), &str, 0.0f, 30.0f, "%.0f")) {
        m_snap_settings.strength_px = str; dirty = true;
    }
    ImGui::Separator();
    dirty |= imgui->bbl_checkbox(_L("Edge alignment"),      m_snap_settings.edge_align);
    dirty |= imgui->bbl_checkbox(_L("Center alignment"),    m_snap_settings.center_align);
    dirty |= imgui->bbl_checkbox(_L("Contact"),             m_snap_settings.contact);
    dirty |= imgui->bbl_checkbox(_L("Propagate spacing (row)"), m_snap_settings.spacing_propagation);
    ImGui::Separator();
    imgui->text(_L("Hold Alt while dragging to disable snapping."));

    if (dirty)
        save_snap_settings();

    imgui->end();
    ImGuiWrapper::pop_toolbar_style();
    return true;
}
```
(If `slider_float` is not a member of `ImGuiWrapper` in this codebase, use the same slider call `_render_arrange_menu` uses — check that function for the exact helper name and mirror it.)

- [ ] **Step 5: Build + manual check**

Run: `cmake --build build --config RelWithDebInfo --target OrcaSlicer -- -m`
Then launch OrcaSlicer, confirm a new toolbar button opens the panel and toggles persist across restart.

- [ ] **Step 6: Commit**
```bash
git add src/slic3r/GUI/GLCanvas3D.hpp src/slic3r/GUI/GLCanvas3D.cpp resources/images/toolbar_snap.svg resources/images/toolbar_snap_dark.svg
git commit -m "feat(align-snap): toolbar button + ImGui settings panel"
```

---

## Task 11: Drag seam — neighbor cache, Alt-suppress, snap correction

**Files:**
- Modify: `src/slic3r/GUI/GLCanvas3D.cpp`

- [ ] **Step 1: Gather neighbors at drag start**

In `GLCanvas3D::on_mouse`, where dragging is initiated (`m_mouse.drag.move_volume_idx = volume_idx;`, near `GLCanvas3D.cpp:4551`), after `m_selection.setup_cache();` add:
```cpp
            // Cache neighbor bounding boxes (XY footprint) once at drag start for snap alignment.
            m_snap_neighbors.clear();
            m_snap_state = AlignmentSnap::SnapState{};
            if (m_snap_settings.enabled) {
                const int cur_plate = wxGetApp().plater()->get_partplate_list().get_curr_plate_index();
                const Model& model = *m_model;
                for (int oi = 0; oi < (int)model.objects.size(); ++oi) {
                    ModelObject* mo = model.objects[oi];
                    if (!mo->snap_alignment_enabled) continue;         // per-object opt-out
                    for (const ModelInstance* mi : mo->instances) {
                        BoundingBoxf3 bb = mo->instance_bounding_box(
                            (int)(&mi - &mo->instances[0]), false);
                        // skip instances not on the current plate
                        // (bbox center vs plate origin check kept simple; refine if needed)
                        m_snap_neighbors.push_back(AlignmentSnap::Neighbor{
                            BoundingBoxf(Vec2d(bb.min.x(), bb.min.y()), Vec2d(bb.max.x(), bb.max.y())), oi });
                    }
                }
            }
```
Note: exclude the object(s) currently being dragged in the next step (they're filtered by selection at correction time). If `get_partplate_list()`/`get_curr_plate_index()` names differ, mirror how `_render_arrange_menu`/arrange finds the current plate.

- [ ] **Step 2: Apply snap correction at the translate seam**

Replace the block at `GLCanvas3D.cpp:4601-4603`:
```cpp
                TransformationType trafo_type;
                trafo_type.set_relative();
                m_selection.translate(cur_pos - m_mouse.drag.start_position_3D, trafo_type);
```
with:
```cpp
                TransformationType trafo_type;
                trafo_type.set_relative();
                Vec3d delta = cur_pos - m_mouse.drag.start_position_3D;
                if (m_snap_settings.enabled && !wxGetKeyState(WXK_ALT) && !m_snap_neighbors.empty()) {
                    // Build mover box from the selection's XY bbox at drag start.
                    const BoundingBoxf3& sel_bb = m_selection.get_bounding_box();
                    BoundingBoxf mover_start(
                        Vec2d(sel_bb.min.x() - delta.x(), sel_bb.min.y() - delta.y()),
                        Vec2d(sel_bb.max.x() - delta.x(), sel_bb.max.y() - delta.y()));
                    // exclude the dragged object(s) from targets
                    std::vector<AlignmentSnap::Neighbor> targets;
                    targets.reserve(m_snap_neighbors.size());
                    for (const auto& n : m_snap_neighbors)
                        if (!m_selection.get_object_idxs().count(n.id))
                            targets.push_back(n);
                    const double zoom = wxGetApp().plater()->get_camera().get_zoom();
                    m_snap_guides = AlignmentSnap::compute_snap(
                        mover_start, Vec2d(delta.x(), delta.y()), targets,
                        m_snap_settings, zoom, m_snap_state);
                    delta.x() = m_snap_guides.corrected_delta.x();
                    delta.y() = m_snap_guides.corrected_delta.y();
                } else {
                    m_snap_guides = AlignmentSnap::SnapResult{};
                }
                m_selection.translate(delta, trafo_type);
```
Notes for the implementer:
- `m_selection.get_bounding_box()` returns the current (already-translated on prior frames?) bbox. Because `setup_cache()` + relative translate re-applies from the cached start each frame, `get_bounding_box()` here reflects the *pre-translate* selection this frame; subtracting `delta` yields the drag-start footprint. If in practice the bbox already includes the running translation, drop the `- delta` terms — verify by logging one drag and checking the mover box matches the object's start position.
- `get_object_idxs()` returns the selected object indices; confirm the exact accessor name in `Selection.hpp` and adjust (`contains_object`, etc.) if different.
- `get_camera().get_zoom()` is OrcaSlicer's world-units→pixels scale, serving as `px_per_mm`.

- [ ] **Step 3: Clear guides on mouse-up**

In `on_mouse`, in the left-button-up handling that ends a move (search for `do_move(L("Tool Move"))`, near `GLCanvas3D.cpp:3710`), add nearby:
```cpp
        m_snap_guides = AlignmentSnap::SnapResult{};
        m_snap_neighbors.clear();
```

- [ ] **Step 4: Build + manual check**

Run: `cmake --build build --config RelWithDebInfo --target OrcaSlicer -- -m`
Manually: enable snapping, drag one cube near another — it should visibly snap edges/centers; holding Alt should drag freely. (Guides not drawn yet — next task.)

- [ ] **Step 5: Commit**
```bash
git add src/slic3r/GUI/GLCanvas3D.cpp
git commit -m "feat(align-snap): apply snap correction during object drag (Alt suppresses)"
```

---

## Task 12: Render guides (`render_snap_guides`)

**Files:**
- Modify: `src/slic3r/GUI/GLCanvas3D.hpp` (declare)
- Modify: `src/slic3r/GUI/GLCanvas3D.cpp`

- [ ] **Step 1: Declare + add style constants**

In `GLCanvas3D.hpp` private methods, add:
```cpp
    void render_snap_guides();
```
In `GLCanvas3D.cpp`, near the top (after includes), add a single style block so colors/thickness live in one place:
```cpp
namespace {
    // Alignment-snap guide styling — adjust here to restyle all guides.
    static const ColorRGBA SNAP_LINE_COLOR   = ColorRGBA(0.22f, 0.70f, 0.30f, 0.9f); // green
    static const ColorRGBA SNAP_GHOST_COLOR  = ColorRGBA(0.60f, 0.60f, 0.60f, 0.7f); // gray
    static const ColorRGBA SNAP_BADGE_COLOR  = ColorRGBA(0.90f, 0.31f, 0.50f, 1.0f); // pink
    static const float     SNAP_LINE_WIDTH   = 1.5f;
    static const float     SNAP_GUIDE_Z      = 0.05f; // just above bed
}
```

- [ ] **Step 2: Implement the renderer**

Add after `_render_snap_menu` in `GLCanvas3D.cpp`. Draw alignment lines and ghost boxes as bed-plane line loops via the existing immediate/line helpers this file already uses for clearance/overlays, and draw spacing badge labels via ImGui at projected screen positions:
```cpp
void GLCanvas3D::render_snap_guides()
{
    if (!m_mouse.dragging) return;
    if (m_snap_guides.lines.empty() && m_snap_guides.ghosts.empty() && m_snap_guides.badges.empty())
        return;

    // Alignment lines
    for (const auto& gl : m_snap_guides.lines) {
        Vec3d a, b;
        if (gl.axis == AlignmentSnap::Axis::X) { a = {gl.coord, gl.span_lo, SNAP_GUIDE_Z}; b = {gl.coord, gl.span_hi, SNAP_GUIDE_Z}; }
        else                                   { a = {gl.span_lo, gl.coord, SNAP_GUIDE_Z}; b = {gl.span_hi, gl.coord, SNAP_GUIDE_Z}; }
        _render_line_3d(a, b, SNAP_LINE_COLOR, SNAP_LINE_WIDTH); // see note
    }
    // Ghost boxes (footprint rectangles)
    for (const auto& g : m_snap_guides.ghosts) {
        Vec3d c0{g.bbox.min.x(), g.bbox.min.y(), SNAP_GUIDE_Z};
        Vec3d c1{g.bbox.max.x(), g.bbox.min.y(), SNAP_GUIDE_Z};
        Vec3d c2{g.bbox.max.x(), g.bbox.max.y(), SNAP_GUIDE_Z};
        Vec3d c3{g.bbox.min.x(), g.bbox.max.y(), SNAP_GUIDE_Z};
        _render_line_3d(c0, c1, SNAP_GHOST_COLOR, 1.0f);
        _render_line_3d(c1, c2, SNAP_GHOST_COLOR, 1.0f);
        _render_line_3d(c2, c3, SNAP_GHOST_COLOR, 1.0f);
        _render_line_3d(c3, c0, SNAP_GHOST_COLOR, 1.0f);
    }
    // Spacing badges: project midpoint to screen and draw the mm value via ImGui.
    if (!m_snap_guides.badges.empty()) {
        const Camera& cam = wxGetApp().plater()->get_camera();
        ImGuiWrapper* imgui = wxGetApp().imgui();
        for (const auto& bd : m_snap_guides.badges) {
            Vec2d midxy = 0.5 * (bd.a + bd.b);
            Vec3d mid{midxy.x(), midxy.y(), SNAP_GUIDE_Z};
            Point scr = cam.project_vertex(mid); // world -> screen; confirm accessor name
            imgui->set_next_window_pos((float)scr.x(), (float)scr.y(), ImGuiCond_Always, 0.5f, 0.5f);
            imgui->begin(wxString::Format("##snapbadge_%p", (void*)&bd),
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
            imgui->text(wxString::Format("%.2f", bd.value_mm));
            imgui->end();
        }
    }
}
```
Implementer notes:
- `_render_line_3d(a,b,color,width)` is illustrative. Use the line-drawing utility already present in `GLCanvas3D.cpp` (the same one used to draw sequential-print clearance / bounding overlays — search `GLModel` line init or `render_line`, and mirror its setup: init a `GLModel` with two vertices and a line primitive, set color, render with the flat shader). Prefer reusing that helper over adding a new one.
- `cam.project_vertex` / world-to-screen: confirm the exact Camera API (`project` / `world_to_screen`) and adjust.

- [ ] **Step 3: Call the renderer in the render pass**

In the main render function (`GLCanvas3D::render` / `_render_...` overlay sequence — search where `_render_arrange_menu` siblings like selection/overlays are drawn, after volumes), add:
```cpp
    render_snap_guides();
```

- [ ] **Step 4: Build + manual check**

Run: `cmake --build build --config RelWithDebInfo --target OrcaSlicer -- -m`
Manually: drag near a neighbor — green alignment line, gray ghost boxes, and pink spacing badges appear; released cleanly on drop.

- [ ] **Step 5: Commit**
```bash
git add src/slic3r/GUI/GLCanvas3D.hpp src/slic3r/GUI/GLCanvas3D.cpp
git commit -m "feat(align-snap): render alignment lines, ghost boxes, and spacing badges"
```

---

## Task 13: Per-object flag on ModelObject + 3mf persistence

**Files:**
- Modify: `src/libslic3r/Model.hpp`
- Modify: `src/libslic3r/Model.cpp`
- Modify: `src/libslic3r/Format/3mf.cpp`

- [ ] **Step 1: Add the member (default true)**

In `src/libslic3r/Model.hpp`, in `class ModelObject`, near other per-object bool/config members, add:
```cpp
    // Orca: participate in alignment snapping (GUI drag). Default true; persisted in 3mf metadata.
    bool snap_alignment_enabled { true };
```

- [ ] **Step 2: Preserve on copy/assign**

In `src/libslic3r/Model.cpp`, in `ModelObject::assign_copy` (search `assign_copy`), copy the field alongside the other scalar members:
```cpp
    this->snap_alignment_enabled = rhs.snap_alignment_enabled;
```

- [ ] **Step 3: Write it as object metadata on save**

In `src/libslic3r/Format/3mf.cpp`, locate where object-level metadata is written during model-config export (search `OBJECT_TYPE` in the writer, and the loop that writes object `name`/config metadata — near the `_add_model_config_file` / object metadata emit). Add a metadata line for each object:
```cpp
    // Orca: persist alignment-snap opt-out (omit when default-true to keep files clean/back-compatible).
    if (!obj->snap_alignment_enabled)
        stream << "   <metadata type=\"object\" key=\"snap_alignment_enabled\" value=\"0\"/>\n";
```
(Match the exact stream/emit idiom used by adjacent metadata writes in that function.)

- [ ] **Step 4: Read it back on load**

In `src/libslic3r/Format/3mf.cpp`, in the object-metadata apply loop (`GLCanvas3D`-independent reader near `3mf.cpp:895-904`, where `metadata.key`/`metadata.value` are applied), add a special-case before the `config.set_deserialize` fallback:
```cpp
                    else if (metadata.key == "snap_alignment_enabled")
                        model_object->snap_alignment_enabled = !(metadata.value == "0" || metadata.value == "false");
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config RelWithDebInfo --target OrcaSlicer -- -m`
Expected: compiles.

- [ ] **Step 6: Manual round-trip check**

Load a project, mark an object excluded (after Task 14), save `.3mf`, reload — flag persists. Load an *old* `.3mf` (no key) — object is enabled. Note result in the PR.

- [ ] **Step 7: Commit**
```bash
git add src/libslic3r/Model.hpp src/libslic3r/Model.cpp src/libslic3r/Format/3mf.cpp
git commit -m "feat(align-snap): persist per-object snap opt-out in 3mf (default enabled)"
```

---

## Task 14: Context-menu opt-out

**Files:**
- Modify: `src/slic3r/GUI/GUI_Factories.cpp`
- Modify: `src/slic3r/GUI/GUI_Factories.hpp` (if a new method is declared)

- [ ] **Step 1: Add a checkable menu item builder**

In `GUI_Factories.cpp`, near `append_menu_item_set_visible` (`GUI_Factories.cpp:518`), add:
```cpp
void MenuFactory::append_menu_item_snap_alignment(wxMenu* menu)
{
    const Selection& sel = plater()->canvas3D()->get_selection();
    // Checked if all selected objects currently participate.
    bool all_on = true;
    for (int oi : sel.get_object_idxs()) {
        ModelObject* mo = plater()->model().objects[oi];
        if (mo && !mo->snap_alignment_enabled) { all_on = false; break; }
    }
    wxMenuItem* item = append_menu_check_item(menu, wxID_ANY, _L("Enable snap alignment"),
        _L("Include this object in alignment snapping while dragging"),
        [](wxCommandEvent&) {
            Plater* plater = wxGetApp().plater();
            const Selection& s = plater->canvas3D()->get_selection();
            bool any_off = false;
            for (int oi : s.get_object_idxs())
                if (!plater->model().objects[oi]->snap_alignment_enabled) { any_off = true; break; }
            for (int oi : s.get_object_idxs())
                plater->model().objects[oi]->snap_alignment_enabled = any_off; // toggle toward enabled
        }, menu);
    item->Check(all_on);
}
```
Notes: mirror the exact signature of `append_menu_check_item`/`append_menu_item` used elsewhere in this file (some variants take a `std::function` and an `wxEvtHandler*`). Use `plater()` and `canvas3D()->get_selection()` as adjacent code does; adjust accessor names to match this file's helpers.

- [ ] **Step 2: Declare it (if the file declares members in the header)**

If `GUI_Factories.hpp` declares the other `append_menu_item_*` methods, add:
```cpp
    void append_menu_item_snap_alignment(wxMenu* menu);
```

- [ ] **Step 3: Wire it into the object menu**

In `GUI_Factories.cpp`, find where the object context menu is assembled (search the function that calls `append_menu_item_set_visible(menu)` for objects) and add near it:
```cpp
    append_menu_item_snap_alignment(menu);
```

- [ ] **Step 4: Build + manual check**

Run: `cmake --build build --config RelWithDebInfo --target OrcaSlicer -- -m`
Manually: right-click an object → "Enable snap alignment" toggles (checkmark reflects state); an unchecked object is ignored as a target and does not snap when dragged.

- [ ] **Step 5: Commit**
```bash
git add src/slic3r/GUI/GUI_Factories.cpp src/slic3r/GUI/GUI_Factories.hpp
git commit -m "feat(align-snap): per-object opt-out in right-click menu"
```

---

## Task 15: Verification doc + backward-compat pass

**Files:**
- Create: `docs/superpowers/verification/2026-07-27-relative-alignment-snapping.md`

- [ ] **Step 1: Write the manual verification checklist**

Create the file with this content:
```markdown
# Relative Alignment & Snapping — Manual Verification

## Core (automated)
- [ ] `ctest --test-dir build/tests/libslic3r -R AlignmentSnap` — all pass.

## Settings & persistence
- [ ] Snapping toolbar button opens the panel.
- [ ] Toggling each option and restarting OrcaSlicer preserves values (AppConfig `snap_align`).
- [ ] Master OFF (default on fresh config): dragging behaves exactly as before this feature.

## Drag behavior
- [ ] Edge align: drag a cube so an edge nears a neighbor edge → snaps flush.
- [ ] Center align: differing-size boxes share a centerline.
- [ ] Contact: boxes butt together with no gap; does not fire when not overlapping perpendicular.
- [ ] Row propagation: two aligned, evenly spaced boxes → third snaps to continue spacing; badges show.
- [ ] Hold Alt during drag → no snapping.
- [ ] Strength: raise it → snap is harder to break away from.

## Guides
- [ ] Green alignment line appears through aligned edge/center.
- [ ] Gray ghost boxes reveal for nearby neighbors.
- [ ] Pink spacing badges show gap distance during row snap.
- [ ] All guides clear on drop.

## Per-object opt-out
- [ ] Right-click → "Enable snap alignment" toggles; checkmark reflects state.
- [ ] Excluded object: no snapping to it, and dragging it does not snap.

## Backward compatibility
- [ ] Old `.3mf` (no key) loads with all objects enabled.
- [ ] Save with one object excluded, reload → exclusion persists.
- [ ] Feature disabled → object drag is byte-for-byte the prior behavior.
```

- [ ] **Step 2: Run the automated suite one final time**

Run: `ctest --test-dir build/tests/libslic3r -R AlignmentSnap --output-on-failure`
Expected: all pass. Then walk the manual checklist in a running build and check boxes.

- [ ] **Step 3: Commit**
```bash
git add docs/superpowers/verification/2026-07-27-relative-alignment-snapping.md
git commit -m "docs(align-snap): manual verification + backward-compat checklist"
```

---

## Self-review notes (for the implementer)

- **Spec coverage:** Task 2–4 → reqs #1/#2; Task 7 → req #3/#4; Task 8 → req #7; Task 10 → req #5; Tasks 13–14 → req #6. All seven requirements have tasks.
- **Codebase-name caveats:** Several GUI accessor names (`get_object_idxs`, `get_partplate_list`, `instance_bounding_box`, `project_vertex`, `append_menu_check_item`, the line-drawing helper, `slider_float`) are called out at their use sites to confirm against the actual headers before compiling. These are the only non-verified symbols; the libslic3r core (Tasks 1–8) uses only confirmed types (`BoundingBoxf`, `Vec2d`).
- **Open items from the spec:** default master toggle = OFF (opt-in) as implemented in Task 9; default `sensitivity_px=8`, `strength_px=6`; toolbar icon is a placeholder copy (Task 10 Step 2) to be replaced with bespoke art.

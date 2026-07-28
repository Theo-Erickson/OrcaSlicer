#include <catch2/catch_all.hpp>
#include "libslic3r/AlignmentSnap.hpp"

using namespace Slic3r;
using namespace Slic3r::AlignmentSnap;
using Catch::Matchers::WithinAbs;

static BoundingBoxf box(double x0, double y0, double x1, double y1) {
    return BoundingBoxf(Vec2d(x0, y0), Vec2d(x1, y1));
}

TEST_CASE("no targets returns raw delta unchanged", "[AlignmentSnap]") {
    SnapSettings s; s.enabled = true;
    SnapState st;
    SnapResult r = compute_snap(box(0,0,10,10), Vec2d(3.0, 4.0), {}, s, 1.0, st);
    REQUIRE_THAT(r.corrected_delta.x(), WithinAbs(3.0, 1e-6));
    REQUIRE_THAT(r.corrected_delta.y(), WithinAbs(4.0, 1e-6));
}

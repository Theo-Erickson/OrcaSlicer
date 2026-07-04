#include <catch2/catch_all.hpp>

#include "libslic3r/NonplanarSpiral.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

#include <cmath>
#include <vector>
#include <optional>

using namespace Slic3r;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double LAYER_H = 0.2;

// A regular many-gon approximating a circle of radius r_mm centered at c (mm).
ExPolygon make_circle(const Vec2d& c, double r_mm, int segs = 128)
{
    Polygon p;
    p.points.reserve(segs);
    for (int i = 0; i < segs; ++i) {
        const double a = 2.0 * PI * i / segs;   // CCW
        p.points.push_back(Point::new_scale(c.x() + r_mm * std::cos(a),
                                            c.y() + r_mm * std::sin(a)));
    }
    return ExPolygon(p);
}

// An axis-aligned square of the given half-extent (mm), CCW.
ExPolygon make_square(const Vec2d& c, double half_mm)
{
    Polygon p;
    p.points = {
        Point::new_scale(c.x() - half_mm, c.y() - half_mm),
        Point::new_scale(c.x() + half_mm, c.y() - half_mm),
        Point::new_scale(c.x() + half_mm, c.y() + half_mm),
        Point::new_scale(c.x() - half_mm, c.y() + half_mm),
    };
    return ExPolygon(p);
}

NonplanarLayerSlice slice_at(int i, ExPolygon island)
{
    NonplanarLayerSlice s;
    s.print_z = (i + 1) * LAYER_H;
    s.islands.emplace_back(std::move(island));
    return s;
}

} // namespace

TEST_CASE("Nonplanar region: cone is detected as a full cap", "[Nonplanar][Spiral]")
{
    // Truncated cone: radius decreases linearly with height from R at the base to ~0.1R.
    const Vec2d  center(0.0, 0.0);
    const double R = 20.0;
    const int    N = 100;
    std::vector<NonplanarLayerSlice> layers;
    for (int i = 0; i < N; ++i) {
        const double frac = static_cast<double>(i) / (N - 1);   // 0 at base, 1 at top
        const double r    = R * (1.0 - 0.9 * frac);
        layers.push_back(slice_at(i, make_circle(center, r)));
    }

    auto region = detect_nonplanar_region(layers);
    REQUIRE(region.has_value());
    REQUIRE(region->valid());
    REQUIRE(region->last_layer == N - 1);
    REQUIRE(region->first_layer == 0);
    REQUIRE_THAT(region->base_radius, Catch::Matchers::WithinAbs(R, 0.3));
    REQUIRE_THAT(region->center.x(), Catch::Matchers::WithinAbs(0.0, 0.05));
    REQUIRE_THAT(region->center.y(), Catch::Matchers::WithinAbs(0.0, 0.05));
}

TEST_CASE("Nonplanar region: cap is bounded above a cylinder", "[Nonplanar][Spiral]")
{
    // Bottom half: constant-radius cylinder (vertical wall — must be excluded).
    // Top half: cone shrinking from the cylinder radius toward the apex.
    const Vec2d  center(0.0, 0.0);
    const double Rc = 15.0;
    const int    n_cyl = 20;
    const int    n_dome = 20;
    std::vector<NonplanarLayerSlice> layers;
    for (int i = 0; i < n_cyl; ++i)
        layers.push_back(slice_at(i, make_circle(center, Rc)));
    for (int i = 0; i < n_dome; ++i) {
        const double frac = static_cast<double>(i + 1) / n_dome;
        const double r    = Rc * (1.0 - 0.9 * frac);
        layers.push_back(slice_at(n_cyl + i, make_circle(center, r)));
    }

    auto region = detect_nonplanar_region(layers);
    REQUIRE(region.has_value());
    REQUIRE(region->last_layer == n_cyl + n_dome - 1);
    // The cap must start near the cylinder/dome junction, not at the bottom of the cylinder.
    REQUIRE(region->first_layer > 12);
    REQUIRE(region->first_layer <= 22);
    REQUIRE_THAT(region->base_radius, Catch::Matchers::WithinAbs(Rc, 1.0));
}

TEST_CASE("Nonplanar region: a box has no cap", "[Nonplanar][Spiral]")
{
    std::vector<NonplanarLayerSlice> layers;
    for (int i = 0; i < 50; ++i)
        layers.push_back(slice_at(i, make_square({0.0, 0.0}, 15.0)));

    REQUIRE_FALSE(detect_nonplanar_region(layers).has_value());
}

TEST_CASE("Nonplanar region: too few layers is rejected", "[Nonplanar][Spiral]")
{
    std::vector<NonplanarLayerSlice> layers;
    layers.push_back(slice_at(0, make_circle({0.0, 0.0}, 10.0)));
    layers.push_back(slice_at(1, make_circle({0.0, 0.0}, 9.0)));

    REQUIRE_FALSE(detect_nonplanar_region(layers).has_value());
}

TEST_CASE("Nonplanar region: explicit override forces the region", "[Nonplanar][Spiral]")
{
    // A plain cylinder would auto-detect nothing, but the override forces a region.
    std::vector<NonplanarLayerSlice> layers;
    for (int i = 0; i < 100; ++i)
        layers.push_back(slice_at(i, make_circle({0.0, 0.0}, 12.0)));

    NonplanarRegionOverride ovr;
    ovr.enabled        = true;
    ovr.first_layer    = 30;
    ovr.last_layer     = 80;
    ovr.center_mm      = Vec2d(5.0, -3.0);
    ovr.base_radius_mm = 7.0;

    auto region = detect_nonplanar_region(layers, ovr);
    REQUIRE(region.has_value());
    REQUIRE(region->first_layer == 30);
    REQUIRE(region->last_layer == 80);
    REQUIRE_THAT(region->base_radius, Catch::Matchers::WithinAbs(7.0, 1e-9));
    REQUIRE_THAT(region->center.x(), Catch::Matchers::WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(region->center.y(), Catch::Matchers::WithinAbs(-3.0, 1e-9));
    REQUIRE_THAT(region->base_z, Catch::Matchers::WithinAbs(31 * LAYER_H, 1e-9));
}

namespace {

// Builds a region for a hemisphere dome of radius R centered at c, base at z=0.
NonplanarTopRegion hemi_region(const Vec2d& c, double R)
{
    NonplanarTopRegion r;
    r.first_layer = 0;
    r.last_layer  = 99;
    r.center      = c;
    r.base_radius = R;
    r.base_z      = 0.0;
    r.apex_z      = R;
    return r;
}

double xy_radius(const Vec3d& p, const Vec2d& c)
{
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

} // namespace

TEST_CASE("Nonplanar spiral: points ride an analytic hemisphere", "[Nonplanar][Spiral]")
{
    const Vec2d  center(100.0, 100.0);
    const double R = 20.0;

    // z = sqrt(R^2 - d^2): the surface of a hemisphere with its base circle at z=0.
    auto hemi = [center, R](const Vec2d& xy) -> std::optional<double> {
        const double dx = xy.x() - center.x();
        const double dy = xy.y() - center.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 > R * R)
            return std::nullopt;
        return std::sqrt(std::max(0.0, R * R - d2));
    };

    NonplanarSpiralParams params;
    params.line_width      = 1.0;   // coarse pitch keeps the point count small for the test
    params.points_per_rev  = 90;
    params.transition_revs = 0.5;

    const auto pts = generate_spiral(hemi_region(center, R), params, hemi);

    REQUIRE(pts.size() > 100);

    // Starts at the base seam: radius == base_radius, Z == base_z (0).
    REQUIRE_THAT(xy_radius(pts.front(), center), Catch::Matchers::WithinAbs(R, 1e-6));
    REQUIRE_THAT(pts.front().z(), Catch::Matchers::WithinAbs(0.0, 1e-6));

    // Ends at the apex: radius ~ 0, Z ~ R.
    REQUIRE_THAT(xy_radius(pts.back(), center), Catch::Matchers::WithinAbs(0.0, params.line_width));
    REQUIRE_THAT(pts.back().z(), Catch::Matchers::WithinAbs(R, 1e-3));

    // Radius is monotonically non-increasing, and past the transition ramp every point
    // sits exactly on the analytic surface.
    const size_t skip = static_cast<size_t>(params.transition_revs * params.points_per_rev) + 2;
    double prev_r = xy_radius(pts.front(), center) + 1e-6;
    for (size_t i = 0; i < pts.size(); ++i) {
        const double ri = xy_radius(pts[i], center);
        REQUIRE(ri <= prev_r + 1e-6);
        prev_r = ri;
        if (i >= skip) {
            const double expected = std::sqrt(std::max(0.0, R * R - ri * ri));
            REQUIRE_THAT(pts[i].z(), Catch::Matchers::WithinAbs(expected, 1e-5));
        }
    }
}

TEST_CASE("Nonplanar spiral: falls back to linear Z when the ray misses", "[Nonplanar][Spiral]")
{
    const Vec2d center(0.0, 0.0);
    const double R = 15.0;

    auto miss = [](const Vec2d&) -> std::optional<double> { return std::nullopt; };

    NonplanarSpiralParams params;
    params.line_width      = 1.0;
    params.points_per_rev  = 60;
    params.transition_revs = 0.0;   // isolate the fallback from the transition blend

    NonplanarTopRegion region = hemi_region(center, R);
    region.base_z = 2.0;
    region.apex_z = 12.0;

    const auto pts = generate_spiral(region, params, miss);

    REQUIRE(pts.size() > 50);
    // Linear base->apex estimate: monotonically rising from base_z to apex_z.
    REQUIRE_THAT(pts.front().z(), Catch::Matchers::WithinAbs(2.0, 1e-6));
    REQUIRE_THAT(pts.back().z(),  Catch::Matchers::WithinAbs(12.0, 1e-2));
    for (size_t i = 1; i < pts.size(); ++i)
        REQUIRE(pts[i].z() >= pts[i - 1].z() - 1e-9);
}

#include <catch2/catch_all.hpp>

#include "libslic3r/NonplanarSpiral.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

#include <cmath>
#include <vector>

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

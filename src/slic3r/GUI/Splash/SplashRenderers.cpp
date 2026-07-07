// SplashRenderers.cpp
//
// C++ ports of the prototype's BORDERS.* / BACKDROPS.* canvas renderers.
// See crashslicer_splash_FINAL.html for the source-of-truth math.

#include "SplashRenderers.hpp"
#include "SplashThemes.hpp"

#include <wx/graphics.h>
#include <wx/colour.h>
#include <wx/pen.h>
#include <wx/brush.h>
#include <wx/font.h>
#include <wx/string.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

constexpr double PI  = 3.14159265358979323846;
constexpr double TAU = 2.0 * PI;
constexpr double W   = SPLASH_W;
constexpr double H   = SPLASH_H;

// rgba(r,g,b,a) -> wxColour. Alpha is 0..1 like the prototype.
inline wxColour col(const SplashRGB& c, double a = 1.0)
{
    int alpha = int(a * 255.0 + 0.5);
    alpha = std::max(0, std::min(255, alpha));
    return wxColour(c.r, c.g, c.b, (unsigned char) alpha);
}

// JS ((t%1)+1)%1 — wrap into [0,1).
inline double wrap01(double t)
{
    double m = std::fmod(t, 1.0);
    return std::fmod(m + 1.0, 1.0);
}

inline void stroke_line(wxGraphicsContext* gc, double x0, double y0, double x1, double y1)
{
    wxGraphicsPath p = gc->CreatePath();
    p.MoveToPoint(x0, y0);
    p.AddLineToPoint(x1, y1);
    gc->StrokePath(p);
}

inline void fill_circle(wxGraphicsContext* gc, double x, double y, double r)
{
    wxGraphicsPath p = gc->CreatePath();
    p.AddCircle(x, y, r);
    gc->FillPath(p);
}

// wxGraphicsContext has no shadowBlur; emulate a glow by filling a few
// increasingly large, increasingly faint discs behind an opaque core.
void glow_dot(wxGraphicsContext* gc, double x, double y, double coreR,
              const SplashRGB& c, double coreAlpha)
{
    gc->SetPen(*wxTRANSPARENT_PEN);
    for (int i = 3; i >= 1; --i) {
        double rr = coreR + i * 2.5;
        double a  = coreAlpha * (0.14 / i);
        gc->SetBrush(wxBrush(col(c, a)));
        fill_circle(gc, x, y, rr);
    }
    gc->SetBrush(wxBrush(col(c, coreAlpha)));
    fill_circle(gc, x, y, coreR);
}

// ─────────────────────────────────────────────────────────────────────────
// CIRCUIT — PCB traces + travelling orbs
// ─────────────────────────────────────────────────────────────────────────

struct RouteSeg { double x0, y0, x1, y1, len, s; };
struct Route     { std::vector<RouteSeg> segs; double tot = 0.0; };
struct TraceSeg  { double x0, y0, x1, y1; };

struct CircuitGeom {
    std::vector<TraceSeg>                     segs;
    std::vector<std::pair<double, double>>    nodes;
    std::vector<Route>                        routes;
    CircuitGeom() { build(); }

    static Route build_route(const std::vector<std::pair<double, double>>& pts)
    {
        Route r;
        double tot = 0.0;
        for (size_t i = 1; i < pts.size(); ++i) {
            double dx = pts[i].first  - pts[i - 1].first;
            double dy = pts[i].second - pts[i - 1].second;
            double l  = std::sqrt(dx * dx + dy * dy);
            if (l <= 0.0) l = 0.001;
            r.segs.push_back({ pts[i - 1].first, pts[i - 1].second,
                               pts[i].first, pts[i].second, l, tot });
            tot += l;
        }
        r.tot = tot;
        return r;
    }

    void build()
    {
        segs = {
            {10,52,10,10},{10,10,52,10},{10,31,30,31},{31,10,31,23},
            {52,10,96,10},{96,10,96,22},{96,22,140,22},
            {10,52,10,96},{10,96,22,96},{22,96,22,140},
            {W-10,52,W-10,10},{W-10,10,W-52,10},{W-10,31,W-30,31},{W-31,10,W-31,23},
            {W-52,10,W-96,10},{W-96,10,W-96,22},{W-96,22,W-140,22},
            {W-10,52,W-10,96},{W-10,96,W-22,96},{W-22,96,W-22,140},
            {10,H-52,10,H-10},{10,H-10,52,H-10},{10,H-31,30,H-31},{31,H-10,31,H-23},
            {52,H-10,96,H-10},{96,H-10,96,H-22},{96,H-22,140,H-22},
            {10,H-52,10,H-96},{10,H-96,22,H-96},{22,H-96,22,H-140},
            {W-10,H-52,W-10,H-10},{W-10,H-10,W-52,H-10},{W-10,H-31,W-30,H-31},{W-31,H-10,W-31,H-23},
            {W-52,H-10,W-96,H-10},{W-96,H-10,W-96,H-22},{W-96,H-22,W-140,H-22},
            {W-10,H-52,W-10,H-96},{W-10,H-96,W-22,H-96},{W-22,H-96,W-22,H-140}
        };
        nodes = {
            {30,31},{31,23},{96,10},{140,22},{10,96},{22,140},
            {W-30,31},{W-31,23},{W-96,10},{W-140,22},{W-10,96},{W-22,140},
            {30,H-31},{31,H-23},{96,H-10},{140,H-22},{10,H-96},{22,H-140},
            {W-30,H-31},{W-31,H-23},{W-96,H-10},{W-140,H-22},{W-10,H-96},{W-22,H-140}
        };
        const std::vector<std::vector<std::pair<double, double>>> raw = {
            {{10,52},{10,10},{52,10},{96,10},{96,22},{140,22},{96,22},{96,10},{52,10},{10,10},{10,52}},
            {{W-10,52},{W-10,10},{W-52,10},{W-96,10},{W-96,22},{W-140,22},{W-96,22},{W-96,10},{W-52,10},{W-10,10},{W-10,52}},
            {{10,H-52},{10,H-96},{22,H-96},{22,H-140},{22,H-96},{10,H-96},{10,H-52},{10,H-10},{52,H-10},{10,H-10}},
            {{W-10,H-52},{W-10,H-96},{W-22,H-96},{W-22,H-140},{W-22,H-96},{W-10,H-96},{W-10,H-52},{W-10,H-10},{W-52,H-10},{W-10,H-10}}
        };
        for (auto& p : raw)
            routes.push_back(build_route(p));
    }
};

const CircuitGeom& circuit_geom()
{
    static const CircuitGeom g;
    return g;
}

std::pair<double, double> route_at(const Route& r, double t)
{
    double d = wrap01(t) * r.tot;
    for (const auto& sg : r.segs) {
        if (d <= sg.s + sg.len + 0.001) {
            double p = std::max(0.0, std::min(1.0, (d - sg.s) / sg.len));
            return { sg.x0 + (sg.x1 - sg.x0) * p, sg.y0 + (sg.y1 - sg.y0) * p };
        }
    }
    const auto& ls = r.segs.back();
    return { ls.x1, ls.y1 };
}

// ─────────────────────────────────────────────────────────────────────────
// TECHS-ALOTL B — circuit-vein tendrils + corner bio-nodes
// ─────────────────────────────────────────────────────────────────────────

struct VeinSeg { double ax, ay, bx, by, len, s; };
struct Vein     { std::vector<std::pair<double, double>> pts; std::vector<VeinSeg> segs; double tot = 0.0; };

// Prototype LCG: rand = (rand*9301+49297) % 233280; pr() = rand/233280.
std::vector<std::pair<double, double>> gen_vein(double ox, double oy, double dx, double dy, long long seed)
{
    std::vector<std::pair<double, double>> pts;
    pts.push_back({ ox, oy });
    double     x = ox, y = oy;
    double     ang = std::atan2(dy, dx);
    long long  rnd = seed;
    auto       pr = [&rnd]() -> double {
        rnd = (rnd * 9301 + 49297) % 233280;
        return double(rnd) / 233280.0;
    };
    const int steps = 6;
    for (int i = 0; i < steps; ++i) {
        ang += (pr() - 0.5) * 0.9;
        double len = 18.0 + pr() * 14.0;
        x += std::cos(ang) * len;
        y += std::sin(ang) * len;
        pts.push_back({ x, y });
    }
    return pts;
}

Vein build_vein(std::vector<std::pair<double, double>> pts)
{
    Vein v;
    v.pts = std::move(pts);
    double tot = 0.0;
    for (size_t i = 1; i < v.pts.size(); ++i) {
        double dx = v.pts[i].first  - v.pts[i - 1].first;
        double dy = v.pts[i].second - v.pts[i - 1].second;
        double l  = std::sqrt(dx * dx + dy * dy);
        v.segs.push_back({ v.pts[i - 1].first, v.pts[i - 1].second,
                           v.pts[i].first, v.pts[i].second, l, tot });
        tot += l;
    }
    v.tot = tot;
    return v;
}

std::pair<double, double> vein_point_at(const Vein& v, double d)
{
    for (const auto& s : v.segs) {
        if (d <= s.s + s.len) {
            double p = (s.len > 0.0) ? (d - s.s) / s.len : 0.0;
            return { s.ax + (s.bx - s.ax) * p, s.ay + (s.by - s.ay) * p };
        }
    }
    return v.pts.back();
}

const std::vector<Vein>& techsalotl_veins()
{
    static const std::vector<Vein> veins = [] {
        std::vector<Vein> v;
        v.push_back(build_vein(gen_vein(4,     4,    1,  1.0,  11)));
        v.push_back(build_vein(gen_vein(W - 4, 4,   -1,  1.0,  29)));
        v.push_back(build_vein(gen_vein(4,     H-4,  1, -1.0,  53)));
        v.push_back(build_vein(gen_vein(W - 4, H-4, -1, -1.0,  71)));
        v.push_back(build_vein(gen_vein(4,     4,    1,  0.4, 101)));
        v.push_back(build_vein(gen_vein(W - 4, H-4, -1, -0.4, 131)));
        return v;
    }();
    return veins;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════
// CIRCUIT border
// ═════════════════════════════════════════════════════════════════════════
void draw_border_circuit(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c   = charge;
    const double el  = double(el_ms);
    const auto&  G   = circuit_geom();

    const SplashRGB dim{ 0, 80, 30 }, br{ 0, 255, 100 };
    const SplashRGB colr = lerp_rgb(dim, br, (float) c);
    const SplashRGB oc   = lerp_rgb(dim, SplashRGB{ 80, 255, 140 }, (float) std::min(1.0, c * 1.3));

    // Traces: a faint wide underlay approximates the canvas shadowBlur glow,
    // then the crisp stroke on top.
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    if (c > 0.0) {
        gc->SetPen(wxPen(col(br, 0.35 * c), 4.0));
        for (const auto& s : G.segs)
            stroke_line(gc, s.x0, s.y0, s.x1, s.y1);
    }
    gc->SetPen(wxPen(col(colr), 1.5));
    for (const auto& s : G.segs)
        stroke_line(gc, s.x0, s.y0, s.x1, s.y1);

    // Nodes.
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(col(colr)));
    for (const auto& n : G.nodes)
        fill_circle(gc, n.first, n.second, 2.5);

    // Travelling orbs along the four routes.
    static const double PH[4] = { 0.0, 0.5, 0.25, 0.75 };
    static const double SP[4] = { 2400.0, 2400.0, 2600.0, 2600.0 };
    for (int i = 0; i < 4; ++i) {
        double t  = wrap01(el / SP[i] + PH[i]);
        auto   p  = route_at(G.routes[i], t);
        double op = 0.45 + 0.55 * c;
        glow_dot(gc, p.first, p.second, 3.5 + c, oc, op);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// TECHS-ALOTL B backdrop
// ═════════════════════════════════════════════════════════════════════════
void draw_backdrop_techsalotl(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c    = charge;
    const double el   = double(el_ms);
    const SplashRGB teal{ 40, 190, 150 }, amber{ 224, 160, 40 };
    const auto& veins = techsalotl_veins();

    for (size_t vi = 0; vi < veins.size(); ++vi) {
        const Vein& v    = veins[vi];
        double      grow = std::min(1.0, c * 1.2);
        double      drawLen = v.tot * grow;

        // Faint growing vein path.
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(wxPen(col(teal, 0.10 * c), 1.4));
        {
            wxGraphicsPath path = gc->CreatePath();
            path.MoveToPoint(v.pts[0].first, v.pts[0].second);
            double acc = 0.0;
            for (size_t i = 1; i < v.pts.size(); ++i) {
                const VeinSeg& seg = v.segs[i - 1];
                if (acc + seg.len <= drawLen) {
                    path.AddLineToPoint(v.pts[i].first, v.pts[i].second);
                    acc += seg.len;
                } else {
                    auto p = vein_point_at(v, drawLen);
                    path.AddLineToPoint(p.first, p.second);
                    break;
                }
            }
            gc->StrokePath(path);
        }

        // Travelling pulse of light along the vein.
        const double pulseCycle = 1800.0;
        double pd = std::fmod(el + vi * 300.0, pulseCycle) / pulseCycle * v.tot;
        if (pd <= drawLen) {
            auto pp = vein_point_at(v, pd);
            glow_dot(gc, pp.first, pp.second, 2.0 + c, amber, 0.5 * c);
        }

        // Node dots at vein joints.
        gc->SetPen(*wxTRANSPARENT_PEN);
        for (size_t pi = 1; pi < v.pts.size(); ++pi) {
            double fl = 0.4 + 0.6 * std::sin(el / 600.0 + double(pi) + double(vi));
            gc->SetBrush(wxBrush(col(teal, 0.14 * c * fl)));
            fill_circle(gc, v.pts[pi].first, v.pts[pi].second, 1.4);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// TECHS-ALOTL B border
// ═════════════════════════════════════════════════════════════════════════
void draw_border_techsalotl(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c   = charge;
    const double el  = double(el_ms);
    const SplashRGB teal{ 40, 200, 160 }, lav{ 160, 140, 230 };

    // Breathing frame.
    double breath = 0.5 + 0.5 * std::sin(el / 1000.0);
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(col(lerp_rgb(teal, lav, (float) breath), 0.4 + 0.3 * c), 1.2));
    {
        wxGraphicsPath frame = gc->CreatePath();
        frame.AddRectangle(5, 5, W - 10, H - 10);
        gc->StrokePath(frame);
    }

    // Corner bio-nodes where the veins originate.
    const std::pair<double, double> corners[4] = {
        { 4, 4 }, { W - 4, 4 }, { 4, H - 4 }, { W - 4, H - 4 }
    };
    for (int i = 0; i < 4; ++i) {
        double pulse = 0.5 + 0.5 * std::sin(el / 700.0 + i * 1.6);
        double r     = 5.0 + 2.0 * pulse * c;
        // Soft filled core (glow emulation) + crisp ring.
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(col(teal, 0.3 * c * pulse)));
        fill_circle(gc, corners[i].first, corners[i].second, r);
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(wxPen(col(lerp_rgb(teal, lav, (float) pulse), 0.7 * c), 1.4));
        {
            wxGraphicsPath ring = gc->CreatePath();
            ring.AddCircle(corners[i].first, corners[i].second, r);
            gc->StrokePath(ring);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Shared helpers + per-theme state for the remaining renderers.
// (Same unnamed namespace as above within this TU, so the earlier helpers —
//  col/glow_dot/fill_circle/stroke_line/wrap01/W/H/PI/TAU — are all visible.)
// ═════════════════════════════════════════════════════════════════════════
namespace {

inline wxColour rgba(int r, int g, int b, double a)
{
    int alpha = int(a * 255.0 + 0.5);
    alpha = std::max(0, std::min(255, alpha));
    return wxColour(r, g, b, (unsigned char) alpha);
}

void fill_rect(wxGraphicsContext* gc, double x, double y, double w, double h)
{
    if (w <= 0.0 || h <= 0.0)
        return;
    wxGraphicsPath p = gc->CreatePath();
    p.AddRectangle(x, y, w, h);
    gc->FillPath(p);
}

void stroke_rect(wxGraphicsContext* gc, double x, double y, double w, double h)
{
    wxGraphicsPath p = gc->CreatePath();
    p.AddRectangle(x, y, w, h);
    gc->StrokePath(p);
}

wxFont mono_font(int pt)
{
    return wxFont(wxFontInfo(pt).Family(wxFONTFAMILY_TELETYPE));
}

// Point on an inset-rectangle perimeter at arc-length d (shared: crashspace, retro).
std::pair<double, double> rect_perimeter_at(double inset, double d)
{
    double w = W - inset * 2, h = H - inset * 2;
    if (d < w) return { inset + d, inset };
    d -= w;
    if (d < h) return { inset + w, inset + d };
    d -= h;
    if (d < w) return { inset + w - d, inset + h };
    d -= w;
    return { inset, inset + h - d };
}

struct GlitchBlock { double y, h, shift, a; SplashRGB colr; };
struct Fish        { double lane, speed, size, phase, dir, wobFreq; };
struct TetPiece    { double x; int shape; int colr; double speed, phase, size; };
struct RuneSlot    { double x, y; int type; double cycle, offset, size; };

void draw_fish(wxGraphicsContext* gc, double x, double y, double size, double dir,
               const SplashRGB& colr, double a, double t)
{
    gc->PushState();
    gc->Translate(x, y);
    gc->Scale(dir * size, size);
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(col(colr, a)));
    gc->DrawEllipse(-10, -4, 20, 8);                 // body (rx10, ry4)
    double wag = std::sin(t * 6.0) * 2.0;
    wxGraphicsPath tail = gc->CreatePath();
    tail.MoveToPoint(-9, 0);
    tail.AddLineToPoint(-16, -4 + wag);
    tail.AddLineToPoint(-16, 4 + wag);
    tail.CloseSubpath();
    gc->FillPath(tail);
    gc->PopState();
}

void rune_glyph(wxGraphicsContext* gc, int type, double s)
{
    wxGraphicsPath p = gc->CreatePath();
    switch (type) {
    case 0: p.MoveToPoint(0,-s); p.AddLineToPoint(0,s); p.MoveToPoint(0,-s); p.AddLineToPoint(s*0.7,-s*0.4); p.MoveToPoint(0,0); p.AddLineToPoint(s*0.7,s*0.4); break;
    case 1: p.MoveToPoint(-s*0.6,-s); p.AddLineToPoint(-s*0.6,s); p.AddLineToPoint(s*0.6,s); break;
    case 2: p.MoveToPoint(0,-s); p.AddLineToPoint(-s*0.7,s); p.AddLineToPoint(s*0.7,s); p.CloseSubpath(); break;
    case 3: p.MoveToPoint(-s*0.6,-s); p.AddLineToPoint(s*0.6,-s); p.MoveToPoint(0,-s); p.AddLineToPoint(0,s); break;
    case 4: p.MoveToPoint(-s*0.6,-s); p.AddLineToPoint(s*0.6,s); p.MoveToPoint(s*0.6,-s); p.AddLineToPoint(-s*0.6,s); break;
    case 5: p.MoveToPoint(0,-s); p.AddLineToPoint(0,s); p.MoveToPoint(-s*0.6,-s*0.5); p.AddLineToPoint(s*0.6,-s*0.5); p.MoveToPoint(-s*0.6,s*0.5); p.AddLineToPoint(s*0.6,s*0.5); break;
    default: break;
    }
    gc->StrokePath(p);
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════
// MAKERSPACE border — ruler ticks + travelling caliper
// ═════════════════════════════════════════════════════════════════════════
void draw_border_makerspace(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB dim{ 60, 40, 10 }, bright{ 255, 170, 30 };
    const SplashRGB lc = lerp_rgb(dim, bright, (float) c);

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(col(lc, 0.6 + 0.4 * c), 1));
    stroke_rect(gc, 8, 8, W - 16, H - 16);

    const int major = 50;
    for (int i = 0; i <= (int) W; i += 10) {
        if (i > 8 && i < W - 8) {
            bool   isM = (i % major == 0);
            double len = isM ? 9 : 5;
            gc->SetPen(wxPen(col(lc, (isM ? 0.7 : 0.35) * c + 0.1), 1));
            stroke_line(gc, i, 8, i, 8 + len);
            stroke_line(gc, i, H - 8, i, H - 8 - len);
        }
    }
    for (int j = 0; j <= (int) H; j += 10) {
        if (j > 8 && j < H - 8) {
            bool   isM = (j % major == 0);
            double len = isM ? 9 : 5;
            gc->SetPen(wxPen(col(lc, (isM ? 0.7 : 0.35) * c + 0.1), 1));
            stroke_line(gc, 8, j, 8 + len, j);
            stroke_line(gc, W - 8, j, W - 8 - len, j);
        }
    }

    double phase = wrap01(el / 2400.0);
    double cx    = 8 + (W - 16) * phase;
    gc->SetPen(wxPen(col(bright, 0.35 * c), 3));   // glow underlay
    stroke_line(gc, cx, H - 8, cx, H - 20);
    gc->SetPen(wxPen(col(bright, 0.6 + 0.4 * c), 2));
    stroke_line(gc, cx, H - 8, cx, H - 20);
    stroke_line(gc, cx - 4, H - 20, cx + 4, H - 20);
}

// ═════════════════════════════════════════════════════════════════════════
// CRASH SPACE border — CNC toolpath tracing the frame
// ═════════════════════════════════════════════════════════════════════════
void draw_border_crashspace(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB base{ 30, 90, 235 }, bright{ 90, 170, 255 };
    const SplashRGB lc = lerp_rgb(base, bright, (float) c);
    const double inset = 14;

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    {
        wxPen dashed(col(lc, 0.3 + 0.3 * c), 1, wxPENSTYLE_SHORT_DASH);
        gc->SetPen(dashed);
        stroke_rect(gc, inset, inset, W - inset * 2, H - inset * 2);
    }

    double peri   = 2 * (W - inset * 2) + 2 * (H - inset * 2);
    double travel = wrap01(el / 3200.0) * peri;

    gc->SetPen(wxPen(col(bright, 0.6 + 0.4 * c), 2));
    {
        wxGraphicsPath path = gc->CreatePath();
        int    steps    = 60;
        double trailLen = std::min(travel, peri * 0.28);
        for (int i = 0; i <= steps; ++i) {
            double d = travel - trailLen + (trailLen * i / steps);
            if (d < 0) d += peri;
            auto p = rect_perimeter_at(inset, std::fmod(d, peri));
            if (i == 0) path.MoveToPoint(p.first, p.second);
            else        path.AddLineToPoint(p.first, p.second);
        }
        gc->StrokePath(path);
    }

    auto head = rect_perimeter_at(inset, travel);
    gc->PushState();
    gc->Translate(head.first, head.second);
    gc->SetPen(wxPen(col(bright, 0.9), 1));
    {
        wxGraphicsPath ring = gc->CreatePath();
        ring.AddCircle(0, 0, 5);
        gc->StrokePath(ring);
    }
    stroke_line(gc, -8, 0, 8, 0);
    stroke_line(gc, 0, -8, 0, 8);
    gc->PopState();

    const std::pair<double, double> corners[4] = {
        { inset, inset }, { W - inset, inset }, { inset, H - inset }, { W - inset, H - inset }
    };
    gc->SetPen(wxPen(col(lc, 0.5 + 0.3 * c), 1));
    for (auto& p : corners) {
        wxGraphicsPath cp = gc->CreatePath();
        cp.AddCircle(p.first, p.second, 4);
        gc->StrokePath(cp);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// SYNTHWAVE border — neon frame + corner accents
// ═════════════════════════════════════════════════════════════════════════
void draw_border_synthwave(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge;
    const SplashRGB base{ 180, 0, 220 }, hot{ 255, 60, 240 };

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(col(hot, 0.4 * c), 4));    // glow underlay
    stroke_rect(gc, 4, 4, W - 8, H - 8);
    gc->SetPen(wxPen(col(lerp_rgb(base, hot, (float) c), 0.7 + 0.3 * c), 2));
    stroke_rect(gc, 4, 4, W - 8, H - 8);

    const double a3 = 22;
    const std::pair<double, double> corners[4] = { { 4, 4 }, { W - 4, 4 }, { 4, H - 4 }, { W - 4, H - 4 } };
    gc->SetPen(wxPen(col(hot, 0.9 * c), 2));
    for (int i = 0; i < 4; ++i) {
        double sx = (i % 2 == 0) ? 1 : -1;
        double sy = (i < 2) ? 1 : -1;
        stroke_line(gc, corners[i].first, corners[i].second, corners[i].first + sx * a3, corners[i].second);
        stroke_line(gc, corners[i].first, corners[i].second, corners[i].first, corners[i].second + sy * a3);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// SYNTHWAVE backdrop — infinite perspective highway
// ═════════════════════════════════════════════════════════════════════════
void draw_backdrop_synthwave(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const double horizonY = H * 0.42;
    const double vx = W / 2.0;

    {
        wxGraphicsBrush g = gc->CreateLinearGradientBrush(0, horizonY - 60, 0, horizonY,
            rgba(40, 0, 60, 0.0), rgba(255, 40, 180, 0.10 * c));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(g);
        fill_rect(gc, 0, horizonY - 60, W, 60);
    }

    gc->SetPen(wxPen(rgba(255, 60, 200, 0.25 * c), 1));
    stroke_line(gc, 0, horizonY, W, horizonY);

    double scroll = std::fmod(el / 1000.0, 1.0);
    for (int i = 0; i < 14; ++i) {
        double t = (i + scroll) / 14.0;
        double y = horizonY + (H - horizonY) * (t * t);
        if (y < horizonY || y > H) continue;
        gc->SetPen(wxPen(rgba(220, 30, 200, 0.05 + 0.14 * t * c), 1));
        stroke_line(gc, 0, y, W, y);
    }

    int nV = 13;
    for (int v = 0; v <= nV; ++v) {
        double fx = (double(v) / nV) * W;
        gc->SetPen(wxPen(rgba(180, 20, 200, 0.06 + 0.10 * c), 1));
        stroke_line(gc, fx, H, vx, horizonY);
    }

    double laneScroll = std::fmod(el / 400.0, 1.0);
    for (int m = 0; m < 10; ++m) {
        double t2 = (m + laneScroll) / 10.0;
        double y2 = horizonY + (H - horizonY) * (t2 * t2);
        if (y2 < horizonY) continue;
        double w2  = 1 + t2 * 4;
        double len = 4 + t2 * 16;
        gc->SetPen(wxPen(rgba(255, 220, 120, 0.16 * c), std::max(1, int(w2 + 0.5))));
        stroke_line(gc, vx, y2, vx, std::min(H, y2 + len));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// CYBERPUNK border — RGB split, HUD, datamosh, scanline tear
// ═════════════════════════════════════════════════════════════════════════
void draw_border_cyberpunk(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB cyan{ 0, 255, 220 }, red{ 255, 30, 70 }, yellow{ 255, 220, 40 };

    static std::mt19937 rng(1234567u);
    auto rnd = [&]() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); };

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    double splitAmt = (1.5 + std::sin(el / 200.0) * 1.2) * c;
    gc->SetPen(wxPen(rgba(255, 0, 60, 0.5 * c), 1));   stroke_rect(gc, 6 - splitAmt, 6, W - 12, H - 12);
    gc->SetPen(wxPen(rgba(0, 255, 220, 0.5 * c), 1));  stroke_rect(gc, 6 + splitAmt, 6, W - 12, H - 12);
    gc->SetPen(wxPen(rgba(255, 255, 255, 0.3 * c), 1)); stroke_rect(gc, 6, 6, W - 12, H - 12);

    struct Br { double px, py, sx, sy; };
    const Br brackets[4] = { { 8, 8, 1, 1 }, { W - 8, 8, -1, 1 }, { 8, H - 8, 1, -1 }, { W - 8, H - 8, -1, -1 } };
    for (auto& b : brackets) {
        gc->SetPen(wxPen(col(cyan, 0.8 * c), 2));
        stroke_line(gc, b.px + b.sx * 2, b.py, b.px + b.sx * 30, b.py);
        stroke_line(gc, b.px, b.py + b.sy * 2, b.px, b.py + b.sy * 30);
        gc->SetPen(wxPen(col(red, 0.6 * c), 1));
        stroke_line(gc, b.px + b.sx * 10, b.py, b.px + b.sx * 10, b.py + b.sy * 7);
    }

    static const char* HEX = "0123456789ABCDEF";
    auto hexStr = [&](int n) { wxString s; for (int i = 0; i < n; ++i) s += HEX[int(rnd() * 16)]; return s; };
    gc->SetFont(mono_font(7), col(cyan, 0.45 * c));
    gc->DrawText(wxString("0x") + hexStr(6) + " :: " + hexStr(4), 44, 10);
    {
        gc->SetFont(mono_font(7), col(yellow, 0.4 * c));
        wxString s = hexStr(2) + "." + hexStr(2) + "." + hexStr(4);
        wxDouble tw, th, de, ex;
        gc->GetTextExtent(s, &tw, &th, &de, &ex);
        gc->DrawText(s, W - 44 - tw, H - 18);
    }

    static std::vector<GlitchBlock> glitch;
    static long lastGen = -1000;
    if (el - lastGen > 90) {
        lastGen = (long) el;
        glitch.clear();
        int n = int(rnd() * 4 * c);
        for (int i = 0; i < n; ++i) {
            GlitchBlock g;
            g.y = rnd() * H; g.h = 2 + rnd() * 10;
            g.shift = rnd() * 20 - 10;
            g.colr = (rnd() < 0.5) ? cyan : red;
            g.a = 0.08 + rnd() * 0.15;
            glitch.push_back(g);
        }
    }
    gc->SetPen(*wxTRANSPARENT_PEN);
    for (auto& g : glitch) {
        gc->SetBrush(wxBrush(col(g.colr, g.a * c)));
        fill_rect(gc, g.shift, g.y, W, g.h);
        gc->SetBrush(wxBrush(col(g.colr, g.a * 2 * c)));
        fill_rect(gc, g.shift, g.y, W, 1);
    }

    double tearY = (std::fmod(el, 2600.0) / 2600.0) * H;
    gc->SetPen(wxPen(col(cyan, 0.2 * c), 1));
    stroke_line(gc, 0, tearY, W, tearY);
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(col(cyan, 0.5 * c)));
    fill_rect(gc, 0, tearY - 1, std::max(0.0, W * (0.3 + 0.4 * std::sin(el / 300.0))), 2);

    gc->SetBrush(wxBrush(rgba(0, 0, 0, 0.06 + 0.04 * c)));
    for (double y = 0; y < H; y += 3)
        fill_rect(gc, 0, y, W, 1);
}

// ═════════════════════════════════════════════════════════════════════════
// CYBERPUNK backdrop — faint scrolling code rain
// ═════════════════════════════════════════════════════════════════════════
void draw_backdrop_cyberpunk(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB cyan{ 0, 255, 210 };
    static const std::vector<wxString> G = {
        wxString::FromUTF8("\xE3\x82\xA2"), wxString::FromUTF8("\xE3\x82\xA4"), wxString::FromUTF8("\xE3\x82\xA6"),
        "0","1","2","3","4","5","6","7","8","9","A","B","C","D","E","F"
    };
    const int cols = 16;
    for (int i = 0; i < cols; ++i) {
        double x     = (i + 0.5) * (W / cols);
        double speed = 500 + ((i * 137) % 400);
        double t     = wrap01(el / speed + i * 0.3);
        for (int k = 0; k < 4; ++k) {
            double y = t * H + k * 11;
            if (y > H) y -= H;
            const wxString& glyph = G[(long(std::floor(el / 160.0)) + i + k) % (long) G.size()];
            gc->SetFont(mono_font(7), col(cyan, (k == 0 ? 0.10 : 0.05) * c));
            wxDouble tw, th, de, ex;
            gc->GetTextExtent(glyph, &tw, &th, &de, &ex);
            gc->DrawText(glyph, x - tw / 2.0, y);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// DEEP SEA border — sonar frame + corner arcs + rising bubbles
// ═════════════════════════════════════════════════════════════════════════
void draw_border_deepsea(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB deep{ 0, 40, 100 }, ping{ 0, 140, 255 };

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(col(lerp_rgb(deep, ping, (float) c), 0.5 + 0.3 * c), 1));
    stroke_rect(gc, 3, 3, W - 6, H - 6);

    struct Corner { double cx, cy, a0, a1; };
    const Corner corners[4] = {
        { 0, 0, 0, PI / 2 }, { W, 0, PI / 2, PI }, { 0, H, PI * 1.5, PI * 2 }, { W, H, PI, PI * 1.5 }
    };
    gc->SetPen(wxPen(col(ping, 0.7 * c), 2));
    for (auto& co : corners) {
        wxGraphicsPath p = gc->CreatePath();
        p.AddArc(co.cx, co.cy, 26, co.a0, co.a1, true);
        gc->StrokePath(p);
    }

    gc->SetPen(*wxTRANSPARENT_PEN);
    int nbub = 12;
    for (int b = 0; b < nbub; ++b) {
        double bPhase = wrap01(el * 0.0004 + double(b) / nbub);
        double bx = (b < nbub / 2) ? 12 : W - 12;
        double by = H - (bPhase * H * 1.1);
        double br = 1.5 + std::sin(b * 1.3) * 1.2;
        double ba = std::sin(bPhase * PI) * 0.6 * c;
        if (br < 0.2) br = 0.2;
        gc->SetBrush(wxBrush(rgba(40, 180, 255, std::max(0.0, ba))));
        fill_circle(gc, bx, by, br);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// DEEP SEA backdrop — sonar pulses + swimming fish silhouettes
// ═════════════════════════════════════════════════════════════════════════
void draw_backdrop_deepsea(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB ping{ 0, 150, 255 };

    static std::vector<Fish> fish = [] {
        std::mt19937 g(4242u);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        std::vector<Fish> f;
        for (int i = 0; i < 7; ++i) {
            Fish x;
            x.lane    = 0.15 + u(g) * 0.7;
            x.speed   = 18 + u(g) * 24;
            x.size    = 0.7 + u(g) * 0.9;
            x.phase   = u(g);
            x.dir     = (u(g) < 0.5) ? 1 : -1;
            x.wobFreq = 1 + u(g) * 2;
            f.push_back(x);
        }
        return f;
    }();

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    double cx = W / 2.0, cy = H * 0.44, cycle = 2600;
    for (int p = 0; p < 3; ++p) {
        double phase = std::fmod(std::fmod(el - p * (cycle / 3), cycle) + cycle, cycle);
        double frac  = phase / cycle;
        double r     = frac * 180;
        double a     = std::max(0.0, (1 - frac) * 0.30) * c;
        gc->SetPen(wxPen(col(ping, a), 1));
        wxGraphicsPath pr = gc->CreatePath();
        pr.AddCircle(cx, cy, r);
        gc->StrokePath(pr);
    }

    double sec = el / 1000.0;
    for (size_t i = 0; i < fish.size(); ++i) {
        const Fish& f = fish[i];
        double span = W + 60;
        double x = std::fmod(std::fmod(f.phase * span + sec * f.speed * f.dir, span) + span, span) - 30;
        if (f.dir < 0) x = W - x;
        double y = H * f.lane + std::sin(sec * f.wobFreq + i) * 8;
        SplashRGB fcol = lerp_rgb(SplashRGB{ 0, 30, 70 }, SplashRGB{ 20, 90, 150 }, (float) c);
        draw_fish(gc, x, y, f.size, f.dir, fcol, 0.16 * c + 0.04, sec + i);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// RETRO border — Pac-Man perimeter + dots + chasing ghost
// ═════════════════════════════════════════════════════════════════════════
void draw_border_retro(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(rgba(0, 90, 255, 0.5 + 0.3 * c), 3));
    stroke_rect(gc, 8, 8, W - 16, H - 16);
    gc->SetPen(wxPen(rgba(60, 140, 255, 0.3 * c), 1));
    stroke_rect(gc, 12, 12, W - 24, H - 24);

    const double inset = 8;
    double peri  = 2 * (W - inset * 2) + 2 * (H - inset * 2);
    int    nDots = 44;
    double eat   = wrap01(el / 4200.0);

    gc->SetPen(*wxTRANSPARENT_PEN);
    for (int i = 0; i < nDots; ++i) {
        double frac = double(i) / nDots;
        if (frac < eat) continue;
        auto   p        = rect_perimeter_at(inset, frac * peri);
        bool   isPellet = (i % 11 == 0);
        double r        = isPellet ? 3.5 : 2;
        wxColour dotc;
        if (isPellet) { double blink = 0.5 + 0.5 * std::sin(el / 200.0); dotc = rgba(255, 255, 180, blink * 0.9); }
        else          { dotc = rgba(255, 255, 150, 0.85 * (0.6 + 0.4 * c)); }
        gc->SetBrush(wxBrush(dotc));
        fill_circle(gc, p.first, p.second, r);
    }

    // Pac-Man.
    auto   pac   = rect_perimeter_at(inset, eat * peri);
    double mouth = std::fabs(std::sin(el / 120.0)) * 0.6 + 0.1;
    auto   nxt   = rect_perimeter_at(inset, std::fmod(eat + 0.002, 1.0) * peri);
    double ang   = std::atan2(nxt.second - pac.second, nxt.first - pac.first);
    gc->PushState();
    gc->Translate(pac.first, pac.second);
    gc->Rotate(ang);
    gc->SetBrush(wxBrush(wxColour(255, 224, 0)));
    {
        wxGraphicsPath pm = gc->CreatePath();
        pm.AddArc(0, 0, 7, mouth, TAU - mouth, true);
        pm.AddLineToPoint(0, 0);
        pm.CloseSubpath();
        gc->FillPath(pm);
    }
    gc->PopState();

    // Chasing ghost.
    double ghostFrac = std::fmod(eat - 0.06 + 1.0, 1.0);
    auto   gp        = rect_perimeter_at(inset, ghostFrac * peri);
    gc->PushState();
    gc->Translate(gp.first, gp.second);
    gc->SetBrush(wxBrush(rgba(255, 80, 200, 0.7 * c)));
    {
        wxGraphicsPath body = gc->CreatePath();
        body.AddArc(0, -1, 5, PI, 0, true);
        body.AddLineToPoint(5, 4);
        body.AddLineToPoint(3, 2);
        body.AddLineToPoint(1, 4);
        body.AddLineToPoint(-1, 2);
        body.AddLineToPoint(-3, 4);
        body.AddLineToPoint(-5, 4);
        body.CloseSubpath();
        gc->FillPath(body);
    }
    gc->SetBrush(wxBrush(wxColour(255, 255, 255)));
    {
        wxGraphicsPath eyes = gc->CreatePath();
        eyes.AddCircle(-2, -1, 1.5);
        eyes.AddCircle(2, -1, 1.5);
        gc->FillPath(eyes);
    }
    gc->PopState();
}

// ═════════════════════════════════════════════════════════════════════════
// RETRO backdrop — falling tetromino blocks
// ═════════════════════════════════════════════════════════════════════════
void draw_backdrop_retro(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    static const SplashRGB COLORS[7] = {
        { 255, 68, 68 }, { 255, 153, 68 }, { 255, 221, 68 }, { 68, 221, 68 },
        { 68, 221, 221 }, { 68, 136, 255 }, { 221, 68, 221 }
    };
    static const std::vector<std::vector<std::pair<int, int>>> SHAPES = {
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } },
        { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } },
        { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 } },
    };
    static std::vector<TetPiece> pieces = [] {
        std::mt19937 g(9090u);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        std::vector<TetPiece> v;
        for (int i = 0; i < 7; ++i) {
            TetPiece p;
            p.x     = u(g) * W;
            p.shape = i % 5;
            p.colr  = i % 7;
            p.speed = 10 + u(g) * 16;
            p.phase = u(g);
            p.size  = 9 + u(g) * 4;
            v.push_back(p);
        }
        return v;
    }();

    double sec = el / 1000.0;
    gc->SetPen(*wxTRANSPARENT_PEN);
    for (size_t i = 0; i < pieces.size(); ++i) {
        const TetPiece& p = pieces[i];
        double totalH = H + 80;
        double y      = std::fmod(std::fmod(p.phase * totalH + sec * p.speed, totalH) + totalH, totalH) - 40;
        const auto& cells = SHAPES[p.shape];
        const SplashRGB& colr = COLORS[p.colr];
        double alpha = 0.10 * c + 0.02;
        for (auto& cell : cells) {
            double bx = p.x + cell.first * p.size;
            double by = y + cell.second * p.size;
            gc->SetBrush(wxBrush(col(colr, alpha)));
            fill_rect(gc, bx, by, p.size - 1, p.size - 1);
            gc->SetBrush(wxBrush(rgba(255, 255, 255, 0.15 * alpha)));
            fill_rect(gc, bx, by, p.size - 1, 1);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// FANTASY border — rotating magic circle + runic ring
// ═════════════════════════════════════════════════════════════════════════
void draw_border_fantasy(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB purple{ 140, 40, 200 }, gold{ 220, 180, 60 };
    double cx = W / 2.0, cy = H / 2.0;
    double r   = std::min(W, H) * 0.44;
    double rot = el / 4000.0;

    gc->SetBrush(*wxTRANSPARENT_BRUSH);

    gc->PushState();
    gc->Translate(cx, cy);
    gc->Rotate(rot);
    int nGlyphs = 18;
    for (int i = 0; i < nGlyphs; ++i) {
        double a  = double(i) / nGlyphs * TAU;
        double gx = std::cos(a) * r, gy = std::sin(a) * r;
        double twinkle = 0.4 + 0.6 * std::sin(el / 600.0 + i * 0.8);
        gc->PushState();
        gc->Translate(gx, gy);
        gc->Rotate(a + PI / 2);
        gc->SetPen(wxPen(col(gold, (0.3 + 0.5 * twinkle) * c), 1));
        stroke_line(gc, -3, -3, 3, 3);
        stroke_line(gc, 3, -3, -3, 3);
        gc->PopState();
    }
    gc->PopState();

    gc->SetPen(wxPen(col(purple, 0.5 + 0.4 * c), 1));
    { wxGraphicsPath p = gc->CreatePath(); p.AddCircle(cx, cy, r * 0.80); gc->StrokePath(p); }
    { wxGraphicsPath p = gc->CreatePath(); p.AddCircle(cx, cy, r);        gc->StrokePath(p); }

    gc->PushState();
    gc->Translate(cx, cy);
    gc->Rotate(-rot * 0.6);
    gc->SetPen(wxPen(col(gold, 0.3 + 0.3 * c), 1));
    {
        wxGraphicsPath tri = gc->CreatePath();
        for (int t = 0; t < 3; ++t) {
            double ta = double(t) / 3 * TAU - PI / 2;
            double tx = std::cos(ta) * r * 0.80, ty = std::sin(ta) * r * 0.80;
            if (t == 0) tri.MoveToPoint(tx, ty);
            else        tri.AddLineToPoint(tx, ty);
        }
        tri.CloseSubpath();
        gc->StrokePath(tri);
    }
    gc->PopState();
}

// ═════════════════════════════════════════════════════════════════════════
// FANTASY backdrop — runes materialising and fading behind the logo
// ═════════════════════════════════════════════════════════════════════════
void draw_backdrop_fantasy(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& /*T*/)
{
    const double c = charge, el = double(el_ms);
    const SplashRGB gold{ 210, 170, 70 }, purple{ 170, 90, 230 };

    static std::vector<RuneSlot> slots = [] {
        std::vector<RuneSlot> v;
        for (int i = 0; i < 9; ++i) {
            double ang = double(i) / 9 * TAU;
            double rad = 70 + ((i * 53) % 50);
            RuneSlot s;
            s.x      = W / 2.0 + std::cos(ang) * rad;
            s.y      = H * 0.44 + std::sin(ang) * rad * 0.75;
            s.type   = i % 6;
            s.cycle  = 2000 + ((i * 311) % 1500);
            s.offset = (i * 270) % 2000;
            s.size   = 7 + ((i * 7) % 6);
            v.push_back(s);
        }
        return v;
    }();

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    for (size_t i = 0; i < slots.size(); ++i) {
        const RuneSlot& s = slots[i];
        double phase = std::fmod(el + s.offset, s.cycle) / s.cycle;
        double alpha;
        if (phase < 0.4)      alpha = phase / 0.4;
        else if (phase < 0.6) alpha = 1.0;
        else                  alpha = 1.0 - (phase - 0.6) / 0.4;
        alpha *= 0.22 * c;
        if (alpha <= 0.01) continue;
        gc->PushState();
        gc->Translate(s.x, s.y);
        gc->SetPen(wxPen(col((i % 2 == 0) ? gold : purple, alpha), 1));
        rune_glyph(gc, s.type, s.size);
        gc->PopState();
    }
}

} // namespace GUI
} // namespace Slic3r

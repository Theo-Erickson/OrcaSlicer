#pragma once
// SplashRenderers.hpp
//
// Per-theme drawing functions for the animated splash. Each is a near-mechanical
// port of a BORDERS.* / BACKDROPS.* function from crashslicer_splash_FINAL.html.
//
// PCH note: no wx headers here. wxGraphicsContext is forward-declared; the heavy
// includes live in the .cpp.
//
// Signature convention (matches the prototype's (ctx/bgx, el, c, T)):
//   gc      - target graphics context, coordinates in the fixed 480x360 space
//   el_ms   - elapsed milliseconds since the splash appeared
//   charge  - 0..1 build-up value (min(1, elapsed / 2600))
//   T       - the active theme's palette

class wxGraphicsContext;

namespace Slic3r {
namespace GUI {

struct SplashThemeDef;

// Fixed logical canvas size. All renderer geometry is authored against this;
// the frame applies a uniform scale for HiDPI.
constexpr int SPLASH_W = 480;
constexpr int SPLASH_H = 360;

// ── Border renderers (drawn in front of the logo/text) ──
void draw_border_circuit    (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_makerspace (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_crashspace (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_synthwave  (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_cyberpunk  (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_deepsea    (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_retro      (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_fantasy    (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_border_techsalotl (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);

// ── Backdrop renderers (drawn behind the logo/text) ──
void draw_backdrop_synthwave (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_backdrop_cyberpunk (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_backdrop_deepsea   (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_backdrop_retro     (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_backdrop_fantasy   (wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);
void draw_backdrop_techsalotl(wxGraphicsContext* gc, long el_ms, float charge, const SplashThemeDef& T);

} // namespace GUI
} // namespace Slic3r

#pragma once
// SplashThemes.hpp
//
// Theme model for the animated CrashSlicer splash screen.
//
// Kept deliberately PCH-light: no wxWidgets headers here. Colours are plain
// RGB triples so this header can be included from anywhere without dragging in
// GL/ImGui/wx. wxColour conversion lives in the renderers' .cpp.
//
// Every theme's key + palette is ported verbatim from the `THEMES` object in
// the crashslicer_splash_FINAL.html prototype. The active theme is persisted in
// AppConfig under "app"/"splash_theme"; see GUI_App.cpp.

#include <string>

namespace Slic3r {
namespace GUI {

// Plain RGB triple (0..255). Matches the prototype's [r,g,b] arrays.
struct SplashRGB {
    int r;
    int g;
    int b;
};

// One entry per theme, in registration order. Techs-alotl is the locked
// variant B design.
enum class SplashTheme {
    Circuit = 0,
    Makerspace,
    CrashSpace,
    Synthwave,
    Cyberpunk,
    DeepSea,
    Retro,
    Fantasy,
    Techsalotl,
    Count
};

// Static palette for a theme (colours the frame reads directly).
struct SplashThemeDef {
    const char* key;           // AppConfig value, e.g. "circuit"
    SplashRGB   bg;            // panel background fill
    SplashRGB   version_color; // version line
    SplashRGB   sub_color;     // "CrashSlicer" sub-label
    SplashRGB   status_color;  // status/action line
    SplashRGB   glow_color;    // logo background glow
};

// Palette lookup for a theme.
const SplashThemeDef& splash_theme_def(SplashTheme theme);

// Map an AppConfig key string to a theme. Unknown / empty -> Circuit.
SplashTheme theme_from_key(const std::string& key);

// The AppConfig key string for a theme.
const char* key_of(SplashTheme theme);

// Resolve a splash theme from a print-status icon theme id (the value stored in
// AppConfig "print_status_active_theme"). Matches case-insensitively, ignoring
// spaces / hyphens / underscores, so e.g. "Deep Sea" -> DeepSea and
// "Techs-alotl" -> Techsalotl. Falls back to Circuit when nothing matches.
SplashTheme theme_from_status_id(const std::string& status_theme_id);

// Sentinel splash_theme values that are NOT themes:
//   "classic"      -> use the legacy static splash screen
//   "match_status" -> follow the active print-status icon theme
inline const char* kSplashClassicKey     () { return "classic"; }
inline const char* kSplashMatchStatusKey () { return "match_status"; }

// Charge-driven colour lerp (ports the prototype's lerpRGB with |0 truncation).
inline SplashRGB lerp_rgb(const SplashRGB& a, const SplashRGB& b, float t)
{
    return SplashRGB{
        a.r + int((b.r - a.r) * t),
        a.g + int((b.g - a.g) * t),
        a.b + int((b.b - a.b) * t)
    };
}

} // namespace GUI
} // namespace Slic3r

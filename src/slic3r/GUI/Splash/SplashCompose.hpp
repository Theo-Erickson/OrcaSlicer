#pragma once
// SplashCompose.hpp
//
// Shared composition of the full splash image (backdrop -> logo+glow -> text ->
// border) in the fixed 480x360 logical space. Used by both the live SplashFrame
// and the Preferences SplashPreviewPanel so they render identically.
//
// The caller is responsible for: filling the theme background, and applying any
// scale transform on the graphics context before calling draw_splash().
//
// Light wx headers only (bitmap/font/string) — no GL/ImGui.

#include <wx/bitmap.h>
#include <wx/font.h>
#include <wx/string.h>

#include "SplashThemes.hpp"

class wxGraphicsContext;

namespace Slic3r {
namespace GUI {

struct SplashComposeInput {
    SplashTheme    theme  = SplashTheme::Circuit;
    long           el_ms  = 0;      // elapsed time (drives animation)
    float          charge = 1.0f;   // 0..1 build-up
    const wxBitmap* logo  = nullptr; // may be null
    wxString       version;
    wxString       sub;
    wxString       status;
    wxFont         font_version;
    wxFont         font_sub;
    wxFont         font_status;
};

// Draws the composed splash onto gc (in 480x360 logical coordinates).
void draw_splash(wxGraphicsContext* gc, const SplashComposeInput& in);

} // namespace GUI
} // namespace Slic3r

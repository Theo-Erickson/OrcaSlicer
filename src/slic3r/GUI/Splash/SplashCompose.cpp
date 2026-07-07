// SplashCompose.cpp
#include "SplashCompose.hpp"
#include "SplashRenderers.hpp"

#include <wx/graphics.h>
#include <wx/colour.h>
#include <wx/pen.h>
#include <wx/brush.h>

#include <algorithm>

namespace Slic3r {
namespace GUI {

namespace {
inline wxColour col(const SplashRGB& c, double a = 1.0)
{
    int alpha = int(a * 255.0 + 0.5);
    alpha = std::max(0, std::min(255, alpha));
    return wxColour(c.r, c.g, c.b, (unsigned char) alpha);
}

void draw_centered(wxGraphicsContext* gc, const wxFont& font, const wxColour& colour,
                   const wxString& text, double y)
{
    if (text.empty())
        return;
    gc->SetFont(font, colour);
    wxDouble tw = 0, th = 0, desc = 0, ext = 0;
    gc->GetTextExtent(text, &tw, &th, &desc, &ext);
    gc->DrawText(text, (SPLASH_W - tw) / 2.0, y);
}
} // anonymous namespace

void draw_splash(wxGraphicsContext* gc, const SplashComposeInput& in)
{
    const SplashThemeDef& T = splash_theme_def(in.theme);

    // ── Backdrop (behind logo/text) ──
    switch (in.theme) {
    case SplashTheme::Synthwave:  draw_backdrop_synthwave(gc, in.el_ms, in.charge, T);  break;
    case SplashTheme::Cyberpunk:  draw_backdrop_cyberpunk(gc, in.el_ms, in.charge, T);  break;
    case SplashTheme::DeepSea:    draw_backdrop_deepsea(gc, in.el_ms, in.charge, T);    break;
    case SplashTheme::Retro:      draw_backdrop_retro(gc, in.el_ms, in.charge, T);      break;
    case SplashTheme::Fantasy:    draw_backdrop_fantasy(gc, in.el_ms, in.charge, T);    break;
    case SplashTheme::Techsalotl: draw_backdrop_techsalotl(gc, in.el_ms, in.charge, T); break;
    default: break;
    }

    // ── Logo with background glow only ──
    const double logoW = 160.0, logoH = 160.0;
    const double logoX = (SPLASH_W - logoW) / 2.0;  // 160
    const double logoY = 71.0;                       // center ~(240,151) ~ H*0.42
    const double cx = logoX + logoW / 2.0;
    const double cy = logoY + logoH / 2.0;

    gc->SetPen(*wxTRANSPARENT_PEN);
    for (int i = 4; i >= 1; --i) {
        double rr = 44.0 + i * 20.0;
        double a  = (0.03 + 0.06 * in.charge) / i;
        gc->SetBrush(wxBrush(col(T.glow_color, a)));
        wxGraphicsPath p = gc->CreatePath();
        p.AddCircle(cx, cy, rr);
        gc->FillPath(p);
    }

    if (in.logo && in.logo->IsOk())
        gc->DrawBitmap(*in.logo, logoX, logoY, logoW, logoH);

    // ── Text stack ──
    draw_centered(gc, in.font_version, col(T.version_color), in.version, 236.0);
    draw_centered(gc, in.font_sub,     col(T.sub_color),     in.sub,     258.0);
    draw_centered(gc, in.font_status,  col(T.status_color),  in.status,  282.0);

    // ── Border (in front) ──
    switch (in.theme) {
    case SplashTheme::Circuit:    draw_border_circuit(gc, in.el_ms, in.charge, T);    break;
    case SplashTheme::Makerspace: draw_border_makerspace(gc, in.el_ms, in.charge, T); break;
    case SplashTheme::CrashSpace: draw_border_crashspace(gc, in.el_ms, in.charge, T); break;
    case SplashTheme::Synthwave:  draw_border_synthwave(gc, in.el_ms, in.charge, T);  break;
    case SplashTheme::Cyberpunk:  draw_border_cyberpunk(gc, in.el_ms, in.charge, T);  break;
    case SplashTheme::DeepSea:    draw_border_deepsea(gc, in.el_ms, in.charge, T);    break;
    case SplashTheme::Retro:      draw_border_retro(gc, in.el_ms, in.charge, T);      break;
    case SplashTheme::Fantasy:    draw_border_fantasy(gc, in.el_ms, in.charge, T);    break;
    case SplashTheme::Techsalotl: draw_border_techsalotl(gc, in.el_ms, in.charge, T); break;
    default: break;
    }
}

} // namespace GUI
} // namespace Slic3r

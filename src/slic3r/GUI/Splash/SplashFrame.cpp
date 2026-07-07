// SplashFrame.cpp
#include "SplashFrame.hpp"
#include "SplashRenderers.hpp"
#include "SplashCompose.hpp"

#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/pen.h>
#include <wx/brush.h>

#include <algorithm>

#include "../BitmapCache.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"

namespace Slic3r {
namespace GUI {

namespace {
inline wxColour col(const SplashRGB& c, double a = 1.0)
{
    int alpha = int(a * 255.0 + 0.5);
    alpha = std::max(0, std::min(255, alpha));
    return wxColour(c.r, c.g, c.b, (unsigned char) alpha);
}
} // anonymous namespace

SplashFrame::SplashFrame(SplashTheme theme, const wxPoint& pos)
    : wxFrame(nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxFRAME_TOOL_WINDOW)
    , m_theme(theme)
    , m_timer(this)
{
    // Required for wxAutoBufferedPaintDC (flicker-free full redraws).
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetClientSize(FromDIP(wxSize(SPLASH_W, SPLASH_H)));

    if (pos != wxDefaultPosition)
        SetPosition(pos);
    CenterOnScreen();

    // Reuse the existing SVG logo assets. The splash background is always dark,
    // so use the dark-mode variant.
    const wxSize logo_px = FromDIP(wxSize(160, 160));
    BitmapCache cache;
    if (wxBitmap* bmp = cache.load_svg("splash_logo_dark", logo_px.GetWidth(), logo_px.GetHeight()))
        m_logo = *bmp;

    m_version = GUI_App::format_display_version();
    m_sub     = "CrashSlicer";
    m_status  = _L("Loading configuration") + "...";

    m_font_version = wxFontInfo(11).Bold();
    m_font_sub     = wxFontInfo(7);
    m_font_status  = wxFontInfo(8);

    Bind(wxEVT_PAINT, &SplashFrame::OnPaint, this);
    Bind(wxEVT_TIMER, &SplashFrame::OnTimer, this);

    m_watch.Start();
    m_timer.Start(33);  // ~30 FPS
}

SplashFrame::~SplashFrame()
{
    if (m_timer.IsRunning())
        m_timer.Stop();
}

void SplashFrame::SetText(const wxString& text)
{
    if (!text.empty()) {
        m_status = text;
        Refresh(false);
        Update();
    }
}

void SplashFrame::OnTimer(wxTimerEvent& /*evt*/)
{
    Refresh(false);
}

void SplashFrame::OnPaint(wxPaintEvent& /*evt*/)
{
    const SplashThemeDef& T = splash_theme_def(m_theme);

    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(col(T.bg)));
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc)
        return;

    // Author everything in the fixed 480x360 space; scale for HiDPI.
    const wxSize cs = GetClientSize();
    gc->Scale(cs.GetWidth() / double(SPLASH_W), cs.GetHeight() / double(SPLASH_H));

    render(gc);

    delete gc;
}

void SplashFrame::render(wxGraphicsContext* gc)
{
    const long el = m_watch.Time();

    SplashComposeInput in;
    in.theme        = m_theme;
    in.el_ms        = el;
    in.charge       = std::min(1.0f, float(el) / 2600.0f);  // RAMP = 2600ms
    in.logo         = m_logo.IsOk() ? &m_logo : nullptr;
    in.version      = m_version;
    in.sub          = m_sub;
    in.status       = m_status;
    in.font_version = m_font_version;
    in.font_sub     = m_font_sub;
    in.font_status  = m_font_status;

    draw_splash(gc, in);
}

} // namespace GUI
} // namespace Slic3r

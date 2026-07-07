// SplashPreviewPanel.cpp
#include "SplashPreviewPanel.hpp"
#include "SplashRenderers.hpp"
#include "SplashCompose.hpp"

#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>

#include <algorithm>

#include "../BitmapCache.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"

namespace Slic3r {
namespace GUI {

namespace {
inline wxColour bg_colour(SplashTheme theme)
{
    const SplashRGB& c = splash_theme_def(theme).bg;
    return wxColour(c.r, c.g, c.b);
}
} // anonymous namespace

SplashPreviewPanel::SplashPreviewPanel(wxWindow* parent, SplashTheme theme)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_timer(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // 4:3 preview keeping the 480x360 aspect of the real splash.
    const wxSize sz = FromDIP(wxSize(320, 240));
    SetMinSize(sz);
    SetSize(sz);

    const wxSize logo_px = FromDIP(wxSize(160, 160));
    BitmapCache cache;
    if (wxBitmap* bmp = cache.load_svg("splash_logo_dark", logo_px.GetWidth(), logo_px.GetHeight()))
        m_logo = *bmp;

    m_version = GUI_App::format_display_version();
    m_sub     = "CrashSlicer";
    m_status  = _L("Ready.");

    m_font_version = wxFontInfo(11).Bold();
    m_font_sub     = wxFontInfo(7);
    m_font_status  = wxFontInfo(8);

    Bind(wxEVT_PAINT, &SplashPreviewPanel::OnPaint, this);
    Bind(wxEVT_TIMER, &SplashPreviewPanel::OnTimer, this);

    m_watch.Start();
    m_timer.Start(33);  // ~30 FPS; the Preferences event loop services it
}

SplashPreviewPanel::~SplashPreviewPanel()
{
    if (m_timer.IsRunning())
        m_timer.Stop();
}

void SplashPreviewPanel::SetTheme(SplashTheme theme)
{
    if (!m_classic && theme == m_theme)
        return;
    m_classic = false;
    m_theme   = theme;
    Refresh(false);
}

void SplashPreviewPanel::SetClassic()
{
    if (m_classic)
        return;
    m_classic = true;
    Refresh(false);
}

void SplashPreviewPanel::OnTimer(wxTimerEvent& /*evt*/)
{
    Refresh(false);
}

void SplashPreviewPanel::OnPaint(wxPaintEvent& /*evt*/)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize cs = GetClientSize();

    if (m_classic) {
        // No animated art for the classic splash — show a neutral placeholder.
        dc.SetBackground(wxBrush(wxColour(20, 20, 24)));
        dc.Clear();
        dc.SetTextForeground(wxColour(150, 150, 155));
        const wxString msg = _L("Classic OrcaSlicer splash\n(no animated preview)");
        wxRect rc(0, 0, cs.GetWidth(), cs.GetHeight());
        dc.DrawLabel(msg, rc, wxALIGN_CENTER);
        return;
    }

    dc.SetBackground(wxBrush(bg_colour(m_theme)));
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc)
        return;

    if (cs.GetWidth() <= 0 || cs.GetHeight() <= 0) {
        delete gc;
        return;
    }
    gc->Scale(cs.GetWidth() / double(SPLASH_W), cs.GetHeight() / double(SPLASH_H));

    SplashComposeInput in;
    in.theme        = m_theme;
    in.el_ms        = m_watch.Time();
    in.charge       = std::min(1.0f, float(m_watch.Time()) / 2600.0f);
    in.logo         = m_logo.IsOk() ? &m_logo : nullptr;
    in.version      = m_version;
    in.sub          = m_sub;
    in.status       = m_status;
    in.font_version = m_font_version;
    in.font_sub     = m_font_sub;
    in.font_status  = m_font_status;

    draw_splash(gc, in);

    delete gc;
}

} // namespace GUI
} // namespace Slic3r

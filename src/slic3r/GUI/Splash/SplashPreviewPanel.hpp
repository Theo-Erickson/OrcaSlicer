#pragma once
// SplashPreviewPanel.hpp
//
// A small wxPanel that live-previews the animated splash for a chosen theme,
// using the exact same draw_splash() composition as the real SplashFrame.
// Used in Preferences so the user can see a theme before selecting it.
//
// Unlike the startup splash, the preview runs inside a normal running event
// loop, so its timer animates smoothly.

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/stopwatch.h>
#include <wx/bitmap.h>
#include <wx/font.h>
#include <wx/string.h>

#include "SplashThemes.hpp"

namespace Slic3r {
namespace GUI {

class SplashPreviewPanel : public wxPanel
{
public:
    explicit SplashPreviewPanel(wxWindow* parent, SplashTheme theme = SplashTheme::Circuit);
    ~SplashPreviewPanel() override;

    // Preview one of the animated themes.
    void SetTheme(SplashTheme theme);
    // Preview the "Default (classic)" choice — a simple placeholder, since the
    // legacy static splash has nothing to animate.
    void SetClassic();
    SplashTheme GetTheme() const { return m_theme; }

private:
    void OnPaint(wxPaintEvent& evt);
    void OnTimer(wxTimerEvent& evt);

    SplashTheme m_theme;
    bool        m_classic = false;
    wxTimer     m_timer;
    wxStopWatch m_watch;

    wxBitmap m_logo;
    wxString m_version;
    wxString m_sub;
    wxString m_status;

    wxFont m_font_version;
    wxFont m_font_sub;
    wxFont m_font_status;
};

} // namespace GUI
} // namespace Slic3r

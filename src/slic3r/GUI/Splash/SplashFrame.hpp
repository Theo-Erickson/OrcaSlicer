#pragma once
// SplashFrame.hpp
//
// Custom animated splash for CrashSlicer (Path A from the hand-off doc): a
// borderless, stay-on-top wxFrame that owns a ~30 FPS timer and redraws a
// double-buffered wxGraphicsContext each tick. Replaces the static wxSplashScreen
// visually while keeping the same show/SetText/Destroy lifecycle so GUI_App can
// drive it identically.
//
// PCH note: only light wx headers here; wxGraphicsContext is used in the .cpp.

#include <wx/frame.h>
#include <wx/timer.h>
#include <wx/stopwatch.h>
#include <wx/bitmap.h>
#include <wx/font.h>
#include <wx/string.h>

#include "SplashThemes.hpp"

namespace Slic3r {
namespace GUI {

class SplashFrame : public wxFrame
{
public:
    // pos: preferred top-left (display of the last main window); may be
    // wxDefaultPosition. The frame is then centered on that display.
    explicit SplashFrame(SplashTheme theme, const wxPoint& pos = wxDefaultPosition);
    ~SplashFrame() override;

    // Update the status/action line (same contract as the old SplashScreen).
    void SetText(const wxString& text);

private:
    void OnPaint(wxPaintEvent& evt);
    void OnTimer(wxTimerEvent& evt);
    void render(wxGraphicsContext* gc);

    SplashTheme m_theme;
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

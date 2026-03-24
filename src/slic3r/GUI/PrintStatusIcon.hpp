#pragma once
// PrintStatusIcon.hpp
// Animated SVG-based status icon widget for the OrcaSlicer top menu bar.
//
// Architecture overview:
//   - PrintStatusIcon is a wxPanel subclass that owns a wxWebView.
//   - The WebView renders the SVG (with CSS animations) in an offscreen-like
//     panel sized to the toolbar height.
//   - State changes swap the SVG source via ExecuteScriptAsync.
//   - A progress arc overlay (for RUNNING state) is drawn via injected JS.
//
// Threading: All wx calls must happen on the main thread.
//   Use CallAfter() when updating from worker threads or MQTT callbacks.

#include <wx/wx.h>
#include <wx/webview.h>
#include <wx/timer.h>
#include <string>
#include <unordered_map>
#include <functional>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// PrintState — mirrors gcode_state / mc_print_stage from DeviceManager.hpp
// ---------------------------------------------------------------------------
enum class PrintState {
    IDLE,
    SLICING,
    SLICED,
    SENDING,
    PREPARE,
    RUNNING,
    PAUSE,
    FILAMENT_CHANGE,
    CALIBRATING,
    FINISH,
    FAILED,
    OFFLINE,
};

// ---------------------------------------------------------------------------
// PrintStatusIcon
// ---------------------------------------------------------------------------
class PrintStatusIcon : public wxPanel
{
public:
    // icon_size: width == height in pixels (toolbar typically 24–32 px,
    //            but we render at 72 and let wxWebView scale down).
    explicit PrintStatusIcon(wxWindow* parent, int icon_size = 28);
    ~PrintStatusIcon() override = default;

    // Call from any thread — safely marshals to main thread.
    void SetState(PrintState state, int progress_pct = -1);

    // Optional: tooltip label, e.g. "Printing – 42% – 1h 03m remaining"
    void SetStatusLabel(const wxString& label);

    // Bind a callback for when the icon is clicked (open Printer tab).
    void BindClickHandler(std::function<void()> handler);

private:
    // WebView lifecycle
    void CreateWebView();
    void LoadState(PrintState state, int progress_pct);
    wxString BuildHtmlPage(const wxString& svg_content, int progress_pct) const;

    // SVG loading — returns raw SVG text for the given state.
    // Looks for files under resources/icons/print_status/ first;
    // falls back to embedded strings in PrintStatusIconSVG.hpp.
    static wxString LoadSVG(PrintState state);
    static wxString SVGFilename(PrintState state);

    // Inject a progress arc into the RUNNING SVG via JS.
    void UpdateProgressArc(int pct);

    // wx event handlers
    void OnWebViewLoaded(wxWebViewEvent& evt);
    void OnLeftClick(wxMouseEvent& evt);

    wxWebView*            m_webview   { nullptr };
    PrintState            m_state     { PrintState::OFFLINE };
    int                   m_progress  { -1 };
    int                   m_icon_size;
    std::function<void()> m_on_click;

    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

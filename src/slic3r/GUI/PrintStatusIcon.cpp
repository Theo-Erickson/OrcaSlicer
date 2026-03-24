// PrintStatusIcon.cpp
// See PrintStatusIcon.hpp for architecture notes.

#include "PrintStatusIcon.hpp"
#include "PrintStatusIconSVG.hpp"   // embedded SVG strings (generated)

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/base64.h>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(PrintStatusIcon, wxPanel)
    EVT_LEFT_UP(PrintStatusIcon::OnLeftClick)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
PrintStatusIcon::PrintStatusIcon(wxWindow* parent, int icon_size)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition,
              wxSize(icon_size, icon_size), wxBORDER_NONE)
    , m_icon_size(icon_size)
{
    SetBackgroundColour(parent->GetBackgroundColour());
    CreateWebView();
}

void PrintStatusIcon::CreateWebView()
{
    // wxWebView renders SVG+CSS animations without any extra dependencies.
    // The panel is transparent so it blends with the toolbar background.
    m_webview = wxWebView::New(
        this, wxID_ANY,
        wxEmptyString,
        wxPoint(0, 0),
        wxSize(m_icon_size, m_icon_size),
        wxWebViewBackendDefault,
        wxBORDER_NONE
    );

    if (!m_webview) {
        // Fallback: render a static wxBitmap instead (see notes below).
        wxLogWarning("PrintStatusIcon: wxWebView not available; "
                     "falling back to static icon.");
        return;
    }

    // Allow the WebView to be transparent so toolbar background shows through.
    m_webview->SetPage("<html><body></body></html>", "");
    m_webview->Bind(wxEVT_WEBVIEW_LOADED, &PrintStatusIcon::OnWebViewLoaded, this);

    // Load initial state.
    LoadState(m_state, m_progress);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void PrintStatusIcon::SetState(PrintState state, int progress_pct)
{
    // Always dispatch to the main thread — this may be called from the
    // MQTT worker thread inside DeviceManager::on_machine_data_update().
    CallAfter([this, state, progress_pct]() {
        if (m_state == state && m_progress == progress_pct)
            return;  // No-op — avoid redundant re-renders.
        m_state    = state;
        m_progress = progress_pct;
        LoadState(state, progress_pct);
    });
}

void PrintStatusIcon::SetStatusLabel(const wxString& label)
{
    SetToolTip(label);
}

void PrintStatusIcon::BindClickHandler(std::function<void()> handler)
{
    m_on_click = std::move(handler);
}

// ---------------------------------------------------------------------------
// Internal rendering
// ---------------------------------------------------------------------------
void PrintStatusIcon::LoadState(PrintState state, int progress_pct)
{
    if (!m_webview) return;

    wxString svg     = LoadSVG(state);
    wxString html    = BuildHtmlPage(svg, progress_pct);
    m_webview->SetPage(html, "about:blank");
}

wxString PrintStatusIcon::BuildHtmlPage(const wxString& svg_content,
                                         int            progress_pct) const
{
    // We set the background to "transparent" and match the system color.
    // The SVG is scaled to exactly fill the WebView viewport.
    wxString progress_js;
    if (progress_pct >= 0) {
        // Inject a thin progress arc over the icon (used in RUNNING state).
        // The arc is drawn in a <canvas> overlay sized to the SVG viewport.
        progress_js = wxString::Format(R"JS(
(function() {
    const pct = %d / 100.0;
    const c   = document.getElementById('progress-arc');
    if (!c) return;
    const ctx = c.getContext('2d');
    const cx  = c.width / 2, cy = c.height / 2, r = cx - 4;
    ctx.clearRect(0, 0, c.width, c.height);
    // Background track
    ctx.beginPath();
    ctx.arc(cx, cy, r, -Math.PI/2, Math.PI*1.5);
    ctx.strokeStyle = 'rgba(93,202,165,0.25)';
    ctx.lineWidth = 3;
    ctx.stroke();
    // Filled arc
    ctx.beginPath();
    ctx.arc(cx, cy, r, -Math.PI/2, -Math.PI/2 + Math.PI*2*pct);
    ctx.strokeStyle = '#1D9E75';
    ctx.lineWidth   = 3;
    ctx.lineCap     = 'round';
    ctx.stroke();
})();
        )JS", progress_pct);
    }

    return wxString::Format(R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8"/>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  html, body {
    width:  %dpx;
    height: %dpx;
    overflow: hidden;
    background: transparent;
  }
  .icon-wrap {
    position: relative;
    width:  %dpx;
    height: %dpx;
  }
  .icon-wrap svg {
    width:  100%%;
    height: 100%%;
    display: block;
  }
  #progress-arc {
    position: absolute;
    top: 0; left: 0;
    pointer-events: none;
  }
</style>
</head>
<body>
<div class="icon-wrap">
  %s
  <canvas id="progress-arc" width="%d" height="%d"></canvas>
</div>
<script>%s</script>
</body>
</html>
    )HTML",
        m_icon_size, m_icon_size,
        m_icon_size, m_icon_size,
        svg_content,
        m_icon_size, m_icon_size,
        progress_js
    );
}

// ---------------------------------------------------------------------------
// SVG loading — filesystem first, embedded strings as fallback
// ---------------------------------------------------------------------------
wxString PrintStatusIcon::SVGFilename(PrintState state)
{
    switch (state) {
        case PrintState::IDLE:            return "status_idle.svg";
        case PrintState::SLICING:         return "status_slicing.svg";
        case PrintState::SLICED:          return "status_sliced.svg";
        case PrintState::SENDING:         return "status_sending.svg";
        case PrintState::PREPARE:         return "status_prepare.svg";
        case PrintState::RUNNING:         return "status_running.svg";
        case PrintState::PAUSE:           return "status_pause.svg";
        case PrintState::FILAMENT_CHANGE: return "status_filament_change.svg";
        case PrintState::CALIBRATING:     return "status_calibrating.svg";
        case PrintState::FINISH:          return "status_finish.svg";
        case PrintState::FAILED:          return "status_failed.svg";
        case PrintState::OFFLINE:         return "status_offline.svg";
        default:                          return "status_idle.svg";
    }
}

wxString PrintStatusIcon::LoadSVG(PrintState state)
{
    // Try loading from the resources directory first so users can
    // swap in custom icons without recompiling.
    wxFileName path(wxStandardPaths::Get().GetResourcesDir(),
                    SVGFilename(state));
    path.AppendDir("icons");
    path.AppendDir("print_status");

    if (path.FileExists()) {
        wxFile f(path.GetFullPath());
        if (f.IsOpened()) {
            wxString content;
            f.ReadAll(&content);
            return content;
        }
    }

    // Fall back to embedded strings (see PrintStatusIconSVG.hpp).
    return PrintStatusIconSVG::Get(state);
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
void PrintStatusIcon::OnWebViewLoaded(wxWebViewEvent& /*evt*/)
{
    // Re-inject progress arc after page load (WebView cleared it).
    if (m_progress >= 0)
        UpdateProgressArc(m_progress);
}

void PrintStatusIcon::UpdateProgressArc(int pct)
{
    if (!m_webview) return;
    wxString js = wxString::Format(R"JS(
(function() {
    const pct = %d / 100.0;
    const c   = document.getElementById('progress-arc');
    if (!c) return;
    const ctx = c.getContext('2d');
    const cx  = c.width / 2, cy = c.height / 2, r = cx - 4;
    ctx.clearRect(0, 0, c.width, c.height);
    ctx.beginPath();
    ctx.arc(cx, cy, r, -Math.PI/2, Math.PI*1.5);
    ctx.strokeStyle = 'rgba(93,202,165,0.25)';
    ctx.lineWidth = 3;
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(cx, cy, r, -Math.PI/2, -Math.PI/2 + Math.PI*2*pct);
    ctx.strokeStyle = '#1D9E75';
    ctx.lineWidth   = 3;
    ctx.lineCap     = 'round';
    ctx.stroke();
})();
    )JS", pct);
    m_webview->RunScript(js);
}

void PrintStatusIcon::OnLeftClick(wxMouseEvent& evt)
{
    if (m_on_click)
        m_on_click();
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r

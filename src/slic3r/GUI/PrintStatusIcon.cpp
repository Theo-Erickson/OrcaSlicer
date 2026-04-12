// PrintStatusIcon.cpp  (v4)

#include "PrintStatusIcon.hpp"
#include "PrintStatusIconGIF.hpp"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/mstream.h>

namespace Slic3r {
namespace GUI {

wxBEGIN_EVENT_TABLE(PrintStatusIcon, wxPanel)
    EVT_LEFT_UP(PrintStatusIcon::OnClick)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
PrintStatusIcon::PrintStatusIcon(wxWindow* parent, int icon_size)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxTRANSPARENT_WINDOW)
    , m_icon_size(icon_size)
{
    // Set the background color of the whole icon and text widget.
    // Comment out for no color override (default = hidden)
    // SetBackgroundColour(parent->GetBackgroundColour());
    Build();
    Apply(PrintState::OFFLINE, -1);
}

void PrintStatusIcon::Build()
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);

    // ── Animated GIF ──────────────────────────────────────────────────────
    // wxAC_NO_AUTORESIZE: lock the control to m_icon_size.
    // The GIFs are generated at exactly m_icon_size × m_icon_size pixels,
    // so there is no clipping or scaling.
    m_anim = new wxAnimationCtrl(
        this, wxID_ANY, wxNullAnimation,
        wxDefaultPosition, wxSize(m_icon_size, m_icon_size),
        wxAC_DEFAULT_STYLE | wxAC_NO_AUTORESIZE | wxBORDER_NONE);
    m_anim->SetBackgroundColour(GetBackgroundColour());
    m_anim->Bind(wxEVT_LEFT_UP, &PrintStatusIcon::OnClick, this);
    row->Add(m_anim, 0, wxALIGN_CENTER_VERTICAL);

    // ── Debug label ───────────────────────────────────────────────────────
    row->AddSpacer(4);
    m_text = new wxStaticText(this, wxID_ANY, "FILAMENT SWAP",  // longest string sets initial width
                              wxDefaultPosition, wxDefaultSize);
    wxFont f = m_text->GetFont();
    f.SetPointSize(8);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_text->SetFont(f);

    // Lock the width to the longest label so it never shrinks or clips.
    // GetBestSize() measures "FILAMENT SWAP" at the font set above.
    // If you want to have the label resize dynamically according to string length, comment out the two lines below
    wxSize best = m_text->GetBestSize();
    m_text->SetMinSize(wxSize(best.GetWidth() + 4, best.GetHeight()));

    m_text->Bind(wxEVT_LEFT_UP, &PrintStatusIcon::OnClick, this);
    row->Add(m_text, 0, wxALIGN_CENTER_VERTICAL);

    SetSizerAndFit(row);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void PrintStatusIcon::SetState(PrintState state, int progress_pct)
{
    CallAfter([this, state, progress_pct]() {
        // Always apply — don't skip on equal state, because
        // progress_pct may change within the same state (e.g. SLICING 10→50).
        m_state    = state;
        m_progress = progress_pct;
        Apply(state, progress_pct);
    });
}

void PrintStatusIcon::SetStatusLabel(const wxString& label)
{
    CallAfter([this, label]() { SetToolTip(label); });
}

void PrintStatusIcon::BindClickHandler(std::function<void()> handler)
{
    m_on_click = std::move(handler);
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------
void PrintStatusIcon::Apply(PrintState state, int progress_pct)
{
    // ── GIF ──
    wxAnimation anim = LoadAnim(state);
    if (anim.IsOk()) {
        m_anim->SetAnimation(anim);
        m_anim->Play();
    }

    // ── Label text ──
    wxString txt = Label(state);
    if (progress_pct >= 0 &&
        (state == PrintState::SLICING ||
         state == PrintState::RUNNING ||
         state == PrintState::SENDING))
        txt = wxString::Format("%s %d%%", txt, progress_pct);

    // Change label for the text next to the animated icon
    m_text->SetLabel(txt);
    // Set the color of the text
    m_text->SetForegroundColour(LabelColour(state));
    // Set the backround for the text element (default hidden)
    // m_text->SetBackgroundColour(*wxBLACK); 
    // Refresh the text object
    m_text->Refresh();

    // Force the parent sizer to re-measure (label width may change).
    if (GetSizer()) 
    {
        GetSizer()->Layout();
    }
}

// ---------------------------------------------------------------------------
// GIF loading
// ---------------------------------------------------------------------------
wxString PrintStatusIcon::GifName(PrintState s)
{
    switch (s) {
    case PrintState::IDLE:            return "status_idle.gif";
    case PrintState::SLICING:         return "status_slicing.gif";
    case PrintState::SLICED:          return "status_sliced.gif";
    case PrintState::SENDING:         return "status_sending.gif";
    case PrintState::PREPARE:         return "status_prepare.gif";
    case PrintState::RUNNING:         return "status_running.gif";
    case PrintState::PAUSE:           return "status_pause.gif";
    case PrintState::FILAMENT_CHANGE: return "status_filament_change.gif";
    case PrintState::CALIBRATING:     return "status_calibrating.gif";
    case PrintState::FINISH:          return "status_finish.gif";
    case PrintState::FAILED:          return "status_failed.gif";
    case PrintState::OFFLINE:         return "status_offline.gif";
    default:                          return "status_idle.gif";
    }
}

wxAnimation PrintStatusIcon::LoadAnim(PrintState s)
{
    // 1. Filesystem (resources/icons/print_status/) — hot-swappable.
    wxFileName p(wxStandardPaths::Get().GetResourcesDir(), GifName(s));
    p.AppendDir("icons");
    p.AppendDir("print_status");
    if (p.FileExists()) {
        wxAnimation a;
        if (a.LoadFile(p.GetFullPath(), wxANIMATION_TYPE_GIF))
            return a;
    }
    // 2. Embedded bytes.
    size_t len = 0;
    const uint8_t* data = PrintStatusIconGIF::GetData(s, len);
    if (data && len) {
        wxMemoryInputStream stream(data, len);
        wxAnimation a;
        if (a.Load(stream, wxANIMATION_TYPE_GIF))
            return a;
    }
    return wxNullAnimation;
}

// ---------------------------------------------------------------------------
// Label helpers
// ---------------------------------------------------------------------------
wxString PrintStatusIcon::Label(PrintState s)
{
    switch (s) {
    case PrintState::IDLE:            return "IDLE";
    case PrintState::SLICING:         return "SLICING";
    case PrintState::SLICED:          return "SLICED";
    case PrintState::SENDING:         return "SENDING";
    case PrintState::PREPARE:         return "PREPARE";
    case PrintState::RUNNING:         return "PRINTING";
    case PrintState::PAUSE:           return "PAUSED";
    case PrintState::FILAMENT_CHANGE: return "FILAMENT SWAP";
    case PrintState::CALIBRATING:     return "CALIBRATING";
    case PrintState::FINISH:          return "DONE!";
    case PrintState::FAILED:          return "FAILED";
    case PrintState::OFFLINE:         return "OFFLINE";
    default:                          return "UNKNOWN";
    }
}

wxColour PrintStatusIcon::LabelColour(PrintState s)
{
    switch (s) {
    case PrintState::RUNNING:          return wxColour( 80, 200, 160);
    case PrintState::FINISH:           return wxColour( 80, 200, 160);
    case PrintState::FAILED:           return wxColour(240,  90,  90);
    case PrintState::PAUSE:            return wxColour(240, 170,  60);
    case PrintState::PREPARE:          return wxColour(240, 170,  60);
    case PrintState::SLICING:          return wxColour( 80, 200, 160);
    case PrintState::SLICED:           return wxColour(130, 200,  70);
    case PrintState::SENDING:          return wxColour( 80, 160, 240);
    case PrintState::FILAMENT_CHANGE:  return wxColour(175, 160, 240);
    default:                           return wxColour(180, 180, 180);
    }
}

void PrintStatusIcon::OnClick(wxMouseEvent& evt)
{
    if (m_on_click) m_on_click();
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r

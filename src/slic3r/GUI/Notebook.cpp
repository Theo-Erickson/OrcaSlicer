// Notebook.cpp
//
// HOW ANIMATION WORKS (timer-driven bitmap swap)
// ───────────────────────────────────────────────
// Each tab button is an OrcaSlicer Button widget.  Instead of overlaying a
// separate wxAnimationCtrl on top (which caused z-order, background, and
// mouse-event problems), we drive animation by calling
//     btn->SetBitmapLabel(frame_bitmap)
// on every timer tick.  This replaces the icon the Button paints itself —
// no extra windows, no hit-test interference, no background colour mismatch.
//
// A single wxTimer fires every 16 ms (~60 Hz).  On each tick it walks the
// active TabAnim list, advances frames whose per-frame delay has elapsed,
// and calls SetBitmapLabel only when the frame actually changes.
//
// Active = hovered OR selected.
// On deactivate we restore the original static SVG bitmap via btn->Rescale().
//
// Frame data lives in TabIconFrames.hpp (auto-generated RGBA arrays).

#include "Notebook.hpp"

#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <wx/menu.h>
#include <boost/log/trivial.hpp>

// Auto-generated: RGBA frame arrays + TabIconFrames_Get()
#include "TabIconFrames.hpp"

wxDEFINE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

// ─────────────────────────────────────────────────────────────────────────────
// TabAnimMode — persistence helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char* kAnimModeKey = "tab_icon_anim_mode";

TabAnimMode TabAnimMode_Load()
{
    auto& cfg = *Slic3r::GUI::wxGetApp().app_config;
    std::string val;
    if (cfg.get(kAnimModeKey, val).empty())
        return TabAnimMode::Always;   // default
    if (val == "active_only") return TabAnimMode::ActiveOnly;
    if (val == "hover_only")  return TabAnimMode::HoverOnly;
    if (val == "never")       return TabAnimMode::Never;
    return TabAnimMode::Always;
}

void TabAnimMode_Save(TabAnimMode mode)
{
    const char* val = "always";
    switch (mode) {
    case TabAnimMode::ActiveOnly: val = "active_only"; break;
    case TabAnimMode::HoverOnly:  val = "hover_only";  break;
    case TabAnimMode::Never:      val = "never";       break;
    default: break;
    }
    Slic3r::GUI::wxGetApp().app_config->set(kAnimModeKey, val);
}

const char* TabAnimMode_Label(TabAnimMode mode)
{
    switch (mode) {
    case TabAnimMode::Always:     return "Always animate (active + hover)";
    case TabAnimMode::ActiveOnly: return "Animate active tab only";
    case TabAnimMode::HoverOnly:  return "Animate on hover only";
    case TabAnimMode::Never:      return "Never animate (static icons)";
    }
    return "Always animate (active + hover)";
}

// ─────────────────────────────────────────────────────────────────────────────
// ButtonsListCtrl
// ─────────────────────────────────────────────────────────────────────────────

ButtonsListCtrl::ButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE | wxTAB_TRAVERSAL)
    , m_timer(this)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif

    wxColour default_btn_bg;
#ifdef __APPLE__
    default_btn_bg = wxColour("#3B4446");
#else
    default_btn_bg = wxColour("#2D2D30");
#endif
    SetBackgroundColour(default_btn_bg);

    int em     = em_unit(this);
    m_btn_margin  = 0;
    m_line_margin = std::lround(0.1 * em);

    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(m_sizer);

    m_buttons_sizer = new wxFlexGridSizer(1, m_btn_margin, m_btn_margin);
    m_sizer->Add(m_buttons_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM, m_btn_margin);

    if (side_tools) {
        m_sizer->AddStretchSpacer(1);
        for (size_t idx = 0; idx < side_tools->GetItemCount(); idx++) {
            if (wxWindow* w = side_tools->GetItem(idx)->GetWindow())
                w->Reparent(this);
        }
        m_sizer->Add(side_tools, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, m_btn_margin);
    }

    Bind(wxEVT_TIMER,        &ButtonsListCtrl::OnTimer,      this, m_timer.GetId());
    Bind(wxEVT_RIGHT_DOWN,   &ButtonsListCtrl::OnRightClick, this);

    // Load persisted animation mode
    m_anim_mode = TabAnimMode_Load();
}

ButtonsListCtrl::~ButtonsListCtrl()
{
    m_timer.Stop();
}

// ── Timer ─────────────────────────────────────────────────────────────────────

void ButtonsListCtrl::OnTimer(wxTimerEvent&)
{
    bool any_active = false;

    for (int n = 0; n < (int)m_anims.size(); n++) {
        TabAnim& anim = m_anims[n];
        if (!anim.active || !anim.IsLoaded()) continue;
        any_active = true;

        if (anim.Tick(kTimerMs)) {
            m_pageButtons[n]->SetIcon(anim.frames[anim.current]);
        }
    }

    if (!any_active)
        m_timer.Stop();
}

// ── Animation control ─────────────────────────────────────────────────────────

TabAnim ButtonsListCtrl::LoadTabAnim(const std::string& icon_name) const
{
    TabIconData data;
    if (!TabIconFrames_Get(icon_name, data))
        return {};

    TabAnim anim;
    anim.frames.reserve(data.nframes);
    anim.delays_ms.reserve(data.nframes);

    for (int i = 0; i < data.nframes; i++) {
        // Build wxImage from raw RGBA bytes
        wxImage img(data.w, data.h);
        img.InitAlpha();

        const uint8_t* src = data.frames[i];
        for (int y = 0; y < data.h; y++) {
            for (int x = 0; x < data.w; x++) {
                int off = (y * data.w + x) * 4;
                img.SetRGB(x, y, src[off], src[off+1], src[off+2]);
                img.SetAlpha(x, y, src[off+3]);
            }
        }

        anim.frames.emplace_back(img);
        anim.delays_ms.push_back(data.delays[i]);
    }

    return anim;
}

void ButtonsListCtrl::SetAnimActive(int n, bool active)
{
    if (n < 0 || n >= (int)m_anims.size()) return;
    TabAnim& anim = m_anims[n];

    if (!anim.IsLoaded()) return;

    // Respect the current animation mode
    if (active && m_anim_mode == TabAnimMode::Never)
        active = false;

    if (active) {
        anim.active = true;
        anim.Reset();
        m_pageButtons[n]->SetIcon(anim.frames[0]);
        if (!m_timer.IsRunning())
            m_timer.Start(kTimerMs);
    } else {
        anim.active = false;
        anim.Reset();
        m_pageButtons[n]->Rescale();
    }
}

// ── InsertPage ────────────────────────────────────────────────────────────────

bool ButtonsListCtrl::InsertPage(size_t n, const wxString& text, bool bSelect,
                                  const std::string& bmp_name,
                                  const std::string& inactive_bmp_name)
{
    Button* btn = new Button(this, text.empty() ? text : " " + text, bmp_name, wxNO_BORDER);
    btn->SetCornerRadius(0);

    int em = em_unit(this);
    btn->SetMinSize({(text.empty() ? 40 : 136) * em / 10, 36 * em / 10});

    StateColor bg_color(
        std::pair{wxColour(107,107,107), (int)StateColor::Hovered},
        std::pair{wxColour( 59, 68, 70), (int)StateColor::Normal});
    btn->SetBackgroundColor(bg_color);

    StateColor text_color(std::pair{wxColour(254,254,254), (int)StateColor::Normal});
    btn->SetTextColor(text_color);
    btn->SetInactiveIcon(inactive_bmp_name);
    btn->SetSelected(false);

    // Click
    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent&) {
        auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn);
        if (it != m_pageButtons.end()) {
            wxCommandEvent evt(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
            evt.SetId(static_cast<int>(it - m_pageButtons.begin()));
            wxPostEvent(GetParent(), evt);
        }
    });

    // Hover — start/stop animation, gated by mode
    btn->Bind(wxEVT_ENTER_WINDOW, [this, btn](wxMouseEvent& e) {
        e.Skip();
        // HoverOnly and Always both animate on hover
        if (m_anim_mode == TabAnimMode::Never ||
            m_anim_mode == TabAnimMode::ActiveOnly)
            return;
        auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn);
        if (it == m_pageButtons.end()) return;
        int idx = static_cast<int>(it - m_pageButtons.begin());
        if (m_hovered == idx) return;
        if (m_hovered >= 0 && m_hovered != m_selection)
            SetAnimActive(m_hovered, false);
        m_hovered = idx;
        SetAnimActive(idx, true);
    });

    btn->Bind(wxEVT_LEAVE_WINDOW, [this, btn](wxMouseEvent& e) {
        e.Skip();
        auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn);
        if (it == m_pageButtons.end()) return;
        int idx = static_cast<int>(it - m_pageButtons.begin());
        // Only deactivate hover anim if not the selected tab (which stays active)
        if (idx != m_selection)
            SetAnimActive(idx, false);
        m_hovered = -1;
    });

    // Right-click on individual button — forward to the ctrl handler
    btn->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent& e) {
        OnRightClick(e);
    });

    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);

    // Eagerly load the animation frames for this tab
    TabAnim anim = LoadTabAnim(bmp_name);

    m_pageButtons  .insert(m_pageButtons.begin()   + n, btn);
    m_pageIconNames.insert(m_pageIconNames.begin() + n, bmp_name);
    m_anims        .insert(m_anims.begin()         + n, std::move(anim));
    m_pageLabels   .insert(m_pageLabels.begin()    + n, text);

    m_buttons_sizer->Insert(n, new wxSizerItem(btn));
    m_buttons_sizer->SetCols(m_buttons_sizer->GetCols() + 1);
    m_sizer->Layout();

    if (bSelect)
        SetSelection(static_cast<int>(n));

    return true;
}

// ── RemovePage ────────────────────────────────────────────────────────────────

void ButtonsListCtrl::RemovePage(size_t n)
{
    SetAnimActive(static_cast<int>(n), false);

    Button* btn = m_pageButtons[n];
    m_pageButtons  .erase(m_pageButtons.begin()   + n);
    m_pageIconNames.erase(m_pageIconNames.begin() + n);
    m_anims        .erase(m_anims.begin()         + n);
    m_pageLabels   .erase(m_pageLabels.begin()    + n);

    m_buttons_sizer->Remove(static_cast<int>(n));
#if __WXOSX__
    RemoveChild(btn);
#else
    btn->Reparent(nullptr);
#endif
    btn->Destroy();
    m_sizer->Layout();
}

// ── SetSelection ──────────────────────────────────────────────────────────────

void ButtonsListCtrl::SetSelection(int sel)
{
    if (m_selection == sel) return;

    // Deactivate previous
    if (m_selection >= 0 && m_selection != m_hovered)
        SetAnimActive(m_selection, false);

    if (m_selection >= 0) {
        StateColor bg(std::pair{wxColour(107,107,107),(int)StateColor::Hovered},
                      std::pair{wxColour( 59, 68, 70),(int)StateColor::Normal});
        m_pageButtons[m_selection]->SetBackgroundColor(bg);
        StateColor tc(std::pair{wxColour(254,254,254),(int)StateColor::Normal});
        m_pageButtons[m_selection]->SetSelected(false);
        m_pageButtons[m_selection]->SetTextColor(tc);
    }

    m_selection = sel;

    // Activate new — respects mode (Never is handled inside SetAnimActive)
    // HoverOnly: don't auto-animate on selection, only on hover
    if (m_anim_mode != TabAnimMode::HoverOnly)
        SetAnimActive(sel, true);

    StateColor bg(std::pair{wxColour(0,150,136),(int)StateColor::Hovered},
                  std::pair{wxColour(0,150,136),(int)StateColor::Normal});
    m_pageButtons[sel]->SetBackgroundColor(bg);
    StateColor tc(std::pair{wxColour(254,254,254),(int)StateColor::Normal});
    m_pageButtons[sel]->SetSelected(true);
    m_pageButtons[sel]->SetTextColor(tc);

    Refresh();
}

// ── Rescale ───────────────────────────────────────────────────────────────────

void ButtonsListCtrl::Rescale()
{
    int em = em_unit(this);
    for (Button* btn : m_pageButtons)
        btn->SetMinSize({(btn->GetLabel().empty() ? 40 : 132) * em / 10, 36 * em / 10});
    m_sizer->Layout();
}

// ── Misc ──────────────────────────────────────────────────────────────────────

void ButtonsListCtrl::UpdateMode() {}

void ButtonsListCtrl::OnPaint(wxPaintEvent&) {}

bool ButtonsListCtrl::SetPageImage(size_t n, const std::string&) const
{
    return n < m_pageButtons.size();
}

void ButtonsListCtrl::SetPageText(size_t n, const wxString& strText)
{
    m_pageButtons[n]->SetLabel(strText);
    if (!strText.empty()) m_pageLabels[n] = strText;
}

void ButtonsListCtrl::SetCompact(size_t n, bool compact)
{
    int em = em_unit(this);
    m_pageButtons[n]->SetMinSize({(compact ? 40 : 136) * em / 10, 36 * em / 10});
    m_pageButtons[n]->SetLabel(compact ? "" : (" " + m_pageLabels[n]));
}

wxString ButtonsListCtrl::GetPageText(size_t n) const
{
    return m_pageButtons[n]->GetLabel();
}

// ── Animation mode helpers ─────────────────────────────────────────────────────

void ButtonsListCtrl::RefreshAnimMode()
{
    // Stop all animations first, restore static icons
    for (int n = 0; n < (int)m_anims.size(); n++)
        SetAnimActive(n, false);

    // Re-activate based on new mode
    if (m_anim_mode == TabAnimMode::Never)
        return;

    // Always / ActiveOnly: re-animate the selected tab
    if (m_anim_mode == TabAnimMode::Always ||
        m_anim_mode == TabAnimMode::ActiveOnly) {
        if (m_selection >= 0)
            SetAnimActive(m_selection, true);
    }
    // HoverOnly: nothing to start now (hover events will trigger when needed)
}

// ── Right-click context menu ───────────────────────────────────────────────────

// Menu IDs — local to this file
enum {
    ID_ANIM_ALWAYS = wxID_HIGHEST + 4200,
    ID_ANIM_ACTIVE_ONLY,
    ID_ANIM_HOVER_ONLY,
    ID_ANIM_NEVER,
};

void ButtonsListCtrl::OnRightClick(wxMouseEvent& /*evt*/)
{
    wxMenu menu;
    menu.SetTitle("Tab icon animation");

    auto add_item = [&](int id, const char* label, TabAnimMode mode) {
        wxMenuItem* item = menu.AppendRadioItem(id, label);
        item->Check(m_anim_mode == mode);
    };

    add_item(ID_ANIM_ALWAYS,      "Always (active + hover)", TabAnimMode::Always);
    add_item(ID_ANIM_ACTIVE_ONLY, "Active tab only",         TabAnimMode::ActiveOnly);
    add_item(ID_ANIM_HOVER_ONLY,  "Hover only",              TabAnimMode::HoverOnly);
    add_item(ID_ANIM_NEVER,       "Never (static icons)",    TabAnimMode::Never);

    Bind(wxEVT_MENU, &ButtonsListCtrl::OnContextMenuItem, this,
         ID_ANIM_ALWAYS, ID_ANIM_NEVER);
    PopupMenu(&menu);
}

void ButtonsListCtrl::OnContextMenuItem(wxCommandEvent& evt)
{
    TabAnimMode new_mode;
    switch (evt.GetId()) {
    case ID_ANIM_ALWAYS:      new_mode = TabAnimMode::Always;     break;
    case ID_ANIM_ACTIVE_ONLY: new_mode = TabAnimMode::ActiveOnly; break;
    case ID_ANIM_HOVER_ONLY:  new_mode = TabAnimMode::HoverOnly;  break;
    case ID_ANIM_NEVER:       new_mode = TabAnimMode::Never;      break;
    default: return;
    }

    if (new_mode == m_anim_mode) return;
    m_anim_mode = new_mode;
    TabAnimMode_Save(new_mode);   // persist to AppConfig
    RefreshAnimMode();
}

// ── Notebook::Init ────────────────────────────────────────────────────────────

void Notebook::Init()
{
    SetInternalBorder(0);
    m_showEffect = m_hideEffect = wxSHOW_EFFECT_NONE;
    m_showTimeout = m_hideTimeout = 0;
#ifndef __WXGTK__
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
}

// ── Compile-time check for ScalableBitmap API ────────────────────────────────
// If MakeScalable fails to compile, find ScalableBitmap in wxExtensions.hpp
// and use whichever of these alternatives matches your version:
//
//   Option A — if ScalableBitmap has a direct wxBitmap ctor:
//     ScalableBitmap sb(bmp);
//
//   Option B — if bmp() returns a reference (most common):
//     ScalableBitmap sb(parent, "", size); sb.bmp() = bmp;
//
//   Option C — if ScalableBitmap stores via sys_color_changed_action:
//     Just use btn->SetIcon(bmp) if Button exposes it, or:
//     wrap bmp in wxBitmapBundle: btn->SetBitmap(wxBitmapBundle::FromBitmap(bmp))
//     if Button inherits from wxBitmapButton.
//
// The safest fallback if none of the above work — store the wxImage instead
// of wxBitmap in TabAnim and use:
//     ScalableBitmap sb(parent, "", h); sb.bmp() = wxBitmap(img);
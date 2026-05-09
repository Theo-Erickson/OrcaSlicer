#ifndef slic3r_Notebook_hpp_
#define slic3r_Notebook_hpp_

#include <wx/bookctrl.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/bitmap.h>
#include <wx/menu.h>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TabAnimMode — controls when tab icon animations play.
// Persisted in AppConfig under key "tab_icon_anim_mode".
// ─────────────────────────────────────────────────────────────────────────────
enum class TabAnimMode {
    Always,     // animate whenever the tab is active OR hovered
    ActiveOnly, // animate only when the tab is the selected tab
    HoverOnly,  // animate only on mouse hover (not when selected/idle)
    Never,      // always show static icon (animations disabled)
};

// Read/write the mode from AppConfig.  Safe to call before GUI is fully up.
TabAnimMode TabAnimMode_Load();
void        TabAnimMode_Save(TabAnimMode mode);
const char* TabAnimMode_Label(TabAnimMode mode);

class ModeSizer;
class ScalableButton;
class Button;

wxDECLARE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

// ─────────────────────────────────────────────────────────────────────────────
// TabAnim — per-button animation state
//
// Holds the decoded wxBitmap frames for one tab icon and tracks which frame
// is currently displayed.  The ButtonsListCtrl drives all active TabAnims
// from a single shared wxTimer.
// ─────────────────────────────────────────────────────────────────────────────
struct TabAnim {
    std::vector<wxBitmap> frames;
    std::vector<int>      delays_ms;   // per-frame delay
    int                   current  = 0;
    int                   ms_accum = 0; // accumulated ms since last advance
    bool                  active   = false; // playing?

    bool IsLoaded() const { return !frames.empty(); }

    // Returns true if the frame index advanced (caller should redraw).
    bool Tick(int elapsed_ms) {
        if (!active || frames.empty()) return false;
        ms_accum += elapsed_ms;
        int delay = delays_ms.empty() ? 60 : delays_ms[current % (int)delays_ms.size()];
        if (ms_accum < delay) return false;
        ms_accum -= delay;
        current = (current + 1) % (int)frames.size();
        return true;
    }

    void Reset() { current = 0; ms_accum = 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// ButtonsListCtrl
// ─────────────────────────────────────────────────────────────────────────────
class ButtonsListCtrl : public wxControl
{
public:
    ButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools = nullptr);
    ~ButtonsListCtrl();

    void OnPaint(wxPaintEvent&);
    void SetSelection(int sel);
    void UpdateMode();
    void Rescale();
    bool InsertPage(size_t n, const wxString& text, bool bSelect = false,
                    const std::string& bmp_name = "",
                    const std::string& inactive_bmp_name = "");
    void RemovePage(size_t n);
    bool SetPageImage(size_t n, const std::string& bmp_name) const;
    void SetPageText(size_t n, const wxString& strText);
    void SetCompact(size_t n, bool compact);
    wxString GetPageText(size_t n) const;
    wxFlexGridSizer* GetBtnsSizer() { return m_buttons_sizer; }

    // Re-evaluate all running animations after a mode change.
    void RefreshAnimMode();
    
private:
    // Load TabAnim frames from embedded RGBA data for the given icon name.
    // Returns an empty TabAnim if no data is registered.
    TabAnim LoadTabAnim(const std::string& icon_name) const;

    // Start/stop animation for a button slot.
    void SetAnimActive(int n, bool active);

    // Shared timer callback — advances all active animations.
    void OnTimer(wxTimerEvent&);

    // Right-click context menu on the tab bar.
    void OnRightClick(wxMouseEvent& evt);
    void OnContextMenuItem(wxCommandEvent& evt);

    wxFlexGridSizer*          m_buttons_sizer;
    wxBoxSizer*               m_sizer;
    std::vector<Button*>      m_pageButtons;
    std::vector<std::string>  m_pageIconNames;
    std::vector<TabAnim>      m_anims;          // parallel to m_pageButtons
    std::vector<wxString>     m_pageLabels;

    int       m_selection  {-1};
    int       m_hovered    {-1};
    int       m_btn_margin;
    int       m_line_margin;

    wxTimer      m_timer;
    TabAnimMode  m_anim_mode { TabAnimMode::Always };
    static constexpr int kTimerMs = 16;
};

// ─────────────────────────────────────────────────────────────────────────────
// Notebook — unchanged public interface
// ─────────────────────────────────────────────────────────────────────────────
class Notebook : public wxBookCtrlBase
{
public:
    Notebook(wxWindow* parent,
             wxWindowID winid = wxID_ANY,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             wxBoxSizer* side_tools = nullptr,
             long style = 0)
    {
        Init();
        Create(parent, winid, pos, size, side_tools, style);
    }

    bool Create(wxWindow* parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                wxBoxSizer* side_tools = nullptr,
                long style = 0)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style | wxBK_TOP))
            return false;

        m_bookctrl = new ButtonsListCtrl(this, side_tools);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);
        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(1).Expand());
        wxSizerFlags flags;
        if (IsVertical()) flags.Expand();
        else              flags.CentreVertical();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, [this](wxCommandEvent& evt) {
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });
        this->Bind(wxEVT_NAVIGATION_KEY, &Notebook::OnNavigationKey, this);
        return true;
    }

    bool ShowNewPage(wxWindow* page) { return AddPage(page, wxString(), "", ""); }

    void SetEffects(wxShowEffect show, wxShowEffect hide)
        { m_showEffect = show; m_hideEffect = hide; }
    void SetEffect(wxShowEffect e) { SetEffects(e, e); }
    void SetEffectsTimeouts(unsigned st, unsigned ht)
        { m_showTimeout = st; m_hideTimeout = ht; }
    void SetEffectTimeout(unsigned t) { SetEffectsTimeouts(t, t); }

    bool AddPage(wxWindow* page, const wxString& text,
                 const std::string& bmp_name,
                 const std::string& inactive_bmp_name,
                 bool bSelect = false)
    {
        DoInvalidateBestSize();
        return InsertPage(GetPageCount(), page, text, bmp_name, inactive_bmp_name, bSelect);
    }

    virtual bool InsertPage(size_t n, wxWindow* page, const wxString& text,
                            bool bSelect = false, int imageId = NO_IMAGE) override
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect, imageId))
            return false;
        GetBtnsListCtrl()->InsertPage(n, text, bSelect);
        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();
        return true;
    }

    bool InsertPage(size_t n, wxWindow* page, const wxString& text,
                    const std::string& bmp_name = "",
                    const std::string& inactive_bmp_name = "",
                    bool bSelect = false)
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect))
            return false;
        GetBtnsListCtrl()->InsertPage(n, text, bSelect, bmp_name, inactive_bmp_name);
        if (bSelect) SetSelection(n);
        return true;
    }

    virtual int SetSelection(size_t n) override
    {
        int ret = DoSetSelection(n, SetSelection_SendEvent);
        if (GetSelection() != (int)n) return ret;
        GetBtnsListCtrl()->SetSelection(n);
        for (size_t i = 0; i < m_pages.size(); i++)
            if (i != n && GetPage(i) != GetPage(n))
                m_pages[i]->Hide();
        return ret;
    }

    virtual int ChangeSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    virtual bool SetPageText(size_t n, const wxString& s) override
    {
        wxCHECK_MSG(n < GetPageCount(), false, wxS("Invalid page"));
        GetBtnsListCtrl()->SetPageText(n, s);
        return true;
    }
    virtual wxString GetPageText(size_t n) const override
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetBtnsListCtrl()->GetPageText(n);
    }
    virtual bool SetPageImage(size_t, int) override { return false; }
    virtual int  GetPageImage(size_t) const override { return NO_IMAGE; }
    bool SetPageImage(size_t n, const std::string& bmp_name)
        { return GetBtnsListCtrl()->SetPageImage(n, bmp_name); }

    virtual void SetFocus() override
    {
        if (wxWindow* p = GetCurrentPage()) p->SetFocus();
    }

    ButtonsListCtrl* GetBtnsListCtrl() const
        { return static_cast<ButtonsListCtrl*>(m_bookctrl); }

    void UpdateMode() { GetBtnsListCtrl()->UpdateMode(); }
    void Rescale()    { GetBtnsListCtrl()->Rescale(); }

    void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            AdvanceSelection(event.GetDirection());
        } else {
            wxWindow* const parent = GetParent();
            const bool isFromParent = event.GetEventObject() == (wxObject*)parent;
            const bool isFromSelf   = event.GetEventObject() == (wxObject*)this;
            const bool isForward    = event.GetDirection();

            if (isFromSelf && !isForward) {
                event.SetCurrentFocus(this);
                parent->HandleWindowEvent(event);
            } else if (isFromParent || isFromSelf) {
                if (m_selection != wxNOT_FOUND && (!event.GetDirection() || isFromSelf)) {
                    event.SetEventObject(this);
                    wxWindow* page = m_pages[m_selection];
                    if (!page->HandleWindowEvent(event))
                        page->SetFocus();
                } else {
                    SetFocus();
                }
            } else {
                if (!isForward) SetFocus();
                else if (parent) {
                    event.SetCurrentFocus(this);
                    parent->HandleWindowEvent(event);
                }
            }
        }
    }

protected:
    virtual void UpdateSelectedPage(size_t) override {}
    virtual wxBookCtrlEvent* CreatePageChangingEvent() const override
        { return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING, GetId()); }
    virtual void MakeChangedEvent(wxBookCtrlEvent& e) override
        { e.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED); }
    virtual wxWindow* DoRemovePage(size_t page) override
    {
        wxWindow* win = wxBookCtrlBase::DoRemovePage(page);
        if (win) { GetBtnsListCtrl()->RemovePage(page); DoSetSelectionAfterRemoval(page); }
        return win;
    }
    virtual void DoSize() override
    {
        if (wxWindow* p = GetCurrentPage()) p->SetSize(GetPageRect());
    }
    virtual void DoShowPage(wxWindow* page, bool show) override
    {
        if (show) page->ShowWithEffect(m_showEffect, m_showTimeout);
        else      page->HideWithEffect(m_hideEffect, m_hideTimeout);
    }

private:
    void Init();
    wxShowEffect m_showEffect, m_hideEffect;
    unsigned     m_showTimeout, m_hideTimeout;
};

#endif // slic3r_Notebook_hpp_
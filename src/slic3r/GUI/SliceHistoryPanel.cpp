#include "SliceHistoryPanel.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/settings.h>

#include "Tab.hpp"

namespace Slic3r { namespace GUI {

SliceHistoryPanel::SliceHistoryPanel(wxWindow*            parent,
                                       Plater*              plater,
                                       SliceHistoryManager* mgr)
    : wxPopupTransientWindow(parent, wxBORDER_SIMPLE | wxPU_CONTAINS_CONTROLS)
    , m_plater(plater)
    , m_mgr(mgr)
{
    SetBackgroundColour(wxColour(40, 42, 52));

    auto* root = new wxBoxSizer(wxVERTICAL);

    // ── Title bar ──────────────────────────────────────────────────────
    auto* title = new wxStaticText(this, wxID_ANY, _L("Slice History"));
    wxFont f = title->GetFont();
    f.SetPointSize(f.GetPointSize() + 1);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(f);
    title->SetForegroundColour(wxColour(220, 220, 230));
    root->Add(title, 0, wxALL, 8);
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // ── Notebook ───────────────────────────────────────────────────────
    m_notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition,
                                wxSize(460, 320));
    m_notebook->SetBackgroundColour(wxColour(48, 50, 62));
    root->Add(m_notebook, 1, wxEXPAND | wxALL, 4);

    SetSizer(root);
    build_tabs();
    Fit();
}

void SliceHistoryPanel::popup_below(wxWindow* anchor)
{
    // Rebuild with latest config so diffs are fresh
    const DynamicPrintConfig* cfg = nullptr;
    if (wxGetApp().preset_bundle)
        cfg = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
    refresh(cfg);

    wxPoint pos = anchor->ClientToScreen(wxPoint(0, anchor->GetSize().y + 4));
    Position(pos, wxSize(0, 0));
    Popup();
}

void SliceHistoryPanel::refresh(const DynamicPrintConfig* current_config)
{
    m_last_config = current_config;
    m_notebook->DeleteAllPages();
    build_tabs();
    Layout();
    Fit();
}

void SliceHistoryPanel::build_tabs()
{
    if (m_mgr->count() == 0) {
        auto* pg  = new wxPanel(m_notebook, wxID_ANY);
        pg->SetBackgroundColour(wxColour(48, 50, 62));
        auto* lbl = new wxStaticText(pg, wxID_ANY,
            _L("No slices yet.\nSlice a model to start tracking history."),
            wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        lbl->SetForegroundColour(wxColour(160, 162, 180));
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->AddStretchSpacer();
        s->Add(lbl, 0, wxALIGN_CENTER | wxALL, 16);
        s->AddStretchSpacer();
        pg->SetSizer(s);
        m_notebook->AddPage(pg, _L("Empty"));
        return;
    }

    // Newest first
    size_t n = m_mgr->count();
    for (int i = (int)n - 1; i >= 0; --i) {
        const SliceSnapshot& snap = m_mgr->get((size_t)i);

        auto* pg = new wxPanel(m_notebook, wxID_ANY);
        pg->SetBackgroundColour(wxColour(48, 50, 62));
        auto* vs = new wxBoxSizer(wxVERTICAL);

        // ── Stats block ──────────────────────────────────────────────
        auto stat_str = wxString::Format(
            _L("Preset: %s   |   Objects: %d"),
            snap.preset_name, snap.object_count);
        auto* stat1 = new wxStaticText(pg, wxID_ANY, stat_str);
        stat1->SetForegroundColour(wxColour(130, 220, 160));
        vs->Add(stat1, 0, wxLEFT | wxTOP, 8);

        auto time_str = wxString::Format(
            _L("Time: %s   |   Filament: %.1fg / %.0fmm"),
            snap.print_time, snap.filament_g, snap.filament_mm);
        auto* stat2 = new wxStaticText(pg, wxID_ANY, time_str);
        stat2->SetForegroundColour(wxColour(130, 200, 220));
        vs->Add(stat2, 0, wxLEFT | wxBOTTOM, 8);

        vs->Add(new wxStaticLine(pg), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

        // ── Changed settings list ─────────────────────────────────────
        auto* diff_lbl = new wxStaticText(pg, wxID_ANY,
            _L("Settings changed since this snapshot:"));
        diff_lbl->SetForegroundColour(wxColour(190, 192, 210));
        vs->Add(diff_lbl, 0, wxLEFT | wxTOP, 8);

        auto* list = new wxListCtrl(pg, wxID_ANY,
            wxDefaultPosition, wxSize(-1, 180),
            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
        list->SetBackgroundColour(wxColour(36, 38, 48));
        list->SetTextColour(wxColour(220, 220, 230));
        list->AppendColumn(_L("Setting"),         wxLIST_FORMAT_LEFT, 170);
        list->AppendColumn(_L("Snapshot value"),  wxLIST_FORMAT_LEFT, 120);
        list->AppendColumn(_L("Current value"),   wxLIST_FORMAT_LEFT, 120);

        if (m_last_config) {
            auto diffs = m_mgr->diff((size_t)i, *m_last_config);
            if (diffs.empty()) {
                long row = list->InsertItem(0, _L("(no differences)"));
                list->SetItem(row, 1, wxEmptyString);
                list->SetItem(row, 2, wxEmptyString);
            } else {
                for (const auto& d : diffs) {
                    long row = list->InsertItem(
                        list->GetItemCount(),
                        wxString::FromUTF8(d.key));
                    list->SetItem(row, 1, wxString::FromUTF8(d.snap_value));
                    list->SetItem(row, 2, wxString::FromUTF8(d.curr_value));
                }
            }
        } else {
            long row = list->InsertItem(0, _L("(slice again to compare)"));
            list->SetItem(row, 1, wxEmptyString);
            list->SetItem(row, 2, wxEmptyString);
        }
        vs->Add(list, 1, wxEXPAND | wxALL, 6);

        // ── Restore button ────────────────────────────────────────────
        auto* btn = new wxButton(pg, wxID_ANY,
            _L("Restore These Settings"));
        btn->SetBackgroundColour(wxColour(60, 120, 190));
        btn->SetForegroundColour(*wxWHITE);
        // Store snapshot index via SetId (offset by 1000 to avoid wxID clashes)
        btn->SetId(1000 + i);
        btn->Bind(wxEVT_BUTTON, &SliceHistoryPanel::on_restore, this);
        vs->Add(btn, 0, wxALIGN_RIGHT | wxALL, 8);

        pg->SetSizer(vs);
        m_notebook->AddPage(pg, wxString::FromUTF8(snap.label));
    }
}

void SliceHistoryPanel::on_restore(wxCommandEvent& evt)
{
    int snap_idx = evt.GetId() - 1000;
    if (snap_idx < 0 || snap_idx >= (int)m_mgr->count()) return;

    const DynamicPrintConfig& snap_cfg = m_mgr->get((size_t)snap_idx).config;

    // Apply to the global print preset config
    DynamicPrintConfig& curr =
        wxGetApp().preset_bundle->prints.get_edited_preset().config;
    curr.apply_only(snap_cfg, snap_cfg.keys(), /*ignore_nonexistent=*/true);

    // Propagate change into the UI
    wxGetApp().get_tab(Preset::TYPE_PRINT)->load_current_preset();
    m_plater->on_config_change(wxGetApp().preset_bundle->full_config());
    m_plater->set_plater_dirty(true);

    Dismiss();
}

}} // namespace Slic3r::GUI
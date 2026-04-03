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

    // ── Title bar with Clear button ────────────────────────────────────
    auto* title_bar = new wxPanel(this, wxID_ANY);
    title_bar->SetBackgroundColour(wxColour(32, 34, 44));
    auto* title_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* title = new wxStaticText(title_bar, wxID_ANY, _L("Slice"));
    wxFont f = title->GetFont();
    f.SetPointSize(f.GetPointSize() + 1);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(f);
    title->SetForegroundColour(wxColour(220, 220, 230));
    title_sizer->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);

    // Clear button — eraser Unicode character as icon
    m_clear_btn = new wxButton(title_bar, wxID_ANY,
        wxString::FromUTF8("\xE2\x8C\xAB") + " " + _L("Clear"),  // ⌫ symbol
        wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_clear_btn->SetBackgroundColour(wxColour(70, 35, 35));
    m_clear_btn->SetForegroundColour(wxColour(220, 120, 120));
    m_clear_btn->SetToolTip(_L("Clear all slice history"));
    m_clear_btn->Bind(wxEVT_BUTTON, &SliceHistoryPanel::on_clear_history, this);
    title_sizer->Add(m_clear_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM, 6);

    title_bar->SetSizer(title_sizer);
    root->Add(title_bar, 0, wxEXPAND);
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 0);

    // ── Notebook ───────────────────────────────────────────────────────
    m_notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition,
                                wxSize(460, 360));
    m_notebook->SetBackgroundColour(wxColour(48, 50, 62));
    root->Add(m_notebook, 1, wxEXPAND | wxALL, 4);

    SetSizer(root);
    build_tabs();
    Fit();
}

void SliceHistoryPanel::popup_below(wxWindow* anchor)
{
    if  (anchor) {
        m_anchor_btn = anchor;
    }
    
    // Rebuild with latest config so diffs are fresh
    const DynamicPrintConfig* cfg = nullptr;
    if (wxGetApp().preset_bundle)
        cfg = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
    refresh(cfg);

    // Position to the left of the button so it doesn't clip off the right edge
    wxPoint screen_pos = anchor->ClientToScreen(wxPoint(0, 0));
    wxSize  panel_size = GetBestSize();
    wxPoint pos(
        screen_pos.x + anchor->GetSize().x - panel_size.x,  // right-align to button
        screen_pos.y + anchor->GetSize().y + 4               // just below button
    );
    
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
    // Update clear button visibility
    if (m_clear_btn)
        m_clear_btn->Show(m_mgr->count() > 0);

    if (m_mgr->count() == 0) {
        auto* pg = new wxScrolledWindow(m_notebook, wxID_ANY);
        pg->SetScrollRate(0, 5);
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
    
    
    if (m_mgr->count() == 0) {
        auto* pg = new wxScrolledWindow(m_notebook, wxID_ANY);
        pg->SetScrollRate(0, 5);  // vertical scroll only
        
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

        auto* pg = new wxScrolledWindow(m_notebook, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        pg->SetBackgroundColour(wxColour(48, 50, 62));
        pg->SetScrollRate(0, 8);
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

        // ── Per-extruder filament breakdown ──────────────────────────────
        if (!snap.extruder_usages.empty()) {
            vs->Add(new wxStaticLine(pg), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

            auto* fil_lbl = new wxStaticText(pg, wxID_ANY, _L("Filament usage:"));
            fil_lbl->SetForegroundColour(wxColour(190, 192, 210));
            vs->Add(fil_lbl, 0, wxLEFT | wxTOP, 8);

            // Header row
            auto* hdr_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto make_hdr = [&](const wxString& txt, int width) {
                auto* t = new wxStaticText(pg, wxID_ANY, txt,
                    wxDefaultPosition, wxSize(width, -1));
                t->SetForegroundColour(wxColour(140, 142, 160));
                return t;
            };
            hdr_sizer->Add(make_hdr("", 18), 0);           // color swatch
            hdr_sizer->Add(make_hdr(_L("Extruder"), 70), 0);
            hdr_sizer->Add(make_hdr(_L("Model"),    60), 0);
            hdr_sizer->Add(make_hdr(_L("Support"),  60), 0);
            hdr_sizer->Add(make_hdr(_L("Flush"),    55), 0);
            hdr_sizer->Add(make_hdr(_L("Other"),    55), 0);
            hdr_sizer->Add(make_hdr(_L("Total"),    60), 0);
            vs->Add(hdr_sizer, 0, wxLEFT | wxRIGHT, 8);

            for (const auto& eu : snap.extruder_usages) {
                auto* row = new wxBoxSizer(wxHORIZONTAL);

                // Color swatch — small colored square
                auto* swatch = new wxPanel(pg, wxID_ANY,
                    wxDefaultPosition, wxSize(12, 12));
                // Parse hex color "#RRGGBB"
                wxColour swatch_color;
                swatch_color.Set(wxString::FromUTF8(eu.color_hex));
                swatch->SetBackgroundColour(swatch_color);
                row->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

                auto make_cell = [&](const wxString& txt, int width) {
                    auto* t = new wxStaticText(pg, wxID_ANY, txt,
                        wxDefaultPosition, wxSize(width, -1));
                    t->SetForegroundColour(wxColour(210, 212, 230));
                    return t;
                };

                row->Add(make_cell(
                    wxString::Format("#%d %s",
                        eu.extruder_id + 1,
                        wxString::FromUTF8(eu.material_name).substr(0, 8)),
                    70), 0);
                row->Add(make_cell(wxString::Format("%.1fg", eu.model_g),   60), 0);
                row->Add(make_cell(wxString::Format("%.1fg", eu.support_g), 60), 0);
                row->Add(make_cell(wxString::Format("%.1fg", eu.flush_g),   55), 0);
                row->Add(make_cell(wxString::Format("%.1fg", eu.other_g),   55), 0);
                row->Add(make_cell(wxString::Format("%.1fg", eu.total_g),   60), 0);

                vs->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
            }

            // Cross-extruder totals row
            vs->Add(new wxStaticLine(pg), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
            wxString tot_str = wxString::Format(
                _L("Totals  —  Model: %.1fg   Support: %.1fg   Flush: %.1fg   Other: %.1fg"),
                snap.total_model_g, snap.total_support_g,
                snap.total_flush_g, snap.total_other_g);
            auto* tot_lbl = new wxStaticText(pg, wxID_ANY, tot_str);
            tot_lbl->SetForegroundColour(wxColour(220, 200, 130));
            vs->Add(tot_lbl, 0, wxLEFT | wxTOP | wxBOTTOM, 8);
        }
        
        vs->Add(new wxStaticLine(pg), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

        // ── Changed settings list Top Row ─────────────────────────────────────
        auto* diff_row = new wxBoxSizer(wxHORIZONTAL);
        
        // ── Changed settings label ─────────────────────────────────────
        auto* diff_lbl = new wxStaticText(pg, wxID_ANY,
            _L("Settings changed since this snapshot:"));
        diff_lbl->SetForegroundColour(wxColour(190, 192, 210));
        
        // ── Restore button ────────────────────────────────────────────
        auto* restore_btn = new wxButton(pg, wxID_ANY,
            _L("Restore These Settings"));
        restore_btn->SetBackgroundColour(wxColour(60, 120, 190));
        restore_btn->SetForegroundColour(*wxWHITE);
        // Store snapshot index via SetId (offset by 1000 to avoid wxID clashes)
        restore_btn->SetId(1000 + i);
        restore_btn->Bind(wxEVT_BUTTON, &SliceHistoryPanel::on_restore, this);
        
        diff_row->Add(diff_lbl, 1, wxALIGN_CENTER_VERTICAL);
        diff_row->Add(restore_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        vs->Add(diff_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
        

        // ── Changed settings list Body ─────────────────────────────────────
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

        pg->SetSizer(vs);
        
        pg->FitInside();   // tells wxScrolledWindow to recalculate scroll range

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

void SliceHistoryPanel::on_clear_history(wxCommandEvent&)
{
    wxMessageDialog dlg(
        this,
        _L("Clear all slice history snapshots?"),
        _L("Clear History"),
        wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);

    if (dlg.ShowModal() != wxID_YES)
        return;

    m_mgr->clear();
    refresh(nullptr);  // or pass current config if you have it
}

void SliceHistoryPanel::OnDismiss()
{
    if (m_anchor_btn)
        m_anchor_btn->Show(true);
    
    wxPopupTransientWindow::OnDismiss();
}

}} // namespace Slic3r::GUI
// VCBackupPanel.cpp
// Drop into: src/slic3r/GUI/
// Add to src/slic3r/CMakeLists.txt under SLIC3R_GUI_SOURCES.

#include "VCBackupPanel.hpp"

#include <wx/msgdlg.h>
#include <wx/filename.h>
#include <wx/statline.h>

#include "GUI_App.hpp" // wxGetApp()
#include "Plater.hpp"  // wxGetApp().plater()->load_project()
#include "I18N.hpp"    // _L()

namespace Slic3r { namespace GUI {

// ============================================================
//  Construction
// ============================================================

VCBackupPanel::VCBackupPanel(wxWindow* parent, ProjectVCBackupManager* manager, wxWindowID id) : wxPanel(parent, id), m_manager(manager)
{
    build_ui();
    // Start hidden; shown when a project row is selected
    Show(false);
}

// ============================================================
//  UI construction
// ============================================================

wxButton* VCBackupPanel::make_button(wxWindow* parent, const wxString& label, BtnStyle style)
{
    auto* btn = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxSize(-1, 28));
    btn->SetWindowStyle(wxBORDER_NONE);

    wxColour bg, fg;
    switch (style) {
    case BtnStyle::Primary:
        bg = wxColour(26, 161, 121); // OrcaSlicer teal accent
        fg = wxColour(255, 255, 255);
        break;
    case BtnStyle::Danger:
        bg = wxColour(61, 26, 26);    // dark red surface
        fg = wxColour(240, 149, 149); // soft red text
        break;
    case BtnStyle::Secondary:
    default:
        bg = wxColour(55, 55, 55);    // dark neutral surface
        fg = wxColour(210, 210, 210); // light text
        break;
    }

    btn->SetBackgroundColour(bg);
    btn->SetForegroundColour(fg);
    btn->SetOwnBackgroundColour(bg);
    btn->SetOwnForegroundColour(fg);
    btn->Refresh();

    return btn;
}

void VCBackupPanel::build_ui()
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
  
    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ── Divider + header ─────────────────────────────────────
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxTOP, 6);

    auto* header_row = new wxBoxSizer(wxHORIZONTAL);
    m_header_label   = new wxStaticText(this, wxID_ANY, _L("VCBackups"));
    wxFont f         = m_header_label->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_header_label->SetFont(f);
    m_header_label->SetForegroundColour(wxColour(200, 200, 200));
    header_row->Add(m_header_label, 1, wxALIGN_CENTER_VERTICAL);
    outer->Add(header_row, 0, wxEXPAND | wxALL, 8);

    // ── Empty-state label (shown when no VCBackups yet) ──────
    m_empty_label = new wxStaticText(this, wxID_ANY, _L("No VCBackups yet. Save the project to create one."));
    m_empty_label->SetForegroundColour(wxColour(130, 130, 130));
    outer->Add(m_empty_label, 0, wxLEFT | wxBOTTOM, 10);

    // ── VCBackup list ────────────────────────────────────────
    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 140), wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_list->SetBackgroundColour(wxColour(35, 35, 35));
    m_list->SetForegroundColour(wxColour(200, 200, 200));
    m_list->InsertColumn(0, _L("Saved"), wxLIST_FORMAT_LEFT, 160);
    m_list->InsertColumn(1, _L("Age"), wxLIST_FORMAT_LEFT, 110);
    m_list->InsertColumn(2, _L("VCBackup file"), wxLIST_FORMAT_LEFT, 280);
    outer->Add(m_list, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // ── Button row ───────────────────────────────────────────
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);

    m_btn_restore    = make_button(this,_L("Restore this VCBackup"), BtnStyle::Primary);
    m_btn_delete     = make_button(this, _L("Delete VCBackup"), BtnStyle::Secondary);
    m_btn_delete_all = make_button(this, _L("Delete all VCBackups"), BtnStyle::Danger);
    
    btn_row->Add(m_btn_restore, 0, wxRIGHT, 6);
    btn_row->Add(m_btn_delete, 0, wxRIGHT, 6);
    btn_row->AddStretchSpacer();
    btn_row->Add(m_btn_delete_all, 0);

    outer->Add(btn_row, 0, wxEXPAND | wxALL, 8);
    SetSizer(outer);

    // ── Events ───────────────────────────────────────────────
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &VCBackupPanel::on_list_select, this);
    m_btn_restore->Bind(wxEVT_BUTTON, &VCBackupPanel::on_restore, this);
    m_btn_delete->Bind(wxEVT_BUTTON, &VCBackupPanel::on_delete_VCBackup, this);
    m_btn_delete_all->Bind(wxEVT_BUTTON, &VCBackupPanel::on_delete_all, this);
}

// ============================================================
//  Public API
// ============================================================

void VCBackupPanel::load_project(const std::string& project_path)
{
    m_project_path = project_path;

    if (project_path.empty()) {
        Show(false);
        return;
    }

    // Update header
    wxFileName fn(wxString::FromUTF8(project_path));
    m_header_label->SetLabel(_L("VCBackups") + " — " + fn.GetFullName());

    refresh();
    Show(true);

    // Force the parent to re-layout
    if (GetParent())
        GetParent()->Layout();
}

void VCBackupPanel::refresh()
{
    if (m_project_path.empty() || !m_manager)
        return;

    m_VCBackups = m_manager->list_VCBackups(m_project_path);
    populate_list();
}

// ============================================================
//  Private
// ============================================================

void VCBackupPanel::populate_list()
{
    m_list->DeleteAllItems();

    if (m_VCBackups.empty()) {
        m_empty_label->Show(true);
        m_list->Show(false);
        m_btn_restore->Enable(false);
        m_btn_delete->Enable(false);
        return;
    }

    m_empty_label->Show(false);
    m_list->Show(true);

    for (long i = 0; i < static_cast<long>(m_VCBackups.size()); ++i) {
        const auto& snap = m_VCBackups[i];
        long        idx  = m_list->InsertItem(i, wxString::FromUTF8(snap.date_string()));
        m_list->SetItem(idx, 1, wxString::FromUTF8(snap.relative_age()));

        // Show just the filename in the list, not the full path
        wxFileName fn(wxString::FromUTF8(snap.VCBackup_path));
        m_list->SetItem(idx, 2, fn.GetFullName());
    }

    // Nothing selected yet — disable restore/delete until user picks one
    m_btn_restore->Enable(false);
    m_btn_delete->Enable(false);

    Layout();
}

void VCBackupPanel::on_list_select(wxListEvent& evt)
{
    long idx   = evt.GetIndex();
    bool valid = (idx >= 0 && static_cast<size_t>(idx) < m_VCBackups.size());
    m_btn_restore->Enable(valid);
    m_btn_delete->Enable(valid);
}

void VCBackupPanel::on_restore(wxCommandEvent& /*evt*/)
{
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_VCBackups.size())
        return;

    const VCBackup& snap = m_VCBackups[sel];

    int confirm = wxMessageBox(wxString::Format(_L("Restore VCBackup from %s?\n\n"
                                                   "The current project file will be backed up as .bak before overwriting."),
                                                wxString::FromUTF8(snap.date_string())),
                               _L("Restore VCBackup"), wxYES_NO | wxICON_QUESTION, this);

    if (confirm != wxYES)
        return;

    bool ok = m_manager->restore(snap.VCBackup_path, m_project_path);
    if (!ok) {
        wxMessageBox(_L("Restore failed. Check that the VCBackup file still exists."), _L("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Reload the restored project in the Plater
    wxGetApp().plater()->load_project(wxString::FromUTF8(m_project_path));
}

void VCBackupPanel::on_delete_VCBackup(wxCommandEvent& /*evt*/)
{
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_VCBackups.size())
        return;

    const VCBackup& VCBack = m_VCBackups[sel];

    int confirm = wxMessageBox(wxString::Format(_L("Delete VCBackup from %s?"), wxString::FromUTF8(VCBack.date_string())),
                               _L("Delete VCBackup"), wxYES_NO | wxICON_QUESTION, this);

    if (confirm != wxYES)
        return;

    m_manager->delete_VCBackup(VCBack.VCBackup_path);
    refresh();
}

void VCBackupPanel::on_delete_all(wxCommandEvent& /*evt*/)
{
    if (m_VCBackups.empty())
        return;

    wxFileName fn(wxString::FromUTF8(m_project_path));
    int        confirm = wxMessageBox(wxString::Format(_L("Delete all %zu VCBackups for \"%s\"?"), m_VCBackups.size(),
                                                       fn.GetFullName().ToUTF8().data()),
                                      _L("Delete all VCBackups"), wxYES_NO | wxICON_WARNING, this);

    if (confirm != wxYES)
        return;

    m_manager->delete_all_VCBackups(m_project_path);
    refresh();
}

}} // namespace Slic3r::GUI
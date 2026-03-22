// VCBackupSettingsPanel.cpp
// Drop into: src/slic3r/GUI/

#include "VCBackupSettingsPanel.hpp"

#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/utils.h>
#include <wx/filename.h>
#include <filesystem>

#include "I18N.hpp"

namespace Slic3r {
namespace GUI {

namespace fs = std::filesystem;

// ============================================================
//  Construction
// ============================================================

VCBackupSettingsPanel::VCBackupSettingsPanel(wxWindow* parent,
                                             ProjectVCBackupManager* manager,
                                             wxWindowID id)
    : wxPanel(parent, id)
    , m_manager(manager)
{
    build_ui();
}

void VCBackupSettingsPanel::apply_button_style(wxButton* btn,
                                               const wxColour& bg,
                                               const wxColour& fg)
{
    btn->SetBackgroundColour(bg);
    btn->SetForegroundColour(fg);
    btn->SetOwnBackgroundColour(bg);
    btn->SetOwnForegroundColour(fg);
    btn->SetWindowStyle(wxBORDER_NONE);
    btn->SetMinSize(wxSize(-1, 28));
    btn->Refresh();
}

void VCBackupSettingsPanel::build_ui()
{
    SetBackgroundColour(wxColour(38, 38, 38));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 6);

    // Section heading
    auto* heading = new wxStaticText(this, wxID_ANY, _L("Backup storage location"));
    wxFont f = heading->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    heading->SetFont(f);
    heading->SetForegroundColour(wxColour(180, 180, 180));
    outer->Add(heading, 0, wxLEFT | wxBOTTOM, 8);

    // Path display row
    auto* path_row = new wxBoxSizer(wxHORIZONTAL);

    m_path_display = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxSize(-1, 28),
                                     wxTE_READONLY | wxBORDER_SIMPLE);
    m_path_display->SetBackgroundColour(wxColour(30, 30, 30));
    m_path_display->SetForegroundColour(wxColour(200, 200, 200));

    m_btn_browse = new wxButton(this, wxID_ANY, _L("Browse\u2026"));
    m_btn_open   = new wxButton(this, wxID_ANY, _L("Open folder"));
    m_btn_reset  = new wxButton(this, wxID_ANY, _L("Reset to default"));

    apply_button_style(m_btn_browse, wxColour(26, 161, 121), wxColour(255, 255, 255));
    apply_button_style(m_btn_open,   wxColour(55, 55, 55),   wxColour(210, 210, 210));
    apply_button_style(m_btn_reset,  wxColour(55, 55, 55),   wxColour(210, 210, 210));

    path_row->Add(m_path_display, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    path_row->Add(m_btn_browse,   0, wxRIGHT, 4);
    path_row->Add(m_btn_open,     0, wxRIGHT, 4);
    path_row->Add(m_btn_reset,    0);
    outer->Add(path_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // Hint text
    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("Backups are stored in per-project subfolders. "
           "Changing the folder will offer to move existing backups."));
    hint->SetForegroundColour(wxColour(110, 110, 110));
    wxFont hf = hint->GetFont();
    hf.SetPointSize(hf.GetPointSize() - 1);
    hint->SetFont(hf);
    outer->Add(hint, 0, wxLEFT | wxBOTTOM, 8);

    SetSizer(outer);
    refresh_display();

    m_btn_browse->Bind(wxEVT_BUTTON, &VCBackupSettingsPanel::on_browse,      this);
    m_btn_reset ->Bind(wxEVT_BUTTON, &VCBackupSettingsPanel::on_reset,       this);
    m_btn_open  ->Bind(wxEVT_BUTTON, &VCBackupSettingsPanel::on_open_folder, this);
}

// ============================================================
//  Public
// ============================================================

void VCBackupSettingsPanel::refresh_display()
{
    if (m_manager)
        m_path_display->SetValue(wxString::FromUTF8(m_manager->VCBackup_dir()));
}

// ============================================================
//  Migration prompt
// ============================================================

bool VCBackupSettingsPanel::prompt_migration(const std::string& new_dir)
{
    if (!m_manager) return false;

    // Count how many backup files currently exist
    // We reach into the filesystem via the manager's current dir
    std::string old_dir = m_manager->VCBackup_dir();
    if (old_dir == new_dir) return false;

    // Check if old dir has any files worth migrating
    size_t file_count = 0;
    {
        if (fs::exists(old_dir)) {
            for (const auto& e : fs::recursive_directory_iterator(old_dir))
                if (e.is_regular_file() && e.path().extension() == ".3mf")
                    ++file_count;
        }
    }

    if (file_count == 0) {
        // Nothing to migrate — just switch silently
        m_manager->set_backup_dir(new_dir);
        refresh_display();
        return true;
    }

    // ── Migration dialog ──────────────────────────────────────────────────
    wxString msg = wxString::Format(
        _L("You have %zu backup file(s) in the current folder:\n"
           "%s\n\n"
           "Would you like to move them to the new folder?\n"
           "%s\n\n"
           "Yes   \u2014 move files and switch folder\n"
           "No    \u2014 switch folder, leave old files where they are\n"
           "Cancel \u2014 keep the current folder"),
        file_count,
        wxString::FromUTF8(old_dir),
        wxString::FromUTF8(new_dir));

    int choice = wxMessageBox(msg,
                              _L("Move VC Backups?"),
                              wxYES_NO | wxCANCEL | wxICON_QUESTION,
                              this);

    if (choice == wxCANCEL)
        return false;   // Abort — do not change directory

    if (choice == wxNO) {
        // Switch dir without migrating
        m_manager->set_backup_dir(new_dir);
        refresh_display();
        return true;
    }

    // choice == wxYES — migrate with a progress dialog
    wxProgressDialog progress(
        _L("Moving VC Backups"),
        _L("Preparing to move files\u2026"),
        static_cast<int>(file_count),
        this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);

    MigrationResult result = m_manager->migrate_backups_to(
        new_dir,
        [&progress, file_count](size_t done, size_t total) {
            progress.Update(
                static_cast<int>(done),
                wxString::Format(_L("Moving file %zu of %zu\u2026"), done, total));
        });

    // Report outcome
    if (!result.error_message.empty()) {
        wxMessageBox(
            wxString::Format(_L("Migration error: %s\n\nNo files were moved."),
                             wxString::FromUTF8(result.error_message)),
            _L("Migration Failed"), wxOK | wxICON_ERROR, this);
        return false;
    }

    if (result.files_failed > 0) {
        wxMessageBox(
            wxString::Format(
                _L("Migration completed with errors.\n\n"
                   "Moved: %zu file(s)\n"
                   "Failed: %zu file(s)\n\n"
                   "Failed files remain in the old folder:\n%s"),
                result.files_moved,
                result.files_failed,
                wxString::FromUTF8(old_dir)),
            _L("Migration Partial"), wxOK | wxICON_WARNING, this);
    } else {
        wxMessageBox(
            wxString::Format(
                _L("Successfully moved %zu backup file(s) to:\n%s\n\n"
                   "The original files in the old folder have NOT been deleted. "
                   "You may remove the old folder manually if desired:\n%s"),
                result.files_moved,
                wxString::FromUTF8(new_dir),
                wxString::FromUTF8(old_dir)),
            _L("Migration Complete"), wxOK | wxICON_INFORMATION, this);
    }

    refresh_display();
    return true;
}

// ============================================================
//  Event handlers
// ============================================================

void VCBackupSettingsPanel::on_browse(wxCommandEvent&)
{
    if (!m_manager) return;

    wxString current = wxString::FromUTF8(m_manager->VCBackup_dir());
    wxDirDialog dlg(this,
                    _L("Choose VC Backup folder"),
                    current,
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

    if (dlg.ShowModal() != wxID_OK) return;

    std::string new_dir = dlg.GetPath().ToUTF8().data();
    if (new_dir == m_manager->VCBackup_dir()) return;

    prompt_migration(new_dir);
}

void VCBackupSettingsPanel::on_reset(wxCommandEvent&)
{
    if (!m_manager) return;

    std::string def = m_manager->default_VCBackup_dir();
    if (def == m_manager->VCBackup_dir()) {
        wxMessageBox(_L("Already using the default backup folder."),
                     _L("Reset"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    prompt_migration(def);
}

void VCBackupSettingsPanel::on_open_folder(wxCommandEvent&)
{
    if (!m_manager) return;
    wxLaunchDefaultApplication(wxString::FromUTF8(m_manager->VCBackup_dir()));
}

} // namespace GUI
} // namespace Slic3r
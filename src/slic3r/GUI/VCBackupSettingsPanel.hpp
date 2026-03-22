//
// A settings panel embedded at the bottom of the History tab's Projects page.
// Lets the user:
//   1. See the current backup directory
//   2. Browse for a new directory
//   3. Receive a prompt to migrate existing backups when the directory changes

#pragma once

#include <wx/wx.h>
#include <wx/textctrl.h>
#include <wx/statline.h>

#include "ProjectVCBackupManager.hpp"

namespace Slic3r {
namespace GUI {

class VCBackupSettingsPanel : public wxPanel
{
public:
    VCBackupSettingsPanel(wxWindow* parent,
                          ProjectVCBackupManager* manager,
                          wxWindowID id = wxID_ANY);
    ~VCBackupSettingsPanel() override = default;

    /// Refresh displayed path (call after manager's backup_dir changes).
    void refresh_display();

private:
    void build_ui();
    void apply_button_style(wxButton* btn, const wxColour& bg, const wxColour& fg);

    // Prompt the user with a yes/no/cancel migration dialog.
    // Returns true if the caller should proceed with the directory change.
    // Handles the migration internally if the user chooses "Yes".
    bool prompt_migration(const std::string& new_dir);

    void on_browse(wxCommandEvent&);
    void on_reset(wxCommandEvent&);
    void on_open_folder(wxCommandEvent&);

    ProjectVCBackupManager* m_manager{nullptr};

    wxTextCtrl* m_path_display{nullptr};
    wxButton*   m_btn_browse  {nullptr};
    wxButton*   m_btn_reset   {nullptr};
    wxButton*   m_btn_open    {nullptr};
};

} // namespace GUI
} // namespace Slic3r
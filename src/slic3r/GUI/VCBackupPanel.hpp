#pragma once
// VCBackupPanel.hpp
// Drop into: src/slic3r/GUI/
//
// A collapsible sub-panel shown inside HistoryPanel when the user clicks
// a project row. Displays the snapshot timeline for that project and
// provides Restore / Delete buttons.
//
// Usage inside HistoryPanel:
//
//   m_snapshot_panel = new VCBackupPanel(this, m_snapshot_manager);
//   outer_sizer->Add(m_snapshot_panel, 0, wxEXPAND | wxALL, 4);
//
//   // When the user selects a project row:
//   m_snapshot_panel->load_project(selected_project_path);

#pragma once

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/collpane.h>

#include "ProjectVCBackupManager.hpp"

#include <string>
#include <vector>
#include <memory>

namespace Slic3r { namespace GUI {

class VCBackupPanel : public wxPanel
{
public:
    VCBackupPanel(wxWindow* parent, ProjectVCBackupManager* manager, wxWindowID id = wxID_ANY);
    ~VCBackupPanel() override = default;

    /// Load and display snapshots for the given project path.
    /// Passing an empty string hides the panel.
    void load_project(const std::string& project_path);

    /// Called by HistoryPanel after a successful capture so we refresh
    /// without reloading from disk unnecessarily.
    void refresh();

private:
    void build_ui();
    void populate_list();

    wxButton* make_button(wxWindow* parent, const wxString& label, BtnStyle style);

    void on_restore(wxCommandEvent&);
    void on_delete_VCBackup(wxCommandEvent&);
    void on_delete_all(wxCommandEvent&);
    void on_list_select(wxListEvent&);

    ProjectVCBackupManager* m_manager{nullptr};
    std::string             m_project_path;
    std::vector<VCBackup>   m_VCBackups; // mirrors list, same order

    wxStaticText* m_header_label{nullptr};
    wxListCtrl*   m_list{nullptr};
    wxButton*     m_btn_restore{nullptr};
    wxButton*     m_btn_delete{nullptr};
    wxButton*     m_btn_delete_all{nullptr};
    wxStaticText* m_empty_label{nullptr};
};

}} // namespace Slic3r::GUI
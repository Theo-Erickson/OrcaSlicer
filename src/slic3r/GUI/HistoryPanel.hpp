// HistoryPanel.hpp  (v2 — replaces original)
// Drop into: src/slic3r/GUI/

#pragma once

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/srchctrl.h>
#include <wx/statline.h>
#include <wx/filename.h>

#include "ProjectVCBackupManager.hpp"
#include "VCBackupPanel.hpp"
#include "VCBackupSettingsPanel.hpp"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// HistoryEntry
// ---------------------------------------------------------------------------
struct HistoryEntry
{
    std::string path;
    std::string printer; // gcode only; empty for projects
    std::time_t timestamp{0};

    std::string filename() const;
    std::string date_string() const;
    std::string short_path(size_t n = 60) const;
};

// ---------------------------------------------------------------------------
// HistoryPanel
// ---------------------------------------------------------------------------
class HistoryPanel : public wxPanel
{
public:
    explicit HistoryPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~HistoryPanel() override = default;

    // ── Public API (call from Plater / GUI_App hooks) ──────────────────────

    /// Record that the user opened or saved a project file.
    /// Also automatically captures a VCBackup.
    void record_project(const std::string& path);

    /// Record that the user exported a G-code file for a given printer.
    void record_gcode(const std::string& path, const std::string& printer);

    /// Manually trigger a VCBackup capture (e.g. from a menu item).
    void capture_VCBackup(const std::string& path);

    /// Reload history from disk and repopulate both lists.
    void refresh();

    // ── Persistence ────────────────────────────────────────────────────────
    void load();
    void save() const;

    static constexpr size_t MAX_ENTRIES   = 50;
    static constexpr size_t MAX_SNAPSHOTS = 20;

private:
    // UI construction
    void build_ui();
    void build_project_page(wxNotebook* nb);
    void build_gcode_page(wxNotebook* nb);

    // List population (respects current search filter)
    void populate_project_list();
    void populate_gcode_list();

    // Shared button factory with explicit colour styling
    wxButton* make_button(wxWindow* parent, const wxString& label, HistoryBtnStyle style);

    std::string history_file_path() const;
    std::string data_dir() const;

    // ── Event handlers — project tab ───────────────────────────────────────
    void on_project_activated(wxListEvent&); // double-click → open project
    void on_project_selected(wxListEvent&);  // single-click → show VCBackups
    void on_capture_now(wxCommandEvent&);    // "Snapshot now" button
    void on_remove_project(wxCommandEvent&);
    void on_clear_projects(wxCommandEvent&);
    void on_open_folder_project(wxCommandEvent&);
    void on_search_changed(wxCommandEvent&); // shared for both tabs

    // ── Event handlers — gcode tab ─────────────────────────────────────────
    void on_gcode_selected(wxListEvent&);
    void on_open_in_plater(wxCommandEvent&); // "Open in Plater" button
    void on_gcode_activated(wxListEvent& evt);
    void on_remove_gcode(wxCommandEvent&);
    void on_clear_gcodes(wxCommandEvent&);
    void on_open_folder_gcode(wxCommandEvent&);

    // ── Members ────────────────────────────────────────────────────────────
    wxNotebook* m_notebook{nullptr};

    // Project sub-tab
    wxPanel*       m_project_page{nullptr};
    wxListCtrl*    m_project_list{nullptr};
    wxSearchCtrl*  m_search_projects{nullptr};
    wxButton*      m_btn_capture{nullptr};    // "Snapshot now"
    VCBackupPanel* m_VCBackup_panel{nullptr}; // collapsible timeline
    VCBackupSettingsPanel* m_settings_panel  {nullptr};
    
    // G-code sub-tab
    wxPanel*      m_gcode_page{nullptr};
    wxListCtrl*   m_gcode_list{nullptr};
    wxSearchCtrl* m_search_gcodes{nullptr};
    wxButton*     m_btn_open_plater{nullptr}; // "Open in Plater viewer"

    // Backend
    std::unique_ptr<ProjectVCBackupManager> m_VCBackup_manager;

    // History data
    std::vector<HistoryEntry> m_projects;
    std::vector<HistoryEntry> m_gcodes;

    // Filtered views (pointers into m_projects / m_gcodes)
    std::vector<const HistoryEntry*> m_filtered_projects;
    std::vector<const HistoryEntry*> m_filtered_gcodes;

    // How many backups per project are saved
    int MAX_VCBackups = 25;
};

}} // namespace Slic3r::GUI
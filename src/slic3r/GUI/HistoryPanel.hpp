#pragma once
// HistoryPanel.hpp
// Drop this file into: src/slic3r/GUI/
//
// A "History" tab panel for OrcaSlicer that tracks:
//   - Recent .3mf/.stl project files
//   - Recent .gcode exports, and which printer they were sliced for
//
// Storage: JSON file in the OrcaSlicer data directory
//   (same folder as OrcaSlicer.conf), named "history.json".
//
// Integration summary (see MainFrame changes at bottom of HistoryPanel.cpp):
//   1. Add  #include "HistoryPanel.hpp"  to MainFrame.cpp
//   2. Add  HistoryPanel* m_history_panel = nullptr;  to MainFrame.hpp private section
//   3. Add  tpHistory  to the MainFrame::TabPosition enum (after tpProject)
//   4. In MainFrame::create_preset_tabs(), construct and add the panel (see snippet below)
//   5. Call  HistoryPanel::record_project(path)  and  HistoryPanel::record_gcode(path, printer)
//      from the appropriate save / export hooks in Plater.cpp / GUI_App.cpp

#pragma once

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/srchctrl.h>
#include <wx/statline.h>
#include <wx/filename.h>

#include <string>
#include <vector>
#include <ctime>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

/// One entry in the project or gcode history list.
struct HistoryEntry
{
    std::string path;         ///< Full filesystem path
    std::string printer;      ///< Printer profile name (gcode only, empty for projects)
    std::time_t timestamp{0}; ///< Unix timestamp of when it was recorded

    /// Convenience: filename without directory
    std::string filename() const;
    /// Human-readable date string
    std::string date_string() const;
    /// Short display path (truncated if very long)
    std::string short_path(size_t max_chars = 60) const;
};

// ---------------------------------------------------------------------------
// HistoryPanel
// ---------------------------------------------------------------------------

/// wxPanel subclass that implements the History tab.
/// Add one instance to MainFrame::m_tabpanel and call record_*() as needed.
class HistoryPanel : public wxPanel
{
public:
    explicit HistoryPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~HistoryPanel() override = default;

    // ------------------------------------------------------------------
    // Public API – call these from Plater / GUI_App hooks
    // ------------------------------------------------------------------

    /// Record that the user saved / opened a project file (.3mf, .stl, …).
    /// Deduplicates by path and keeps the list capped at max_entries().
    void record_project(const std::string& path);

    /// Record that the user exported a G-code file, for a given printer.
    void record_gcode(const std::string& path, const std::string& printer);

    /// Reload data from disk and refresh both list views.
    void refresh();

    // ------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------

    /// Load history from the JSON file; called automatically in ctor.
    void load();
    /// Save history to the JSON file.
    void save() const;

    // ------------------------------------------------------------------
    // Settings
    // ------------------------------------------------------------------

    static constexpr size_t MAX_ENTRIES = 50; ///< Cap per list

private:
    // ------------------------------------------------------------------
    // UI helpers
    // ------------------------------------------------------------------
    void build_ui();
    void populate_project_list();
    void populate_gcode_list();

    wxString format_timestamp(std::time_t t) const;

    // ------------------------------------------------------------------
    // Event handlers
    // ------------------------------------------------------------------
    void on_project_activated(wxListEvent& evt);
    void on_gcode_activated(wxListEvent& evt);
    void on_clear_projects(wxCommandEvent&);
    void on_clear_gcodes(wxCommandEvent&);
    void on_remove_project(wxCommandEvent&);
    void on_remove_gcode(wxCommandEvent&);
    void on_search_changed(wxCommandEvent&);
    void on_open_folder_project(wxCommandEvent&);
    void on_open_folder_gcode(wxCommandEvent&);

    std::string history_file_path() const;

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    wxNotebook* m_notebook{nullptr};

    // Project sub-tab
    wxPanel*      m_project_page{nullptr};
    wxListCtrl*   m_project_list{nullptr};
    wxSearchCtrl* m_search_projects{nullptr};

    // G-code sub-tab
    wxPanel*      m_gcode_page{nullptr};
    wxListCtrl*   m_gcode_list{nullptr};
    wxSearchCtrl* m_search_gcodes{nullptr};

    std::vector<HistoryEntry> m_projects;
    std::vector<HistoryEntry> m_gcodes;

    // Filtered views (match current search text)
    std::vector<const HistoryEntry*> m_filtered_projects;
    std::vector<const HistoryEntry*> m_filtered_gcodes;
};

}} // namespace Slic3r::GUI
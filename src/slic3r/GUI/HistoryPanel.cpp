// HistoryPanel.cpp
// Drop this file into: src/slic3r/GUI/
// Add it to:  src/slic3r/CMakeLists.txt  under the GUI sources list.

#include "HistoryPanel.hpp"

#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/utils.h> // wxLaunchDefaultApplication / wxExecute
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/artprov.h>

#include <nlohmann/json.hpp> // already a dependency of OrcaSlicer (libslic3r uses it)

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

// OrcaSlicer internal helpers (already pulled in by most GUI files):
#include "GUI_App.hpp" // wxGetApp(), data_dir()
#include "I18N.hpp"    // _L()

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

// ============================================================
//  HistoryEntry helpers
// ============================================================

std::string HistoryEntry::filename() const
{
    wxFileName fn(wxString::FromUTF8(path));
    return fn.GetFullName().ToUTF8().data();
}

std::string HistoryEntry::date_string() const
{
    if (timestamp == 0)
        return "�";
    std::tm* tm_info = std::localtime(&timestamp);
    char     buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d  %H:%M", tm_info);
    return buf;
}

std::string HistoryEntry::short_path(size_t max_chars) const
{
    if (path.size() <= max_chars)
        return path;
    // Keep the tail (most informative part)
    return "�" + path.substr(path.size() - max_chars + 1);
}

// ============================================================
//  Construction
// ============================================================

HistoryPanel::HistoryPanel(wxWindow* parent, wxWindowID id) : wxPanel(parent, id)
{
    build_ui();
    load();
    populate_project_list();
    populate_gcode_list();
}

// ============================================================
//  UI construction
// ============================================================

void HistoryPanel::build_ui()
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);

    // ?? Header ????????????????????????????????????????????????
    auto*  header   = new wxStaticText(this, wxID_ANY, _L("Print History"));
    wxFont hdr_font = header->GetFont();
    hdr_font.SetPointSize(hdr_font.GetPointSize() + 4);
    hdr_font.SetWeight(wxFONTWEIGHT_BOLD);
    header->SetFont(hdr_font);
    outer_sizer->Add(header, 0, wxALL, 12);
    outer_sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // ?? Sub-notebook (Projects | G-code) ??????????????????????
    m_notebook = new wxNotebook(this, wxID_ANY);
    outer_sizer->Add(m_notebook, 1, wxEXPAND | wxALL, 8);

    // ??????????????????????????????????????????????????????????
    // PAGE 1 � Projects
    // ??????????????????????????????????????????????????????????
    m_project_page   = new wxPanel(m_notebook);
    auto* proj_sizer = new wxBoxSizer(wxVERTICAL);

    // Search bar
    m_search_projects = new wxSearchCtrl(m_project_page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_search_projects->SetDescriptiveText(_L("Search projects�"));
    proj_sizer->Add(m_search_projects, 0, wxEXPAND | wxALL, 6);

    // List
    m_project_list = new wxListCtrl(m_project_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_project_list->InsertColumn(0, _L("File"), wxLIST_FORMAT_LEFT, 260);
    m_project_list->InsertColumn(1, _L("Path"), wxLIST_FORMAT_LEFT, 340);
    m_project_list->InsertColumn(2, _L("Last Accessed"), wxLIST_FORMAT_LEFT, 150);
    proj_sizer->Add(m_project_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // Button row
    auto* proj_btn_sizer       = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_open_proj_folder = new wxButton(m_project_page, wxID_ANY, _L("Open Folder"));
    auto* btn_remove_proj      = new wxButton(m_project_page, wxID_ANY, _L("Remove Entry"));
    auto* btn_clear_proj       = new wxButton(m_project_page, wxID_ANY, _L("Clear All"));
    proj_btn_sizer->Add(btn_open_proj_folder, 0, wxRIGHT, 6);
    proj_btn_sizer->Add(btn_remove_proj, 0, wxRIGHT, 6);
    proj_btn_sizer->AddStretchSpacer();
    proj_btn_sizer->Add(btn_clear_proj, 0);
    proj_sizer->Add(proj_btn_sizer, 0, wxEXPAND | wxALL, 8);

    m_project_page->SetSizer(proj_sizer);
    m_notebook->AddPage(m_project_page, _L("Projects"), true);

    // ??????????????????????????????????????????????????????????
    // PAGE 2 � G-code
    // ??????????????????????????????????????????????????????????
    m_gcode_page   = new wxPanel(m_notebook);
    auto* gc_sizer = new wxBoxSizer(wxVERTICAL);

    // Search bar
    m_search_gcodes = new wxSearchCtrl(m_gcode_page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_search_gcodes->SetDescriptiveText(_L("Search G-code files�"));
    gc_sizer->Add(m_search_gcodes, 0, wxEXPAND | wxALL, 6);

    // List
    m_gcode_list = new wxListCtrl(m_gcode_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_gcode_list->InsertColumn(0, _L("File"), wxLIST_FORMAT_LEFT, 220);
    m_gcode_list->InsertColumn(1, _L("Printer"), wxLIST_FORMAT_LEFT, 200);
    m_gcode_list->InsertColumn(2, _L("Path"), wxLIST_FORMAT_LEFT, 280);
    m_gcode_list->InsertColumn(3, _L("Exported"), wxLIST_FORMAT_LEFT, 150);
    gc_sizer->Add(m_gcode_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // Button row
    auto* gc_btn_sizer       = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_open_gc_folder = new wxButton(m_gcode_page, wxID_ANY, _L("Open Folder"));
    auto* btn_remove_gc      = new wxButton(m_gcode_page, wxID_ANY, _L("Remove Entry"));
    auto* btn_clear_gc       = new wxButton(m_gcode_page, wxID_ANY, _L("Clear All"));
    gc_btn_sizer->Add(btn_open_gc_folder, 0, wxRIGHT, 6);
    gc_btn_sizer->Add(btn_remove_gc, 0, wxRIGHT, 6);
    gc_btn_sizer->AddStretchSpacer();
    gc_btn_sizer->Add(btn_clear_gc, 0);
    gc_sizer->Add(gc_btn_sizer, 0, wxEXPAND | wxALL, 8);

    m_gcode_page->SetSizer(gc_sizer);
    m_notebook->AddPage(m_gcode_page, _L("G-code Files"), false);

    SetSizer(outer_sizer);

    // ??????????????????????????????????????????????????????????
    // Event bindings
    // ??????????????????????????????????????????????????????????
    m_project_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &HistoryPanel::on_project_activated, this);
    m_gcode_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &HistoryPanel::on_gcode_activated, this);

    m_search_projects->Bind(wxEVT_SEARCH, &HistoryPanel::on_search_changed, this);
    m_search_projects->Bind(wxEVT_TEXT, &HistoryPanel::on_search_changed, this);
    m_search_gcodes->Bind(wxEVT_SEARCH, &HistoryPanel::on_search_changed, this);
    m_search_gcodes->Bind(wxEVT_TEXT, &HistoryPanel::on_search_changed, this);

    btn_clear_proj->Bind(wxEVT_BUTTON, &HistoryPanel::on_clear_projects, this);
    btn_clear_gc->Bind(wxEVT_BUTTON, &HistoryPanel::on_clear_gcodes, this);
    btn_remove_proj->Bind(wxEVT_BUTTON, &HistoryPanel::on_remove_project, this);
    btn_remove_gc->Bind(wxEVT_BUTTON, &HistoryPanel::on_remove_gcode, this);
    btn_open_proj_folder->Bind(wxEVT_BUTTON, &HistoryPanel::on_open_folder_project, this);
    btn_open_gc_folder->Bind(wxEVT_BUTTON, &HistoryPanel::on_open_folder_gcode, this);
}

// ============================================================
//  List population
// ============================================================

static bool entry_matches_filter(const HistoryEntry& e, const wxString& filter)
{
    if (filter.IsEmpty())
        return true;
    wxString lower_filter = filter.Lower();
    wxString fname        = wxString::FromUTF8(e.filename()).Lower();
    wxString fpath        = wxString::FromUTF8(e.path).Lower();
    wxString fprnt        = wxString::FromUTF8(e.printer).Lower();
    return fname.Contains(lower_filter) || fpath.Contains(lower_filter) || fprnt.Contains(lower_filter);
}

void HistoryPanel::populate_project_list()
{
    m_project_list->DeleteAllItems();
    m_filtered_projects.clear();

    wxString filter = m_search_projects ? m_search_projects->GetValue() : wxString{};

    for (const auto& entry : m_projects) {
        if (!entry_matches_filter(entry, filter))
            continue;
        m_filtered_projects.push_back(&entry);
    }

    for (long i = 0; i < static_cast<long>(m_filtered_projects.size()); ++i) {
        const auto* e   = m_filtered_projects[i];
        long        idx = m_project_list->InsertItem(i, wxString::FromUTF8(e->filename()));
        m_project_list->SetItem(idx, 1, wxString::FromUTF8(e->short_path()));
        m_project_list->SetItem(idx, 2, wxString::FromUTF8(e->date_string()));
    }
}

void HistoryPanel::populate_gcode_list()
{
    m_gcode_list->DeleteAllItems();
    m_filtered_gcodes.clear();

    wxString filter = m_search_gcodes ? m_search_gcodes->GetValue() : wxString{};

    for (const auto& entry : m_gcodes) {
        if (!entry_matches_filter(entry, filter))
            continue;
        m_filtered_gcodes.push_back(&entry);
    }

    for (long i = 0; i < static_cast<long>(m_filtered_gcodes.size()); ++i) {
        const auto* e   = m_filtered_gcodes[i];
        long        idx = m_gcode_list->InsertItem(i, wxString::FromUTF8(e->filename()));
        m_gcode_list->SetItem(idx, 1, wxString::FromUTF8(e->printer.empty() ? "�" : e->printer));
        m_gcode_list->SetItem(idx, 2, wxString::FromUTF8(e->short_path()));
        m_gcode_list->SetItem(idx, 3, wxString::FromUTF8(e->date_string()));
    }
}

// ============================================================
//  Public API
// ============================================================

void HistoryPanel::record_project(const std::string& path)
{
    if (path.empty())
        return;

    // Remove any existing entry for the same path so it moves to top
    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                     m_projects.end());

    HistoryEntry e;
    e.path      = path;
    e.timestamp = std::time(nullptr);
    m_projects.insert(m_projects.begin(), std::move(e));

    // Cap
    if (m_projects.size() > MAX_ENTRIES)
        m_projects.resize(MAX_ENTRIES);

    save();
    populate_project_list();
}

void HistoryPanel::record_gcode(const std::string& path, const std::string& printer)
{
    if (path.empty())
        return;

    m_gcodes.erase(std::remove_if(m_gcodes.begin(), m_gcodes.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                   m_gcodes.end());

    HistoryEntry e;
    e.path      = path;
    e.printer   = printer;
    e.timestamp = std::time(nullptr);
    m_gcodes.insert(m_gcodes.begin(), std::move(e));

    if (m_gcodes.size() > MAX_ENTRIES)
        m_gcodes.resize(MAX_ENTRIES);

    save();
    populate_gcode_list();
}

void HistoryPanel::refresh()
{
    load();
    populate_project_list();
    populate_gcode_list();
}

// ============================================================
//  Persistence
// ============================================================

std::string HistoryPanel::history_file_path() const
{
    // wxGetApp().app_config->config_path() points to the OrcaSlicer config dir
    // Fallback to user data dir if not available.
    wxString data_dir = wxStandardPaths::Get().GetUserDataDir();
    // OrcaSlicer stores config under <UserData>/OrcaSlicer � adjust if your build differs
    wxFileName fn(data_dir, "history.json");
    return fn.GetFullPath().ToUTF8().data();
}

void HistoryPanel::load()
{
    m_projects.clear();
    m_gcodes.clear();

    std::string   fpath = history_file_path();
    std::ifstream ifs(fpath);
    if (!ifs.is_open())
        return; // First run � no file yet, that's fine

    try {
        json root = json::parse(ifs);

        auto load_list = [](const json& arr, std::vector<HistoryEntry>& out) {
            if (!arr.is_array())
                return;
            for (const auto& item : arr) {
                HistoryEntry e;
                e.path      = item.value("path", "");
                e.printer   = item.value("printer", "");
                e.timestamp = static_cast<std::time_t>(item.value("timestamp", 0));
                if (!e.path.empty())
                    out.push_back(std::move(e));
            }
        };

        if (root.contains("projects"))
            load_list(root["projects"], m_projects);
        if (root.contains("gcodes"))
            load_list(root["gcodes"], m_gcodes);

    } catch (const json::parse_error& ex) {
        // Corrupted file � silently start fresh
        (void) ex;
        m_projects.clear();
        m_gcodes.clear();
    }
}

void HistoryPanel::save() const
{
    auto dump_list = [](const std::vector<HistoryEntry>& list) {
        json arr = json::array();
        for (const auto& e : list) {
            json item;
            item["path"]      = e.path;
            item["printer"]   = e.printer;
            item["timestamp"] = static_cast<int64_t>(e.timestamp);
            arr.push_back(std::move(item));
        }
        return arr;
    };

    json root;
    root["projects"] = dump_list(m_projects);
    root["gcodes"]   = dump_list(m_gcodes);

    std::string   fpath = history_file_path();
    std::ofstream ofs(fpath);
    if (ofs.is_open())
        ofs << root.dump(2);
}

// ============================================================
//  Event handlers
// ============================================================

void HistoryPanel::on_project_activated(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= m_filtered_projects.size())
        return;

    const std::string& path = m_filtered_projects[idx]->path;

    // Ask OrcaSlicer to open the project.
    // The exact call depends on your version; use whichever works:
    //   wxGetApp().plater()->load_project(wxString::FromUTF8(path));
    // or simply open it via the OS if the above is not available:
    wxLaunchDefaultApplication(wxString::FromUTF8(path));
}

void HistoryPanel::on_gcode_activated(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= m_filtered_gcodes.size())
        return;

    const std::string& path = m_filtered_gcodes[idx]->path;
    // Open with default application (e.g. system G-code viewer or text editor)
    wxLaunchDefaultApplication(wxString::FromUTF8(path));
}

void HistoryPanel::on_clear_projects(wxCommandEvent& /*evt*/)
{
    if (wxMessageBox(_L("Clear all project history?"), _L("Confirm"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;
    m_projects.clear();
    save();
    populate_project_list();
}

void HistoryPanel::on_clear_gcodes(wxCommandEvent& /*evt*/)
{
    if (wxMessageBox(_L("Clear all G-code history?"), _L("Confirm"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;
    m_gcodes.clear();
    save();
    populate_gcode_list();
}

void HistoryPanel::on_remove_project(wxCommandEvent& /*evt*/)
{
    long sel = m_project_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel == wxNOT_FOUND || static_cast<size_t>(sel) >= m_filtered_projects.size())
        return;

    const std::string path = m_filtered_projects[sel]->path;
    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                     m_projects.end());

    save();
    populate_project_list();
}

void HistoryPanel::on_remove_gcode(wxCommandEvent& /*evt*/)
{
    long sel = m_gcode_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel == wxNOT_FOUND || static_cast<size_t>(sel) >= m_filtered_gcodes.size())
        return;

    const std::string path = m_filtered_gcodes[sel]->path;
    m_gcodes.erase(std::remove_if(m_gcodes.begin(), m_gcodes.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                   m_gcodes.end());

    save();
    populate_gcode_list();
}

void HistoryPanel::on_search_changed(wxCommandEvent& /*evt*/)
{
    // Re-filter whichever page triggered the event
    populate_project_list();
    populate_gcode_list();
}

void HistoryPanel::on_open_folder_project(wxCommandEvent& /*evt*/)
{
    long sel = m_project_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel == wxNOT_FOUND || static_cast<size_t>(sel) >= m_filtered_projects.size())
        return;

    wxFileName fn(wxString::FromUTF8(m_filtered_projects[sel]->path));
    wxLaunchDefaultApplication(fn.GetPath());
}

void HistoryPanel::on_open_folder_gcode(wxCommandEvent& /*evt*/)
{
    long sel = m_gcode_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel == wxNOT_FOUND || static_cast<size_t>(sel) >= m_filtered_gcodes.size())
        return;

    wxFileName fn(wxString::FromUTF8(m_filtered_gcodes[sel]->path));
    wxLaunchDefaultApplication(fn.GetPath());
}

}} // namespace Slic3r::GUI

// ============================================================================
// ============================================================================
//
//  INTEGRATION GUIDE � paste these changes into the existing source files
//
// ============================================================================
// ============================================================================
//
// ?? 1.  src/slic3r/GUI/MainFrame.hpp ????????????????????????????????????????
//
//  In the class MainFrame, find the TabPosition enum (near the bottom of the
//  public section) and add tpHistory:
//
//      enum TabPosition : unsigned {
//          tp3DEditor  = 0,
//          tpProject   = 1,
//          tpHistory   = 2,   // <?? ADD THIS
//          tpMonitor   = 3,   // (shift existing values if needed)
//          // �
//      };
//
//  In the private members section add:
//
//      HistoryPanel* m_history_panel { nullptr };
//
//
// ?? 2.  src/slic3r/GUI/MainFrame.cpp ????????????????????????????????????????
//
//  At the top with other includes:
//
//      #include "HistoryPanel.hpp"
//
//  In MainFrame::init_tabpanel() (or wherever other tabs are added), after the
//  Project tab is added, insert:
//
//      m_history_panel = new HistoryPanel(m_tabpanel);
//      m_tabpanel->AddPage(m_history_panel, _L("History"), false);
//
//
// ?? 3.  Hook record_project() � Plater.cpp ??????????????????????????????????
//
//  Find the function that saves / opens a project file.  Common locations:
//    � Plater::save_project()           (called on File ? Save Project)
//    � Plater::load_project()           (called on File ? Open Project)
//
//  After the file path is confirmed, call:
//
//      if (auto* hist = wxGetApp().mainframe->m_history_panel)
//          hist->record_project(path.ToUTF8().data());
//
//  (Make m_history_panel public, or add a getter to MainFrame.)
//
//
// ?? 4.  Hook record_gcode() � Plater.cpp ????????????????????????????????????
//
//  Find the function that exports / slices to G-code.  Typically:
//    � Plater::export_gcode()
//
//  After the export path is confirmed and printer name is available:
//
//      if (auto* hist = wxGetApp().mainframe->m_history_panel) {
//          std::string printer = wxGetApp().preset_bundle->printers
//                                    .get_selected_preset().name;
//          hist->record_gcode(output_path.ToUTF8().data(), printer);
//      }
//
//
// ?? 5.  CMakeLists � src/slic3r/CMakeLists.txt ??????????????????????????????
//
//  In the set(SLIC3R_GUI_SOURCES �) block add:
//
//      GUI/HistoryPanel.cpp
//      GUI/HistoryPanel.hpp
//
// ============================================================================
// HistoryPanel.cpp  (v2 — replaces original)
// Drop into: src/slic3r/GUI/
// Add all four files to SLIC3R_GUI_SOURCES in src/slic3r/CMakeLists.txt.

#include "HistoryPanel.hpp"

#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <algorithm>
#include <ctime>

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "I18N.hpp"

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
        return "—";
    std::tm* tm = std::localtime(&timestamp);
    char     buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d  %H:%M", tm);
    return buf;
}

std::string HistoryEntry::short_path(size_t max_chars) const
{
    if (path.size() <= max_chars)
        return path;
    return "…" + path.substr(path.size() - max_chars + 1);
}

// ============================================================
//  Construction
// ============================================================

HistoryPanel::HistoryPanel(wxWindow* parent, wxWindowID id) : wxPanel(parent, id)
{
    // Initialise the VCBackup manager pointing at the OrcaSlicer data dir
    m_VCBackup_manager = std::make_unique<ProjectVCBackupManager>(data_dir(), MAX_VCBackups);

    build_ui();
    load();
    populate_project_list();
    populate_gcode_list();
}

// ============================================================
//  Button factory
// ============================================================

wxButton* HistoryPanel::make_button(wxWindow* parent, const wxString& label, HistoryBtnStyle style)
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

// ============================================================
//  UI construction
// ============================================================

void HistoryPanel::build_ui()
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto*  header = new wxStaticText(this, wxID_ANY, _L("Print History"));
    wxFont hf     = header->GetFont();
    hf.SetPointSize(hf.GetPointSize() + 4);
    hf.SetWeight(wxFONTWEIGHT_BOLD);
    header->SetFont(hf);
    outer->Add(header, 0, wxALL, 12);
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_notebook = new wxNotebook(this, wxID_ANY);
    outer->Add(m_notebook, 1, wxEXPAND | wxALL, 8);

    build_project_page(m_notebook);
    build_gcode_page(m_notebook);

    SetSizer(outer);
}

void HistoryPanel::build_project_page(wxNotebook* nb)
{
    m_project_page = new wxPanel(nb);
    auto* sizer    = new wxBoxSizer(wxVERTICAL);

    // Search
    m_search_projects = new wxSearchCtrl(m_project_page, wxID_ANY);
    m_search_projects->SetDescriptiveText(_L("Search projects…"));
    sizer->Add(m_search_projects, 0, wxEXPAND | wxALL, 6);

    // List
    m_project_list = new wxListCtrl(m_project_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_project_list->InsertColumn(0, _L("File"), wxLIST_FORMAT_LEFT, 240);
    m_project_list->InsertColumn(1, _L("Path"), wxLIST_FORMAT_LEFT, 320);
    m_project_list->InsertColumn(2, _L("Last Accessed"), wxLIST_FORMAT_LEFT, 150);
    sizer->Add(m_project_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // Button row
    auto* btn_row         = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_open_folder = make_button(m_project_page, _L("Open Folder"), HistoryBtnStyle::Secondary);
    auto* btn_remove      = make_button(m_project_page, _L("Remove Entry"), HistoryBtnStyle::Secondary);
    m_btn_capture         = make_button(m_project_page, _L("VCBackup now"), HistoryBtnStyle::Primary);
    auto* btn_clear       = make_button(m_project_page, _L("Clear All"), HistoryBtnStyle::Danger);

    btn_row->Add(btn_open_folder, 0, wxRIGHT, 6);
    btn_row->Add(btn_remove, 0, wxRIGHT, 6);
    btn_row->Add(m_btn_capture, 0, wxRIGHT, 6);
    btn_row->AddStretchSpacer();
    btn_row->Add(btn_clear, 0);
    sizer->Add(btn_row, 0, wxEXPAND | wxALL, 8);

    // VCBackupPanel — shown below the list when a project row is selected
    m_VCBackup_panel = new VCBackupPanel(m_project_page, m_VCBackup_manager.get());
    sizer->Add(m_VCBackup_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // Backup storage settings panel (always visible at the bottom)
    m_settings_panel = new VCBackupSettingsPanel(m_project_page, m_VCBackup_manager.get());
    sizer->Add(m_settings_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    
    
    m_project_page->SetSizer(sizer);
    nb->AddPage(m_project_page, _L("Projects"), true);
    
    // Events
    m_project_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &HistoryPanel::on_project_activated, this);
    m_project_list->Bind(wxEVT_LIST_ITEM_SELECTED, &HistoryPanel::on_project_selected, this);
    m_search_projects->Bind(wxEVT_SEARCH, &HistoryPanel::on_search_changed, this);
    m_search_projects->Bind(wxEVT_TEXT, &HistoryPanel::on_search_changed, this);
    btn_open_folder->Bind(wxEVT_BUTTON, &HistoryPanel::on_open_folder_project, this);
    btn_remove->Bind(wxEVT_BUTTON, &HistoryPanel::on_remove_project, this);
    m_btn_capture->Bind(wxEVT_BUTTON, &HistoryPanel::on_capture_now, this);
    btn_clear->Bind(wxEVT_BUTTON, &HistoryPanel::on_clear_projects, this);
}

void HistoryPanel::build_gcode_page(wxNotebook* nb)
{
    m_gcode_page = new wxPanel(nb);
    auto* sizer  = new wxBoxSizer(wxVERTICAL);

    // Search
    m_search_gcodes = new wxSearchCtrl(m_gcode_page, wxID_ANY);
    m_search_gcodes->SetDescriptiveText(_L("Search G-code files…"));
    sizer->Add(m_search_gcodes, 0, wxEXPAND | wxALL, 6);

    // List
    m_gcode_list = new wxListCtrl(m_gcode_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_gcode_list->InsertColumn(0, _L("File"), wxLIST_FORMAT_LEFT, 220);
    m_gcode_list->InsertColumn(1, _L("Printer"), wxLIST_FORMAT_LEFT, 190);
    m_gcode_list->InsertColumn(2, _L("Path"), wxLIST_FORMAT_LEFT, 260);
    m_gcode_list->InsertColumn(3, _L("Exported"), wxLIST_FORMAT_LEFT, 150);
    sizer->Add(m_gcode_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // Button row
    auto* btn_row         = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_open_folder = make_button(m_gcode_page, _L("Open Folder"), HistoryBtnStyle::Secondary);
    auto* btn_remove      = make_button(m_gcode_page, _L("Remove Entry"), HistoryBtnStyle::Secondary);
    m_btn_open_plater     = make_button(m_gcode_page, _L("Open in Plater"), HistoryBtnStyle::Primary);
    auto* btn_clear       = make_button(m_gcode_page, _L("Clear All"), HistoryBtnStyle::Danger);

    // "Open in Plater" starts disabled until a row is selected
    m_btn_open_plater->Enable(false);

    btn_row->Add(btn_open_folder, 0, wxRIGHT, 6);
    btn_row->Add(btn_remove, 0, wxRIGHT, 6);
    btn_row->Add(m_btn_open_plater, 0, wxRIGHT, 6);
    btn_row->AddStretchSpacer();
    btn_row->Add(btn_clear, 0);
    sizer->Add(btn_row, 0, wxEXPAND | wxALL, 8);

    m_gcode_page->SetSizer(sizer);
    nb->AddPage(m_gcode_page, _L("G-code Files"), false);

    // Events
    m_gcode_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &HistoryPanel::on_gcode_activated, this);
    m_gcode_list->Bind(wxEVT_LIST_ITEM_SELECTED, &HistoryPanel::on_gcode_selected, this);
    m_search_gcodes->Bind(wxEVT_SEARCH, &HistoryPanel::on_search_changed, this);
    m_search_gcodes->Bind(wxEVT_TEXT, &HistoryPanel::on_search_changed, this);
    btn_open_folder->Bind(wxEVT_BUTTON, &HistoryPanel::on_open_folder_gcode, this);
    btn_remove->Bind(wxEVT_BUTTON, &HistoryPanel::on_remove_gcode, this);
    m_btn_open_plater->Bind(wxEVT_BUTTON, &HistoryPanel::on_open_in_plater, this);
    btn_clear->Bind(wxEVT_BUTTON, &HistoryPanel::on_clear_gcodes, this);
}

// ============================================================
//  Public API
// ============================================================

void HistoryPanel::record_project(const std::string& path)
{
    if (path.empty())
        return;

    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                     m_projects.end());

    HistoryEntry e;
    e.path      = path;
    e.timestamp = std::time(nullptr);
    m_projects.insert(m_projects.begin(), std::move(e));

    if (m_projects.size() > MAX_ENTRIES)
        m_projects.resize(MAX_ENTRIES);

    // Auto-capture a VCBackup whenever the project is saved
    capture_VCBackup(path);

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

void HistoryPanel::capture_VCBackup(const std::string& path)
{
    if (!m_VCBackup_manager || path.empty())
        return;
    m_VCBackup_manager->capture_VCBackup(path);

    // If this project is currently shown in the VCBackup panel, refresh it
    if (m_VCBackup_panel && m_VCBackup_panel->IsShown())
        m_VCBackup_panel->refresh();
}

void HistoryPanel::refresh()
{
    load();
    populate_project_list();
    populate_gcode_list();
}

// ============================================================
//  List population helpers
// ============================================================

static bool matches_filter(const HistoryEntry& e, const wxString& filter)
{
    if (filter.IsEmpty())
        return true;
    wxString lf = filter.Lower();
    return wxString::FromUTF8(e.filename()).Lower().Contains(lf) || wxString::FromUTF8(e.path).Lower().Contains(lf) ||
           wxString::FromUTF8(e.printer).Lower().Contains(lf);
}

void HistoryPanel::populate_project_list()
{
    m_project_list->DeleteAllItems();
    m_filtered_projects.clear();

    wxString filter = m_search_projects ? m_search_projects->GetValue() : wxString{};

    for (const auto& e : m_projects)
        if (matches_filter(e, filter))
            m_filtered_projects.push_back(&e);

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

    for (const auto& e : m_gcodes)
        if (matches_filter(e, filter))
            m_filtered_gcodes.push_back(&e);

    for (long i = 0; i < static_cast<long>(m_filtered_gcodes.size()); ++i) {
        const auto* e   = m_filtered_gcodes[i];
        long        idx = m_gcode_list->InsertItem(i, wxString::FromUTF8(e->filename()));
        m_gcode_list->SetItem(idx, 1, wxString::FromUTF8(e->printer.empty() ? "—" : e->printer));
        m_gcode_list->SetItem(idx, 2, wxString::FromUTF8(e->short_path()));
        m_gcode_list->SetItem(idx, 3, wxString::FromUTF8(e->date_string()));
    }

    if (m_btn_open_plater)
        m_btn_open_plater->Enable(false);
}

// ============================================================
//  Persistence
// ============================================================

std::string HistoryPanel::data_dir() const { return wxStandardPaths::Get().GetUserDataDir().ToUTF8().data(); }

std::string HistoryPanel::history_file_path() const
{
    wxFileName fn(wxStandardPaths::Get().GetUserDataDir(), "history.json");
    return fn.GetFullPath().ToUTF8().data();
}

void HistoryPanel::load()
{
    m_projects.clear();
    m_gcodes.clear();

    std::ifstream ifs(history_file_path());
    if (!ifs.is_open())
        return;

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

    } catch (...) {
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

    std::ofstream ofs(history_file_path());
    if (ofs.is_open())
        ofs << root.dump(2);
}

// ============================================================
//  Event handlers — projects
// ============================================================

void HistoryPanel::on_project_activated(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= m_filtered_projects.size())
        return;
    const std::string& path = m_filtered_projects[idx]->path;
    // Open project in Plater on double-click
    wxGetApp().plater()->load_project(wxString::FromUTF8(path));
}

void HistoryPanel::on_project_selected(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= m_filtered_projects.size())
        return;
    // Show VCBackup timeline for the selected project
    if (m_VCBackup_panel)
        m_VCBackup_panel->load_project(m_filtered_projects[idx]->path);
}

void HistoryPanel::on_capture_now(wxCommandEvent& /*evt*/)
{
    long sel = m_project_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_filtered_projects.size()) {
        wxMessageBox(_L("Select a project row first."), _L("VCBackup"), wxOK, this);
        return;
    }
    capture_VCBackup(m_filtered_projects[sel]->path);
    wxMessageBox(_L("VCBackup saved."), _L("VCBackup"), wxOK | wxICON_INFORMATION, this);
}

void HistoryPanel::on_remove_project(wxCommandEvent& /*evt*/)
{
    long sel = m_project_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_filtered_projects.size())
        return;

    const std::string path = m_filtered_projects[sel]->path;
    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                     m_projects.end());

    save();
    populate_project_list();

    if (m_VCBackup_panel)
        m_VCBackup_panel->load_project("");
}

void HistoryPanel::on_clear_projects(wxCommandEvent& /*evt*/)
{
    if (wxMessageBox(_L("Clear all project history?"), _L("Confirm"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;
    m_projects.clear();
    save();
    populate_project_list();
    if (m_VCBackup_panel)
        m_VCBackup_panel->load_project("");
}

void HistoryPanel::on_open_folder_project(wxCommandEvent& /*evt*/)
{
    long sel = m_project_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_filtered_projects.size())
        return;
    wxFileName fn(wxString::FromUTF8(m_filtered_projects[sel]->path));
    wxLaunchDefaultApplication(fn.GetPath());
}

void HistoryPanel::on_search_changed(wxCommandEvent& /*evt*/)
{
    populate_project_list();
    populate_gcode_list();
}

// ============================================================
//  Event handlers — gcode
// ============================================================

void HistoryPanel::on_gcode_activated(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= m_filtered_gcodes.size())
        return;
    wxLaunchDefaultApplication(wxString::FromUTF8(m_filtered_gcodes[idx]->path));
}

void HistoryPanel::on_gcode_selected(wxListEvent& /*evt*/)
{
    if (m_btn_open_plater)
        m_btn_open_plater->Enable(true);
}

void HistoryPanel::on_open_in_plater(wxCommandEvent& /*evt*/)
{
    long sel = m_gcode_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_filtered_gcodes.size())
        return;

    const std::string& path = m_filtered_gcodes[sel]->path;

    // Load into the GCode viewer tab in the Plater
    // OrcaSlicer's Plater exposes load_gcode() which switches to the preview
    // tab and loads the file into the GCodeViewer.
    wxGetApp().plater()->load_gcode(wxString::FromUTF8(path));
}

void HistoryPanel::on_remove_gcode(wxCommandEvent& /*evt*/)
{
    long sel = m_gcode_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_filtered_gcodes.size())
        return;

    const std::string path = m_filtered_gcodes[sel]->path;
    m_gcodes.erase(std::remove_if(m_gcodes.begin(), m_gcodes.end(), [&path](const HistoryEntry& e) { return e.path == path; }),
                   m_gcodes.end());

    save();
    populate_gcode_list();
}

void HistoryPanel::on_clear_gcodes(wxCommandEvent& /*evt*/)
{
    if (wxMessageBox(_L("Clear all G-code history?"), _L("Confirm"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;
    m_gcodes.clear();
    save();
    populate_gcode_list();
}

void HistoryPanel::on_open_folder_gcode(wxCommandEvent& /*evt*/)
{
    long sel = m_gcode_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= m_filtered_gcodes.size())
        return;
    wxFileName fn(wxString::FromUTF8(m_filtered_gcodes[sel]->path));
    wxLaunchDefaultApplication(fn.GetPath());
}

}} // namespace Slic3r::GUI

// ============================================================================
//  INTEGRATION NOTES (updated for v2)
// ============================================================================
//
// No changes to MainFrame integration compared to v1 — the same
// AddPage() call and m_history_panel member apply.
//
// ── Hook changes ────────────────────────────────────────────────────────────
//
// In Plater::save_project() / load_project():
//
//   if (auto* hist = wxGetApp().mainframe->m_history_panel) {
//       hist->record_project(path.ToUTF8().data());
//       // record_project() now calls capture_VCBackup() automatically.
//       // No separate capture call needed unless you want on-demand VCBackups.
//   }
//
// In Plater::export_gcode():
//
//   if (auto* hist = wxGetApp().mainframe->m_history_panel) {
//       std::string printer = wxGetApp().preset_bundle->printers
//                                 .get_selected_preset().name;
//       hist->record_gcode(output_path.ToUTF8().data(), printer);
//   }
//
// ── CMakeLists.txt ───────────────────────────────────────────────────────────
//
//   GUI/HistoryPanel.hpp
//   GUI/HistoryPanel.cpp
//   GUI/VCBackupPanel.hpp
//   GUI/VCBackupPanel.cpp
//   GUI/ProjectVCBackupManager.hpp
//   GUI/ProjectVCBackupManager.cpp
//
// ── Plater.hpp check ─────────────────────────────────────────────────────────
//
//   Verify that Plater exposes:
//     void load_project(const wxString& filename);
//     void load_gcode(const wxString& filename);   // for "Open in Plater"
//
//   load_gcode() is present in OrcaSlicer as of mid-2024; it switches to the
//   Preview tab and loads the file into the embedded GCodeViewer. If your
//   branch uses a different method name, grep for "GCodeViewer" in Plater.cpp.
//
// ============================================================================
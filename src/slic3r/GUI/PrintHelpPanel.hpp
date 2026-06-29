#pragma once

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/html/htmlwin.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/hyperlink.h>
#include <wx/wrapsizer.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <wx/splitter.h>

#include <string>
#include <vector>
#include <functional>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace Slic3r {
namespace GUI {

// -----------------------------------------------------------------------
// Data structs (populated from JSON seed files at startup)
// -----------------------------------------------------------------------

struct HelpCategory {
    std::string id;
    std::string label;
    std::string color_hex;   // "#e05a2b"
};

enum class HelpEntryType { HMS, SlicerWarning, Maintenance, General };

struct HelpEntry {
    HelpEntryType   type;
    std::string     id;
    std::string     name;
    std::string     summary;
    std::string     description;
    std::string     severity;          // fatal / warning / info / error / suggestion
    std::vector<std::string> categories;
    std::vector<std::string> tags;
    std::vector<std::string> fix_steps;
    std::string     wiki_url;
    // HMS-specific
    std::string     code;
    std::vector<std::string> alt_codes;
    std::vector<std::string> likely_causes;
    // Maintenance-specific
    std::string     interval;
    std::string     interval_note;
    std::vector<std::string> tools_needed;
    std::vector<std::string> consumables;
    std::vector<std::string> warnings;
    // Slicer-warning specific
    std::string     message_pattern;
    std::vector<std::string> alt_patterns;
    std::vector<std::string> related_settings;
    // Printer family constraint (empty = all printers)
    std::string     printer_family;
};

// -----------------------------------------------------------------------
// Troubleshooter step (decision tree node)
// -----------------------------------------------------------------------

struct TroubleshootStep {
    std::string question;
    std::vector<std::string> options;
    // option index -> next step index; -1 = terminal
    std::vector<int>         next_steps;
    // terminal result text (when next_step == -1)
    std::string              result_text;
    std::string              result_url;
};

// -----------------------------------------------------------------------
// Tag chip button
// -----------------------------------------------------------------------

class TagChip : public wxPanel
{
public:
    TagChip(wxWindow* parent, const wxString& label, const wxColour& bg, std::function<void(bool)> on_toggle);
    bool IsActive() const { return m_active; }
    void SetActive(bool v);
    const wxString& GetTag() const { return m_label; }

private:
    wxStaticText* m_text;
    wxString      m_label;
    wxColour      m_bg_color;
    bool          m_active{false};
    std::function<void(bool)> m_on_toggle;

    void update_colors();
    void on_paint(wxPaintEvent&);
    void on_mouse(wxMouseEvent&);
};

// -----------------------------------------------------------------------
// Result card (single help entry displayed in the results list)
// -----------------------------------------------------------------------

class HelpResultCard : public wxPanel
{
public:
    HelpResultCard(wxWindow* parent, const HelpEntry& entry, std::function<void(const HelpEntry&)> on_click);

private:
    void on_paint(wxPaintEvent&);
    void on_mouse(wxMouseEvent&);

    const HelpEntry& m_entry;
    std::function<void(const HelpEntry&)> m_on_click;
    bool m_hovered{false};

    wxColour severity_color() const;
};

// -----------------------------------------------------------------------
// Detail view (shown when a result card is clicked)
// -----------------------------------------------------------------------

class HelpDetailView : public wxScrolledWindow
{
public:
    HelpDetailView(wxWindow* parent);
    void ShowEntry(const HelpEntry& entry);
    void Clear();

private:
    wxSizer* build_section(const wxString& title, const std::vector<std::string>& items, bool numbered = false);

    wxBoxSizer*  m_sizer;
    wxStaticText* m_title_text{nullptr};
    wxStaticText* m_subtitle_text{nullptr};
    wxPanel*      m_content_panel{nullptr};
};

// -----------------------------------------------------------------------
// Main Printer Help Panel
// -----------------------------------------------------------------------

class PrintHelpPanel : public wxPanel
{
public:
    PrintHelpPanel(wxWindow* parent);
    ~PrintHelpPanel() override = default;

    // Called by MainFrame when the panel becomes visible
    void OnActivate();

    // Called from NotificationManager to pre-fill search with an error code or warning text
    void SearchFor(const wxString& query);

    // Called when printer selection changes to refresh printer-specific tabs
    void OnPrinterChanged(const std::string& printer_vendor, const std::string& printer_model);

    // Load seed JSON files from the resources directory
    bool LoadSeedData(const std::string& resources_dir);

private:
    // UI construction
    void build_search_bar(wxSizer* parent_sizer);
    void build_tag_chips(wxSizer* parent_sizer);
    void build_tabs();
    void build_results_area();
    void build_detail_area();

    // Data loading helpers
    void load_hms_json(const json& j);
    void load_warnings_json(const json& j);
    void load_maintenance_json(const json& j);
    void load_general_json(const json& j);

    // Search / filter
    void apply_filter();
    bool entry_matches_query(const HelpEntry& e, const wxString& query) const;
    bool entry_matches_active_tags(const HelpEntry& e) const;
    bool entry_visible_for_printer(const HelpEntry& e) const;

    // Result rendering
    void render_results(const std::vector<const HelpEntry*>& results);
    void clear_results();
    void show_detail(const HelpEntry& entry);
    void hide_detail();

    // Event handlers
    void on_search_text(wxCommandEvent&);
    void on_search_enter(wxCommandEvent&);
    void on_tab_changed(wxBookCtrlEvent&);
    void on_tag_toggled(const std::string& category_id, bool active);
    void on_refresh_clicked(wxCommandEvent&);
    void on_troubleshooter_clicked(wxCommandEvent&);
    void on_search_timer(wxTimerEvent&);

    // Troubleshooter
    void open_troubleshooter(const HelpEntry* seed_entry = nullptr);

    // Data
    std::vector<HelpEntry>    m_all_entries;
    std::vector<HelpCategory> m_categories;
    std::string               m_current_printer_vendor;
    std::string               m_current_printer_model;
    bool                      m_data_loaded{false};

    // Active filter state
    wxString              m_search_text;
    std::string           m_active_tab;       // "all", "hms", "warnings", "maintenance", "general"
    std::vector<std::string> m_active_tag_ids;

    // UI widgets
    wxSearchCtrl*        m_search_ctrl{nullptr};
    wxNotebook*          m_tab_ctrl{nullptr};
    wxBoxSizer*          m_tag_sizer{nullptr};
    wxScrolledWindow*    m_tag_panel{nullptr};
    wxScrolledWindow*    m_results_scroll{nullptr};
    wxBoxSizer*          m_results_sizer{nullptr};
    HelpDetailView*      m_detail_view{nullptr};
    wxSplitterWindow*    m_splitter{nullptr};
    wxButton*            m_refresh_btn{nullptr};
    wxButton*            m_troubleshooter_btn{nullptr};
    wxStaticText*        m_result_count_label{nullptr};

    std::vector<TagChip*> m_tag_chips;
    wxTimer               m_search_timer;

    // Currently shown detail entry (nullptr = none)
    const HelpEntry*      m_detail_entry{nullptr};
};

} // namespace GUI
} // namespace Slic3r
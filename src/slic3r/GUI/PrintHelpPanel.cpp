#include "PrintHelpPanel.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/splitter.h>
#include <wx/msgdlg.h>
#include <wx/clipbrd.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static wxColour hex_to_colour(const std::string& hex, wxColour fallback = wxColour(150, 150, 150))
{
    if (hex.size() >= 7 && hex[0] == '#') {
        unsigned long val = std::stoul(hex.substr(1), nullptr, 16);
        return wxColour((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    }
    return fallback;
}

static wxColour severity_to_colour(const std::string& severity)
{
    if (severity == "fatal" || severity == "error")   return wxColour(0xE1, 0x47, 0x47);
    if (severity == "warning" || severity == "suggestion") return wxColour(0xF5, 0x9B, 0x16);
    if (severity == "info")                           return wxColour(0x90, 0x90, 0x90);
    return wxColour(0x00, 0x96, 0x88);
}

static wxString safe_str(const std::string& s) { return wxString::FromUTF8(s.c_str()); }

static bool icontains(const std::string& haystack, const std::string& needle)
{
    std::string h = haystack, n = needle;
    boost::algorithm::to_lower(h);
    boost::algorithm::to_lower(n);
    return h.find(n) != std::string::npos;
}

// -----------------------------------------------------------------------
// TagChip
// -----------------------------------------------------------------------

TagChip::TagChip(wxWindow* parent, const wxString& label, const wxColour& bg,
                 std::function<void(bool)> on_toggle)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_label(label)
    , m_bg_color(bg)
    , m_on_toggle(on_toggle)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_text = new wxStaticText(this, wxID_ANY, label);
    auto* sz = new wxBoxSizer(wxHORIZONTAL);
    sz->Add(m_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(6));
    SetSizer(sz);
    update_colors();

    Bind(wxEVT_PAINT,         &TagChip::on_paint, this);
    Bind(wxEVT_LEFT_DOWN,     &TagChip::on_mouse, this);
    m_text->Bind(wxEVT_LEFT_DOWN, &TagChip::on_mouse, this);
}

void TagChip::SetActive(bool v)
{
    m_active = v;
    update_colors();
    Refresh();
}

void TagChip::update_colors()
{
    if (m_active) {
        SetBackgroundColour(m_bg_color);
        m_text->SetForegroundColour(*wxWHITE);
    } else {
        SetBackgroundColour(wxColour(240, 240, 240));
        m_text->SetForegroundColour(wxColour(60, 60, 60));
    }
}

void TagChip::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetSize();
    int radius = FromDIP(10);

    wxColour bg = m_active ? m_bg_color : wxColour(240, 240, 240);
    dc.SetBrush(wxBrush(bg));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
}

void TagChip::on_mouse(wxMouseEvent&)
{
    m_active = !m_active;
    update_colors();
    Refresh();
    if (m_on_toggle) m_on_toggle(m_active);
}

// -----------------------------------------------------------------------
// HelpResultCard
// -----------------------------------------------------------------------

HelpResultCard::HelpResultCard(wxWindow* parent, const HelpEntry& entry,
                               std::function<void(const HelpEntry&)> on_click)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_entry(entry)
    , m_on_click(on_click)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(FromDIP(200), FromDIP(64)));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Severity indicator + name row
    auto* top_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* name_label = new wxStaticText(this, wxID_ANY, safe_str(entry.name),
                                        wxDefaultPosition, wxDefaultSize,
                                        wxST_ELLIPSIZE_END);
    wxFont bold_font = GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    name_label->SetFont(bold_font);
    name_label->SetForegroundColour(wxColour(230, 230, 230));
    top_sizer->Add(name_label, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    // Severity badge
    wxString sev_label = safe_str(entry.severity).Upper();
    auto* sev_text = new wxStaticText(this, wxID_ANY, sev_label);
    sev_text->SetForegroundColour(severity_to_colour(entry.severity));
    top_sizer->Add(sev_text, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));

    sizer->Add(top_sizer, 0, wxEXPAND | wxALL, FromDIP(8));

    // Summary text
    if (!entry.summary.empty()) {
        auto* sum_label = new wxStaticText(this, wxID_ANY, safe_str(entry.summary),
                                           wxDefaultPosition, wxDefaultSize,
                                           wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
        sum_label->SetForegroundColour(wxColour(190, 190, 190)); 
        sizer->Add(sum_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    }

    SetSizer(sizer);

    // Events
    auto bind_click = [this](wxWindow* w) {
        w->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { if (m_on_click) m_on_click(m_entry); });
        w->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hovered = true; Refresh(); });
        w->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = false; Refresh(); });
    };
    bind_click(this);
    for (wxWindow* child : GetChildren()) bind_click(child);

    Bind(wxEVT_PAINT, &HelpResultCard::on_paint, this);
}

void HelpResultCard::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetSize();
    wxColour bg = m_hovered ? wxColour(55, 55, 65) : wxColour(38, 38, 42);
    dc.SetBrush(wxBrush(bg));
    dc.SetPen(wxPen(wxColour(60, 60, 65)));  // thin, subtle border
    dc.DrawRoundedRectangle(0, 0, sz.x - 1, sz.y - 1, FromDIP(4));

    // Left severity stripe, keep it
    dc.SetBrush(wxBrush(severity_to_colour(m_entry.severity)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, FromDIP(3), sz.y);
}

void HelpResultCard::on_mouse(wxMouseEvent&)
{
    if (m_on_click) m_on_click(m_entry);
}

// -----------------------------------------------------------------------
// HelpDetailView
// -----------------------------------------------------------------------

HelpDetailView::HelpDetailView(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL | wxBORDER_NONE)
{
    SetScrollRate(0, FromDIP(10));
    m_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_sizer);
    SetBackgroundColour(*wxWHITE);
}

void HelpDetailView::Clear()
{
    m_sizer->Clear(true);
    m_title_text   = nullptr;
    m_subtitle_text = nullptr;
    m_content_panel = nullptr;
    Layout();
    Refresh();
}

wxSizer* HelpDetailView::build_section(const wxString& title,
                                       const std::vector<std::string>& items,
                                       bool numbered)
{
    if (items.empty()) return nullptr;
    auto* section_sizer = new wxBoxSizer(wxVERTICAL);

    auto* title_text = new wxStaticText(this, wxID_ANY, title);
    wxFont tf = title_text->GetFont(); tf.SetWeight(wxFONTWEIGHT_BOLD); title_text->SetFont(tf);
    title_text->SetForegroundColour(wxColour(220, 220, 220));  // was 40,40,40
    section_sizer->Add(title_text, 0, wxBOTTOM, FromDIP(4));

    for (size_t i = 0; i < items.size(); ++i) {
        wxString prefix = numbered ? wxString::Format("%zu. ", i + 1) : L"\u2022 ";
        auto* item_text = new wxStaticText(this, wxID_ANY,
                                           prefix + safe_str(items[i]),
                                           wxDefaultPosition, wxDefaultSize,
                                           wxST_NO_AUTORESIZE);
        item_text->Wrap(FromDIP(500));
        item_text->SetForegroundColour(wxColour(195, 195, 195));  // was 60,60,60
        section_sizer->Add(item_text, 0, wxBOTTOM | wxLEFT, FromDIP(2));
    }
    return section_sizer;
}

void HelpDetailView::ShowEntry(const HelpEntry& entry)
{
    Freeze();
    Clear();

    m_title_text = new wxStaticText(this, wxID_ANY, safe_str(entry.name));
    wxFont title_font = m_title_text->GetFont();
    title_font.SetPointSize(title_font.GetPointSize() + 3);
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    m_title_text->SetFont(title_font);
    m_title_text->SetForegroundColour(wxColour(235, 235, 235));  // bright white
    m_sizer->Add(m_title_text, 0, wxALL | wxEXPAND, FromDIP(12));

    auto* meta_sizer = new wxBoxSizer(wxHORIZONTAL);
    if (!entry.severity.empty()) {
        auto* sev = new wxStaticText(this, wxID_ANY, safe_str(entry.severity).Upper());
        sev->SetForegroundColour(severity_to_colour(entry.severity));
        wxFont sf = sev->GetFont(); sf.SetWeight(wxFONTWEIGHT_BOLD); sev->SetFont(sf);
        meta_sizer->Add(sev, 0, wxRIGHT, FromDIP(12));
    }
    if (!entry.code.empty()) {
        auto* code = new wxStaticText(this, wxID_ANY, safe_str(entry.code));
        code->SetForegroundColour(wxColour(180, 180, 180));  // visible gray
        meta_sizer->Add(code, 0);
    }
    if (meta_sizer->GetItemCount() > 0)
        m_sizer->Add(meta_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    if (!entry.description.empty()) {
        auto* desc = new wxStaticText(this, wxID_ANY, safe_str(entry.description),
                                      wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        desc->Wrap(FromDIP(520));
        desc->SetForegroundColour(wxColour(210, 210, 210));  // visible light gray
        m_sizer->Add(desc, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Separator
    auto* sep = new wxStaticLine(this);
    m_sizer->Add(sep, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    // Likely causes
    if (!entry.likely_causes.empty()) {
        auto* s = build_section(_L("Likely Causes"), entry.likely_causes, false);
        if (s) m_sizer->Add(s, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Fix steps
    if (!entry.fix_steps.empty()) {
        auto* s = build_section(_L("Fix Steps"), entry.fix_steps, true);
        if (s) m_sizer->Add(s, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Warnings / safety notes
    if (!entry.warnings.empty()) {
        auto* s = build_section(_L("Warnings"), entry.warnings, false);
        if (s) m_sizer->Add(s, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Tools / consumables (maintenance)
    if (!entry.tools_needed.empty()) {
        auto* s = build_section(_L("Tools Needed"), entry.tools_needed, false);
        if (s) m_sizer->Add(s, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }
    if (!entry.consumables.empty()) {
        auto* s = build_section(_L("Consumables"), entry.consumables, false);
        if (s) m_sizer->Add(s, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Related settings (slicer warnings)
    if (!entry.related_settings.empty()) {
        auto* s = build_section(_L("Related Settings"), entry.related_settings, false);
        if (s) m_sizer->Add(s, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Interval (maintenance)
    if (!entry.interval.empty()) {
        auto* intvl = new wxStaticText(this, wxID_ANY,
            _L("Interval") + ": " + safe_str(entry.interval) +
            (entry.interval_note.empty() ? "" : "  (" + entry.interval_note + ")"));
        intvl->SetForegroundColour(wxColour(210,210,210));
        m_sizer->Add(intvl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Wiki link
    if (!entry.wiki_url.empty()) {
        auto* link = new wxHyperlinkCtrl(this, wxID_ANY,
                                         _L("Open Bambu Lab Wiki"),
                                         safe_str(entry.wiki_url));
        m_sizer->Add(link, 0, wxLEFT | wxBOTTOM, FromDIP(12));
    }

    FitInside();
    Layout();
    Scroll(0, 0);
    Thaw();   // single repaint after everything is built
    Refresh();
}

// -----------------------------------------------------------------------
// PrintHelpPanel
// -----------------------------------------------------------------------

PrintHelpPanel::PrintHelpPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , m_search_timer(this)
{
    SetBackgroundColour(*wxWHITE);
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Top toolbar: search + buttons
    build_search_bar(main_sizer);

    // Tag filter chips
    build_tag_chips(main_sizer);

    // Sub-tabs: All / HMS Errors / Slicer Warnings / Maintenance / General
    build_tabs();
    main_sizer->Add(m_tab_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    // Result count label
    m_result_count_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_result_count_label->SetForegroundColour(wxColour(180, 180, 180));
    main_sizer->Add(m_result_count_label, 0, wxLEFT | wxBOTTOM, FromDIP(8));

    // Splitter: results on left, detail on right
    m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxSP_LIVE_UPDATE | wxSP_3DSASH | wxBORDER_NONE);
    m_splitter->SetMinimumPaneSize(FromDIP(180));
    m_splitter->SetSashGravity(0.4);

    // Results scroll
    m_results_scroll = new wxScrolledWindow(m_splitter, wxID_ANY,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxVSCROLL | wxBORDER_NONE);
    m_results_scroll->SetScrollRate(0, FromDIP(10));
    m_results_scroll->SetBackgroundColour(wxColour(248, 248, 248));
    m_results_sizer = new wxBoxSizer(wxVERTICAL);
    m_results_scroll->SetSizer(m_results_sizer);

    // Detail view
    m_detail_view = new HelpDetailView(m_splitter);

    m_splitter->SplitVertically(m_results_scroll, m_detail_view);

    main_sizer->Add(m_splitter, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    SetSizer(main_sizer);

    // Timer for debounced search
    Bind(wxEVT_TIMER, &PrintHelpPanel::on_search_timer, this, m_search_timer.GetId());
}

void PrintHelpPanel::build_search_bar(wxSizer* parent_sizer)
{
    auto* bar_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_search_ctrl = new wxSearchCtrl(this, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_search_ctrl->SetDescriptiveText(_L("Search by error code, description, or keyword"));
    m_search_ctrl->SetMinSize(wxSize(FromDIP(280), -1));
    bar_sizer->Add(m_search_ctrl, 1, wxEXPAND | wxRIGHT, FromDIP(6));

    m_troubleshooter_btn = new wxButton(this, wxID_ANY, _L("Troubleshooter"));
    m_troubleshooter_btn->SetToolTip(_L("Open the guided troubleshooter"));
    bar_sizer->Add(m_troubleshooter_btn, 0, wxRIGHT, FromDIP(6));

    m_refresh_btn = new wxButton(this, wxID_ANY, _L("Refresh Data"));
    m_refresh_btn->SetToolTip(_L("Reload help data from local seed files"));
    bar_sizer->Add(m_refresh_btn, 0);

    parent_sizer->Add(bar_sizer, 0, wxEXPAND | wxALL, FromDIP(8));

    m_search_ctrl->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, &PrintHelpPanel::on_search_enter, this);
    m_search_ctrl->Bind(wxEVT_TEXT_ENTER,             &PrintHelpPanel::on_search_enter, this);
    m_search_ctrl->Bind(wxEVT_TEXT,                   &PrintHelpPanel::on_search_text, this);
    m_refresh_btn->Bind(wxEVT_BUTTON,                 &PrintHelpPanel::on_refresh_clicked, this);
    m_troubleshooter_btn->Bind(wxEVT_BUTTON,          &PrintHelpPanel::on_troubleshooter_clicked, this);
}

void PrintHelpPanel::build_tag_chips(wxSizer* parent_sizer)
{
    // Tag panel that shows the tags going horizontally
    m_tag_panel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                       wxSize(-1, FromDIP(44)),
                                       wxHSCROLL | wxBORDER_NONE);
    m_tag_panel->SetScrollRate(FromDIP(20), 0);
    m_tag_panel->SetBackgroundColour(wxColour(30, 30, 30)); // match dark bg

    // Single-row horizontal sizer, no wrapping
    m_tag_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_tag_panel->SetSizer(m_tag_sizer);

    parent_sizer->Add(m_tag_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
}

void PrintHelpPanel::build_tabs()
{
    m_tab_ctrl = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);

    // Add placeholder pages (actual filtering is done via m_active_tab string)
    struct TabDef { std::string id; wxString label; };
    for (auto& td : std::vector<TabDef>{
            {"all",         _L("All")},
            {"hms",         _L("HMS Errors")},
            {"warnings",    _L("Slicer Warnings")},
            {"maintenance", _L("Maintenance")},
            {"general",     _L("General Tips")},
        })
    {
        // Placeholder empty panel per tab - we filter the shared results area
        auto* pg = new wxPanel(m_tab_ctrl, wxID_ANY);
        pg->SetClientData(reinterpret_cast<void*>(new std::string(td.id)));
        m_tab_ctrl->AddPage(pg, td.label);
    }

    m_active_tab = "all";

    m_tab_ctrl->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &PrintHelpPanel::on_tab_changed, this);
}

// -----------------------------------------------------------------------
// Data loading
// -----------------------------------------------------------------------

bool PrintHelpPanel::LoadSeedData(const std::string& resources_dir)
{
    m_all_entries.clear();
    m_categories.clear();

    struct FileMap { std::string filename; HelpEntryType type; };
    std::vector<FileMap> files = {
        {"bambu_p1s_hms.json",          HelpEntryType::HMS},
        {"orcaslicer_warnings.json",    HelpEntryType::SlicerWarning},
        {"maintenance_p1s.json",        HelpEntryType::Maintenance},
        {"general_troubleshooting.json",HelpEntryType::General},
    };

    // Try user data directory first, then resources
    std::vector<std::string> search_dirs = {
        resources_dir + "/help",
        resources_dir,
    };

    for (const auto& fm : files) {
        std::string found_path;
        for (const auto& dir : search_dirs) {
            fs::path p = fs::path(dir) / fm.filename;
            if (fs::exists(p)) { found_path = p.string(); break; }
        }
        if (found_path.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "PrintHelpPanel: seed file not found: " << fm.filename;
            continue;
        }

        try {
            boost::nowide::ifstream ifs(found_path);
            json j;
            ifs >> j;
            switch (fm.type) {
                case HelpEntryType::HMS:           load_hms_json(j);         break;
                case HelpEntryType::SlicerWarning: load_warnings_json(j);    break;
                case HelpEntryType::Maintenance:   load_maintenance_json(j); break;
                case HelpEntryType::General:       load_general_json(j);     break;
            }
            BOOST_LOG_TRIVIAL(info) << "PrintHelpPanel: loaded " << found_path;
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "PrintHelpPanel: failed to load " << found_path << ": " << ex.what();
        }
    }

    // Collect unique categories from loaded entries
    std::vector<std::string> cat_ids_seen;
    for (const auto& entry : m_all_entries) {
        for (const auto& cid : entry.categories) {
            if (std::find(cat_ids_seen.begin(), cat_ids_seen.end(), cid) == cat_ids_seen.end()) {
                cat_ids_seen.push_back(cid);
                HelpCategory cat;
                cat.id = cid;
                cat.label = cid; // will be overridden if found in JSON
                m_categories.push_back(cat);
            }
        }
    }

    // Rebuild tag chips
    m_tag_sizer->Clear(true);
    m_tag_chips.clear();

    // Standard category colors
    std::map<std::string, std::string> cat_colors = {
        {"temperature",   "#e05a2b"}, {"ams",          "#5b8dd9"},
        {"motion",        "#7c55cc"}, {"calibration",  "#3aaa6e"},
        {"sensor",        "#c9a227"}, {"electrical",   "#d63030"},
        {"mechanical",    "#888888"}, {"software",     "#2b9ea0"},
        {"geometry",      "#7c55cc"}, {"settings",     "#3aaa6e"},
        {"filament",      "#5b8dd9"}, {"support",      "#c9a227"},
        {"multimat",      "#2b9ea0"}, {"performance",  "#888888"},
        {"lubrication",   "#c9a227"}, {"cleaning",     "#3aaa6e"},
        {"inspection",    "#5b8dd9"}, {"replacement",  "#e05a2b"},
        {"adhesion",      "#e05a2b"}, {"extrusion",    "#5b8dd9"},
        {"quality",       "#7c55cc"}, {"general",      "#888888"},
        {"informational", "#aaaaaa"},
    };

    for (auto& cat : m_categories) {
        auto it = cat_colors.find(cat.id);
        cat.color_hex = (it != cat_colors.end()) ? it->second : "#888888";
        // Capitalize label
        cat.label = cat.id;
        if (!cat.label.empty()) cat.label[0] = toupper(cat.label[0]);

        wxColour chip_color = hex_to_colour(cat.color_hex);
        std::string cid = cat.id;
        auto* chip = new TagChip(m_tag_panel, safe_str(cat.label), chip_color,
                                 [this, cid](bool active) { on_tag_toggled(cid, active); });
        m_tag_sizer->AddSpacer(FromDIP(4));  // left padding
        
        m_tag_sizer->Add(chip, 0, wxTOP | wxBOTTOM | wxRIGHT, FromDIP(4));
        
        m_tag_chips.push_back(chip);
    }

    m_tag_panel->Layout();
    m_tag_panel->FitInside();

    m_data_loaded = true;
    apply_filter();
    return true;
}

void PrintHelpPanel::load_hms_json(const json& j)
{
    if (!j.contains("errors")) return;
    std::string printer_family;
    if (j.contains("meta") && j["meta"].contains("printer_family"))
        printer_family = j["meta"]["printer_family"].get<std::string>();

    for (const auto& err : j["errors"]) {
        HelpEntry e;
        e.type           = HelpEntryType::HMS;
        e.printer_family = printer_family;
        e.code           = err.value("code", "");
        e.id             = "hms_" + e.code;
        e.name           = err.value("name", "");
        e.description    = err.value("description", "");
        e.severity       = err.value("severity", "warning");
        e.wiki_url       = err.value("wiki_url", "");
        e.summary        = e.description.substr(0, std::min<size_t>(e.description.size(), 100));

        if (err.contains("alt_codes"))
            for (const auto& ac : err["alt_codes"]) e.alt_codes.push_back(ac.get<std::string>());
        if (err.contains("categories"))
            for (const auto& c : err["categories"]) e.categories.push_back(c.get<std::string>());
        if (err.contains("tags"))
            for (const auto& t : err["tags"]) e.tags.push_back(t.get<std::string>());
        if (err.contains("fix_steps"))
            for (const auto& s : err["fix_steps"]) e.fix_steps.push_back(s.get<std::string>());

        m_all_entries.push_back(std::move(e));
    }
}

void PrintHelpPanel::load_warnings_json(const json& j)
{
    if (!j.contains("warnings")) return;
    for (const auto& w : j["warnings"]) {
        HelpEntry e;
        e.type            = HelpEntryType::SlicerWarning;
        e.id              = w.value("id", "");
        e.name            = w.value("name", "");
        e.description     = w.value("description", "");
        e.severity        = w.value("severity", "warning");
        e.wiki_url        = w.value("wiki_url", "");
        e.message_pattern = w.value("message_pattern", "");
        e.summary         = e.description.substr(0, std::min<size_t>(e.description.size(), 100));

        if (w.contains("alt_patterns"))
            for (const auto& ap : w["alt_patterns"]) e.alt_patterns.push_back(ap.get<std::string>());
        if (w.contains("categories"))
            for (const auto& c : w["categories"]) e.categories.push_back(c.get<std::string>());
        if (w.contains("tags"))
            for (const auto& t : w["tags"]) e.tags.push_back(t.get<std::string>());
        if (w.contains("fix_steps"))
            for (const auto& s : w["fix_steps"]) e.fix_steps.push_back(s.get<std::string>());
        if (w.contains("likely_causes"))
            for (const auto& lc : w["likely_causes"]) e.likely_causes.push_back(lc.get<std::string>());
        if (w.contains("related_settings"))
            for (const auto& rs : w["related_settings"]) e.related_settings.push_back(rs.get<std::string>());

        m_all_entries.push_back(std::move(e));
    }
}

void PrintHelpPanel::load_maintenance_json(const json& j)
{
    if (!j.contains("tasks")) return;
    std::string printer_family;
    if (j.contains("meta") && j["meta"].contains("printer_family"))
        printer_family = j["meta"]["printer_family"].get<std::string>();

    for (const auto& t : j["tasks"]) {
        HelpEntry e;
        e.type           = HelpEntryType::Maintenance;
        e.printer_family = printer_family;
        e.id             = t.value("id", "");
        e.name           = t.value("name", "");
        e.summary        = t.value("summary", "");
        e.description    = e.summary;
        e.severity       = "info";
        e.interval       = t.value("interval", "");
        e.interval_note  = t.value("interval_note", "");
        e.wiki_url       = t.value("wiki_url", "");

        if (t.contains("categories")) e.categories.push_back(t["category"].get<std::string>());
        // category is a string not array in maintenance
        if (t.contains("category")) {
            e.categories.clear();
            e.categories.push_back(t["category"].get<std::string>());
        }
        if (t.contains("tags"))
            for (const auto& tg : t["tags"]) e.tags.push_back(tg.get<std::string>());
        if (t.contains("steps"))
            for (const auto& s : t["steps"]) e.fix_steps.push_back(s.get<std::string>());
        if (t.contains("tools_needed"))
            for (const auto& tn : t["tools_needed"]) e.tools_needed.push_back(tn.get<std::string>());
        if (t.contains("consumables"))
            for (const auto& cn : t["consumables"]) e.consumables.push_back(cn.get<std::string>());
        if (t.contains("warnings"))
            for (const auto& wn : t["warnings"]) e.warnings.push_back(wn.get<std::string>());
        if (t.contains("symptoms_if_skipped"))
            for (const auto& sy : t["symptoms_if_skipped"]) e.likely_causes.push_back(sy.get<std::string>());

        m_all_entries.push_back(std::move(e));
    }
}

void PrintHelpPanel::load_general_json(const json& j)
{
    if (!j.contains("issues")) return;
    for (const auto& iss : j["issues"]) {
        HelpEntry e;
        e.type        = HelpEntryType::General;
        e.id          = iss.value("id", "");
        e.name        = iss.value("name", "");
        e.summary     = iss.value("summary", "");
        e.description = e.summary;
        e.severity    = "suggestion";
        e.wiki_url    = "";

        if (iss.contains("categories"))
            for (const auto& c : iss["categories"]) e.categories.push_back(c.get<std::string>());
        if (iss.contains("tags"))
            for (const auto& t : iss["tags"]) e.tags.push_back(t.get<std::string>());
        if (iss.contains("fix_steps"))
            for (const auto& s : iss["fix_steps"]) e.fix_steps.push_back(s.get<std::string>());
        if (iss.contains("likely_causes"))
            for (const auto& lc : iss["likely_causes"]) e.likely_causes.push_back(lc.get<std::string>());
        if (iss.contains("symptoms"))
            for (const auto& sy : iss["symptoms"]) e.likely_causes.push_back(sy.get<std::string>());

        m_all_entries.push_back(std::move(e));
    }
}

// -----------------------------------------------------------------------
// Filtering
// -----------------------------------------------------------------------

bool PrintHelpPanel::entry_matches_query(const HelpEntry& e, const wxString& query) const
{
    if (query.IsEmpty()) return true;
    std::string q = query.Lower().ToStdString();

    // Normalize: strip dashes and spaces for code matching
    auto normalize = [](std::string s) {
        s.erase(std::remove_if(s.begin(), s.end(), [](char c){ return c == '-' || c == ' '; }), s.end());
        boost::algorithm::to_lower(s);
        return s;
    };

    std::string q_norm = normalize(q);

    if (!e.code.empty() && (icontains(e.code, q) || normalize(e.code).find(q_norm) != std::string::npos))
        return true;
    for (const auto& ac : e.alt_codes)
        if (normalize(ac).find(q_norm) != std::string::npos) return true;

    if (icontains(e.name, q))            return true;
    if (icontains(e.description, q))     return true;
    if (icontains(e.summary, q))         return true;
    for (const auto& t : e.tags)         if (icontains(t, q)) return true;
    if (icontains(e.message_pattern, q)) return true;
    for (const auto& ap : e.alt_patterns)      if (icontains(ap, q)) return true;
    for (const auto& rs : e.related_settings)  if (icontains(rs, q)) return true;

    return false;
}

bool PrintHelpPanel::entry_matches_active_tags(const HelpEntry& e) const
{
    if (m_active_tag_ids.empty()) return true;
    for (const auto& tid : m_active_tag_ids) {
        if (std::find(e.categories.begin(), e.categories.end(), tid) != e.categories.end())
            return true;
        if (std::find(e.tags.begin(), e.tags.end(), tid) != e.tags.end())
            return true;
    }
    return false;
}

bool PrintHelpPanel::entry_visible_for_printer(const HelpEntry& e) const
{
    return true; // Future: filter by m_current_printer_vendor/model
}

void PrintHelpPanel::apply_filter()
{
    std::vector<const HelpEntry*> results;
    for (const auto& e : m_all_entries) {
        // Tab filter
        if (m_active_tab == "hms"         && e.type != HelpEntryType::HMS)           continue;
        if (m_active_tab == "warnings"    && e.type != HelpEntryType::SlicerWarning) continue;
        if (m_active_tab == "maintenance" && e.type != HelpEntryType::Maintenance)   continue;
        if (m_active_tab == "general"     && e.type != HelpEntryType::General)       continue;

        if (!entry_visible_for_printer(e))   continue;
        if (!entry_matches_active_tags(e))   continue;
        if (!entry_matches_query(e, m_search_text)) continue;

        results.push_back(&e);
    }

    render_results(results);

    if (m_result_count_label)
        m_result_count_label->SetLabel(
            wxString::Format(_L("%zu result(s)"), results.size()));
}

// -----------------------------------------------------------------------
// Result rendering
// -----------------------------------------------------------------------

void PrintHelpPanel::clear_results()
{
    m_results_sizer->Clear(true);
    m_results_scroll->Layout();
    m_results_scroll->FitInside();
}

void PrintHelpPanel::render_results(const std::vector<const HelpEntry*>& results)
{
    m_results_scroll->Freeze();  // suppress redraws during rebuild
    clear_results();

    if (results.empty()) {
        auto* empty = new wxStaticText(m_results_scroll, wxID_ANY,
            m_search_text.IsEmpty() ? _L("Load data to see entries, or type a search query.")
                                    : _L("No results found."));
        empty->SetForegroundColour(wxColour(160, 160, 160));
        m_results_sizer->Add(empty, 0, wxALL, FromDIP(16));
    } else {
        for (const HelpEntry* e : results) {
            auto* card = new HelpResultCard(m_results_scroll, *e,
                [this](const HelpEntry& entry) { show_detail(entry); });
            m_results_sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(3));
        }
    }

    m_results_scroll->Layout();
    m_results_scroll->FitInside();
    m_results_scroll->Scroll(0, 0);
    m_results_scroll->Thaw();  // re-enable drawing, single repaint
}

void PrintHelpPanel::show_detail(const HelpEntry& entry)
{
    m_detail_entry = &entry;
    m_detail_view->ShowEntry(entry);
}

void PrintHelpPanel::hide_detail()
{
    m_detail_entry = nullptr;
    m_detail_view->Clear();
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

void PrintHelpPanel::OnActivate()
{
    if (!m_data_loaded) {
        // Try loading from the OrcaSlicer resources directory
        std::string res_dir = wxGetApp().app_config->get("installation_path");
        if (res_dir.empty())
            res_dir = Slic3r::resources_dir();
        LoadSeedData(res_dir);
    }
}

void PrintHelpPanel::SearchFor(const wxString& query)
{
    if (!m_data_loaded) OnActivate();
    m_search_ctrl->SetValue(query);
    m_search_text = query;
    // Switch to "All" tab
    if (m_tab_ctrl) m_tab_ctrl->SetSelection(0);
    m_active_tab = "all";
    apply_filter();
}

void PrintHelpPanel::OnPrinterChanged(const std::string& vendor, const std::string& model)
{
    m_current_printer_vendor = vendor;
    m_current_printer_model  = model;
    if (m_data_loaded) apply_filter();
}

// -----------------------------------------------------------------------
// Event handlers
// -----------------------------------------------------------------------

void PrintHelpPanel::on_search_text(wxCommandEvent& evt)
{
    m_search_text = m_search_ctrl->GetValue();
    // Debounce: restart 300ms timer
    m_search_timer.StartOnce(300);
    evt.Skip();
}

void PrintHelpPanel::on_search_enter(wxCommandEvent&)
{
    m_search_timer.Stop();
    m_search_text = m_search_ctrl->GetValue();
    apply_filter();
}

void PrintHelpPanel::on_search_timer(wxTimerEvent&)
{
    apply_filter();
}

void PrintHelpPanel::on_tab_changed(wxBookCtrlEvent& evt)
{
    int sel = evt.GetSelection();
    if (sel < 0 || !m_tab_ctrl) return;

    std::vector<std::string> tab_ids = {"all", "hms", "warnings", "maintenance", "general"};
    if (sel < (int)tab_ids.size()) {
        m_active_tab = tab_ids[sel];
        apply_filter();
    }
    evt.Skip();
}

void PrintHelpPanel::on_tag_toggled(const std::string& category_id, bool active)
{
    auto it = std::find(m_active_tag_ids.begin(), m_active_tag_ids.end(), category_id);
    if (active && it == m_active_tag_ids.end())
        m_active_tag_ids.push_back(category_id);
    else if (!active && it != m_active_tag_ids.end())
        m_active_tag_ids.erase(it);
    apply_filter();
}

void PrintHelpPanel::on_refresh_clicked(wxCommandEvent&)
{
    m_data_loaded = false;
    m_all_entries.clear();
    m_active_tag_ids.clear();
    OnActivate();
}

void PrintHelpPanel::on_troubleshooter_clicked(wxCommandEvent&)
{
    open_troubleshooter(m_detail_entry);
}

void PrintHelpPanel::open_troubleshooter(const HelpEntry* seed_entry)
{
    // Basic guided troubleshooter dialog
    wxDialog dlg(this, wxID_ANY, _L("Guided Troubleshooter"),
                 wxDefaultPosition, wxSize(FromDIP(500), FromDIP(400)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    wxString intro = seed_entry
        ? wxString::Format(_L("Troubleshooting: %s\n\nPlease answer the questions below to narrow down the issue."),
                           safe_str(seed_entry->name))
        : _L("Describe your issue in the box below, then search for related entries, or use the search bar to look up an error code.");
    auto* intro_text = new wxStaticText(&dlg, wxID_ANY, intro);
    intro_text->Wrap(FromDIP(460));
    sizer->Add(intro_text, 0, wxALL | wxEXPAND, FromDIP(16));

    if (seed_entry && !seed_entry->fix_steps.empty()) {
        auto* steps_label = new wxStaticText(&dlg, wxID_ANY, _L("Recommended steps:"));
        wxFont bf = steps_label->GetFont();
        bf.SetWeight(wxFONTWEIGHT_BOLD);
        steps_label->SetFont(bf);
        sizer->Add(steps_label, 0, wxLEFT | wxRIGHT, FromDIP(16));

        for (size_t i = 0; i < seed_entry->fix_steps.size(); ++i) {
            auto* step = new wxStaticText(&dlg, wxID_ANY,
                wxString::Format("%zu. %s", i + 1, safe_str(seed_entry->fix_steps[i])));
            step->Wrap(FromDIP(460));
            sizer->Add(step, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
        }
    }

    sizer->AddStretchSpacer();

    auto* btn_sizer = new wxStdDialogButtonSizer();
    auto* ok_btn = new wxButton(&dlg, wxID_OK, _L("Close"));
    btn_sizer->AddButton(ok_btn);
    btn_sizer->Realize();
    sizer->Add(btn_sizer, 0, wxALL | wxEXPAND, FromDIP(12));

    dlg.SetSizer(sizer);
    dlg.ShowModal();
}

} // namespace GUI
} // namespace Slic3r
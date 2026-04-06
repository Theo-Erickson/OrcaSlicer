#include "FavoritesPanel.hpp"
#include "FavoritesManager.hpp"
#include "StarButton.hpp"
#include "GUI_App.hpp"        // wxGetApp() — for plater/tabs access when navigating
#include "Plater.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statline.h>

#include "MainFrame.hpp"
#include "Tab.hpp"

namespace Slic3r {
namespace GUI {

// Pixel constants — match OrcaSlicer's existing sidebar row metrics.
static constexpr int ROW_HEIGHT       = 24;
static constexpr int SECTION_HEAD_H   = 20;
static constexpr int LEFT_PAD         = 8;
static constexpr int LABEL_FONT_SIZE  = 11;
static constexpr int HEADER_FONT_SIZE = 10;

FavoritesPanel::FavoritesPanel(wxWindow* parent, Tab* owner_tab)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL | wxNO_BORDER), m_owner_tab(owner_tab)
{
    SetScrollRate(0, 10);
    
    // Set the scroll sizer ONCE here, not in rebuild()
    auto* scroll_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(scroll_sizer);

    m_sub_id = FavoritesManager::get().subscribe(
        [this](const std::string&, bool) {
            CallAfter([this] { rebuild(); });
        });

    rebuild();
}

FavoritesPanel::~FavoritesPanel()
{
    if (m_sub_id >= 0)
        FavoritesManager::get().unsubscribe(m_sub_id);
}

void FavoritesPanel::refresh()
{
    rebuild();
}

// ---------------------------------------------------------------------------

void FavoritesPanel::rebuild()
{
    // Freeze to suppress flicker during teardown + rebuild.
    Freeze();

    // Destroy previous contents.
    if (m_inner) {
        m_inner->Destroy();
        m_inner = nullptr;
    }

    m_inner = new wxPanel(this, wxID_ANY);
    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);

    const auto favs = FavoritesManager::get().sorted_favorites();

    if (favs.empty()) {
        // Empty-state placeholder.
        auto* lbl = new wxStaticText(m_inner, wxID_ANY,
            "No favorites yet.\n\nHover any setting in the other tabs\n"
            "and click Star (*) to pin it here.");
        lbl->SetForegroundColour(wxColour(108, 112, 134));
        lbl->Wrap(260);
        outer_sizer->AddStretchSpacer();
        outer_sizer->Add(lbl, 0, wxALIGN_CENTER | wxALL, 16);
        outer_sizer->AddStretchSpacer();
    } else {
        // Group by tab, then by section within each tab.
        // sorted_favorites() already returns items in (tab_id, sort_order) order
        // so we just iterate and detect group boundaries.

        wxFont header_font(HEADER_FONT_SIZE, wxFONTFAMILY_DEFAULT,
                           wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        wxFont row_font(LABEL_FONT_SIZE, wxFONTFAMILY_DEFAULT,
                        wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

        std::string last_section;

        for (const auto& fav : favs) {
            if (fav.section_label != last_section) {
                last_section = fav.section_label;

                // Section divider + label.
                if (outer_sizer->GetItemCount() > 0) {
                    auto* line = new wxStaticLine(m_inner, wxID_ANY);
                    outer_sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, LEFT_PAD);
                }

                auto* sec_lbl = new wxStaticText(m_inner, wxID_ANY,
                                                 wxString::FromUTF8(fav.section_label));
                sec_lbl->SetFont(header_font);
                sec_lbl->SetForegroundColour(wxColour(180, 190, 254));  // accent2
                outer_sizer->Add(sec_lbl, 0, wxLEFT | wxTOP | wxRIGHT,
                                 LEFT_PAD);
                outer_sizer->AddSpacer(2);
            }

            // Row: [★] [label] ............. [value placeholder]
            auto* row_panel = new wxPanel(m_inner, wxID_ANY);
            auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);

            auto* star_lbl = new wxStaticText(row_panel, wxID_ANY, wxString::FromUTF8("★"));
            star_lbl->SetForegroundColour(wxColour(249, 226, 175)); // gold
            star_lbl->SetFont(row_font.Scaled(1.2f));
            star_lbl->SetCursor(wxCursor(wxCURSOR_HAND));
            row_sizer->Add(star_lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, LEFT_PAD);
            
            auto* name_lbl = new wxStaticText(row_panel, wxID_ANY,
                                              wxString::FromUTF8(fav.opt_key));
            name_lbl->SetFont(row_font);
            name_lbl->SetForegroundColour(wxColour(166, 173, 200));
            row_sizer->Add(name_lbl, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
            
            wxString value_str = "--";
            if (auto* tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
                // Read from config directly — always correct regardless of active page
                if (const DynamicPrintConfig* cfg = tab->get_config()) {
                    if (const ConfigOption* opt = cfg->option(fav.opt_key)) {
                        // Get the option definition for enum label lookup
                        const ConfigOptionDef* def = cfg->def() ? cfg->def()->get(fav.opt_key) : nullptr;

                        switch (opt->type()) {
                            case coFloat:
                            case coFloatOrPercent: {
                                double val = opt->getFloat();
                                // Trim trailing zeros e.g. 0.20 -> 0.2
                                value_str = wxString::Format("%.4g", val);
                                // Append % if percent
                                if (opt->type() == coFloatOrPercent &&
                                    static_cast<const ConfigOptionFloatOrPercent*>(opt)->percent)
                                    value_str += "%";
                                break;
                            }
                            case coPercent: {
                                value_str = wxString::Format("%.4g%%", opt->getFloat());
                                break;
                            }
                            case coInt: {
                                value_str = wxString::Format("%d", opt->getInt());
                                break;
                            }
                            case coBool: {
                                value_str = opt->getBool() ? _L("Yes") : _L("No");
                                break;
                            }
                            case coString: {
                                std::string s = static_cast<const ConfigOptionString*>(opt)->value;
                                // Truncate long strings like gcode
                                value_str = wxString::FromUTF8(s.size() > 30 ? s.substr(0, 27) + "..." : s);
                                break;
                            }
                            case coEnum: {
                                int ival = opt->getInt();
                                // Look up the human-readable label from the definition
                                if (def && ival >= 0 && ival < (int)def->enum_labels.size())
                                    value_str = _L(def->enum_labels[ival]);
                                else
                                    value_str = wxString::Format("%d", ival);
                                break;
                            }
                            case coFloats: {
                                // Vector — show first value with index 0
                                const auto* vec = static_cast<const ConfigOptionFloats*>(opt);
                                if (!vec->values.empty())
                                    value_str = wxString::Format("%.4g", vec->values[0]);
                                break;
                            }
                            case coInts: {
                                const auto* vec = static_cast<const ConfigOptionInts*>(opt);
                                if (!vec->values.empty())
                                    value_str = wxString::Format("%d", vec->values[0]);
                                break;
                            }
                            case coBools: {
                                const auto* vec = static_cast<const ConfigOptionBools*>(opt);
                                if (!vec->values.empty())
                                    value_str = vec->values[0] ? _L("Yes") : _L("No");
                                break;
                            }
                            case coStrings: {
                                const auto* vec = static_cast<const ConfigOptionStrings*>(opt);
                                if (!vec->values.empty()) {
                                    const std::string& s = vec->values[0];
                                    value_str = wxString::FromUTF8(s.size() > 30 ? s.substr(0, 27) + "..." : s);
                                }
                                break;
                            }
                            default:
                                value_str = "--";
                                break;
                        }

                        // Append sidetext (mm, mm/s, etc.) from the option definition
                        if (def && !def->sidetext.empty() && value_str != "--") {
                            value_str += " " + wxString::FromUTF8(def->sidetext);
                        }
                    }
                }
            }

            auto* val_lbl = new wxStaticText(row_panel, wxID_ANY, value_str);
            val_lbl->SetFont(row_font);
            val_lbl->SetForegroundColour(wxColour(108, 112, 134));
            val_lbl->SetCursor(wxCursor(wxCURSOR_HAND));
            val_lbl->SetToolTip(_L("Click to go to this setting"));
            row_sizer->Add(val_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

            // Click the value to jump to the setting in its original tab
            const std::string opt_key_copy = fav.opt_key;
            const std::string section_copy = fav.section_label;

            // ★ Click anywhere on the row to jump to the setting
            row_panel->Bind(wxEVT_LEFT_UP, [this, opt_key_copy, section_copy](wxMouseEvent& e) {
                m_owner_tab->show_favorites(); // close favorites panel
                if (auto* tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
                    wxGetApp().mainframe->select_tab((wxPanel*)tab->parent());
                    tab->activate_option(opt_key_copy, wxString::FromUTF8(
                        section_copy.substr(0, section_copy.find(" \xe2\x80\xba "))));
                }
                e.Skip();
            });

            // Also bind on the child labels so clicks on text bubble up correctly
            name_lbl->Bind(wxEVT_LEFT_UP, [row_panel](wxMouseEvent& e) {
                wxCommandEvent evt(wxEVT_LEFT_UP, row_panel->GetId());
                row_panel->GetEventHandler()->ProcessEvent(evt);
                e.Skip();
            });
            val_lbl->Bind(wxEVT_LEFT_UP, [row_panel](wxMouseEvent& e) {
                wxCommandEvent evt(wxEVT_LEFT_UP, row_panel->GetId());
                row_panel->GetEventHandler()->ProcessEvent(evt);
                e.Skip();
            });

            star_lbl->Bind(wxEVT_LEFT_UP, [opt_key_copy](wxMouseEvent& e) {
                FavoritesManager::get().toggle(opt_key_copy);
                // we purposely DONT skip, so that we don't trigger the on click of the row twice
            });
            
            row_panel->SetSizer(row_sizer);
            row_panel->SetMinSize(wxSize(-1, ROW_HEIGHT));

            // Hover highlight via background colour change.
            auto hover_enter = [row_panel](wxMouseEvent& e) {
                row_panel->SetBackgroundColour(wxColour(42, 42, 62));
                row_panel->Refresh();
                e.Skip();
            };
            auto hover_leave = [row_panel](wxMouseEvent& e) {
                row_panel->SetBackgroundColour(wxNullColour);
                row_panel->Refresh();
                e.Skip();
            };
            row_panel->Bind(wxEVT_ENTER_WINDOW, hover_enter);
            row_panel->Bind(wxEVT_LEAVE_WINDOW, hover_leave);

            outer_sizer->Add(row_panel, 0, wxEXPAND);
        }
        outer_sizer->AddSpacer(12);
    }

    m_inner->SetSizer(outer_sizer);
    m_inner->Layout();

    // Update the scroll sizer to point to new inner panel
    // (don't call SetSizer again — use the existing one)
    GetSizer()->Clear(false); // detach items but don't delete windows
    GetSizer()->Add(m_inner, 1, wxEXPAND);
    FitInside();
    Layout();

    Thaw();
    Refresh();
    
}

} // namespace GUI
} // namespace Slic3r
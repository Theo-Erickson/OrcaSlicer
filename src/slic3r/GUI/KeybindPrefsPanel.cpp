#include "KeybindPrefsPanel.hpp"
#include "KeyBindWidget.hpp"
#include "KeybindConfig.hpp"
#include "KeybindRegistry.hpp"
#include "I18N.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/statline.h>

namespace Slic3r {
namespace GUI {

KeybindPrefsPanel::KeybindPrefsPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    // let the parent control the width
    build_ui();
    populate_rows();
}

void KeybindPrefsPanel::build_ui()
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ── 1. Search bar
    m_search = new wxSearchCtrl(this, wxID_ANY);
    m_search->SetDescriptiveText(_L("Filter actions..."));
    m_search->Bind(wxEVT_SEARCH, [this](wxCommandEvent& e) { on_search(e.GetString()); });
    m_search->Bind(wxEVT_TEXT,   [this](wxCommandEvent& e) { on_search(e.GetString()); });
    outer->Add(m_search, 0, wxEXPAND | wxALL, 6);

    {
        auto* mode_row = new wxBoxSizer(wxHORIZONTAL);
        auto* mode_lbl = new wxStaticText(this, wxID_ANY, _L("Conflict handling:"));
        mode_row->Add(mode_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        auto* mode_combo = new wxChoice(this, wxID_ANY);
        mode_combo->Append(_L("Ignore: allow duplicate binds"));
        mode_combo->Append(_L("Warn: ask before overwriting"));
        mode_combo->Append(_L("Disallow: block duplicate keybinds"));
        mode_combo->SetSelection(
            static_cast<int>(KeybindRegistry::get().get_conflict_mode()));
        mode_row->Add(mode_combo, 0, wxALIGN_CENTER_VERTICAL);

        outer->Add(mode_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

        mode_combo->Bind(wxEVT_CHOICE, [this, mode_combo](wxCommandEvent&) {
            auto new_mode = static_cast<KeybindConflictMode>(
                mode_combo->GetSelection());
            on_conflict_mode_changed(new_mode);
        });
    }
    
    // ── 2. Column headers
    {
        auto* hdr = new wxPanel(this, wxID_ANY);
        auto* hs  = new wxBoxSizer(wxHORIZONTAL);
        auto make_hdr = [&](const wxString& t, int proportion) {
            auto* lbl = new wxStaticText(hdr, wxID_ANY, t);
            wxFont f = lbl->GetFont();
            f.MakeBold();
            lbl->SetFont(f);
            hs->Add(lbl, proportion, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        };
        make_hdr(_L("Category"),  2);
        make_hdr(_L("Action"),    4);
        make_hdr(_L("Shortcut"),  3);
        make_hdr(wxEmptyString,   1);
        hdr->SetSizer(hs);
        outer->Add(hdr, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
        outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 2);
    }

    // ── 3. Row panel — no scroll, just a plain panel that grows with content
    m_row_panel = new wxPanel(this, wxID_ANY);
    m_row_sizer = new wxBoxSizer(wxVERTICAL);
    m_row_panel->SetSizer(m_row_sizer);
    outer->Add(m_row_panel, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

    // ── 4. Separator + button bar
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    {
        auto* bar = new wxBoxSizer(wxHORIZONTAL);
        auto* btn_reset = new wxButton(this, wxID_ANY, _L("Reset all to defaults"));
        auto* btn_imp   = new wxButton(this, wxID_ANY, _L("Import..."));
        auto* btn_exp   = new wxButton(this, wxID_ANY, _L("Export..."));
        btn_reset->Bind(wxEVT_BUTTON, &KeybindPrefsPanel::on_reset_all, this);
        btn_imp  ->Bind(wxEVT_BUTTON, &KeybindPrefsPanel::on_import,    this);
        btn_exp  ->Bind(wxEVT_BUTTON, &KeybindPrefsPanel::on_export,    this);
        bar->Add(btn_reset, 0, wxALL, 4);
        bar->AddStretchSpacer();
        bar->Add(btn_imp,   0, wxALL, 4);
        bar->Add(btn_exp,   0, wxALL, 4);
        outer->Add(bar, 0, wxEXPAND);
    }
    
    Bind(wxEVT_COMMAND_TEXT_UPDATED, [this](wxCommandEvent& e) {
        // A widget reported that another action was unbound
        refresh_all_modified_states();
    });

    SetSizer(outer);
}

void KeybindPrefsPanel::on_conflict_mode_changed(KeybindConflictMode new_mode)
{
    auto old_mode = KeybindRegistry::get().get_conflict_mode();
    if (new_mode == old_mode) return;

    // If switching TO Disallow, check for existing conflicts and warn
    if (new_mode == KeybindConflictMode::Disallow) {
        auto& reg = KeybindRegistry::get();
        std::vector<std::pair<wxString, wxString>> conflicts;

        // Build a temporary reverse map to find duplicates
        std::unordered_map<std::string, std::string> seen; // "key+mod" -> action_id
        for (const KeybindEntry* e : reg.all_sorted()) {
            if (e->keycode == 0) continue;
            std::string key_str = std::to_string(e->keycode) + "+" +
                                  std::to_string(static_cast<int>(e->modifier));
            auto it = seen.find(key_str);
            if (it != seen.end()) {
                const KeybindEntry* other = reg.find_action(it->second);
                if (other)
                    conflicts.push_back({e->label, other->label});
            } else {
                seen[key_str] = e->action_id;
            }
        }

        if (!conflicts.empty()) {
            wxString msg = _L("Switching to Disallow mode would require "
                              "unbinding these conflicting actions:\n\n");
            for (auto& [a, b] : conflicts)
                msg += wxString::Format("  %s  ↔  %s\n", a, b);
            msg += "\n" + _L("Unbind conflicts and continue?");

            int res = wxMessageBox(msg, _L("Resolve conflicts"),
                                   wxYES_NO | wxICON_WARNING, this);
            if (res != wxYES) {
                // Revert combo selection
                // Find and reset the combo — easiest to just repopulate
                populate_rows(m_search->GetValue());
                return;
            }

            // Unbind the second of each conflicting pair
            for (auto& [a, b] : conflicts) {
                // Find action_id by label — scan all
                for (const KeybindEntry* e : reg.all_sorted()) {
                    if (e->label == b)
                        reg.unbind(e->action_id);
                }
            }
            refresh_all_modified_states();
        }
    }

    KeybindConfig::get().set_conflict_mode(new_mode);
}

void KeybindPrefsPanel::clear_rows()
{
    m_rows.clear();
    m_row_sizer->Clear(true);
}

void KeybindPrefsPanel::populate_rows(const wxString& filter)
{
    clear_rows();

    const wxString filter_lc = filter.Lower();
    auto& reg = KeybindRegistry::get();
    wxString current_category;

    for (const KeybindEntry* entry : reg.all_sorted()) {
        if (!filter_lc.empty()) {
            wxString haystack = entry->label.Lower() + " " +
                                entry->category.Lower() + " " +
                                wxString::FromUTF8(entry->action_id).Lower();
            if (!haystack.Contains(filter_lc))
                continue;
        }

        // Category header
        if (entry->category != current_category) {
            current_category = entry->category;
            auto* category_label = new wxStaticText(m_row_panel, wxID_ANY, current_category);
            wxFont f = category_label->GetFont();
            f.MakeBold();
            category_label->SetFont(f);
            m_row_sizer->Add(category_label, 0, wxLEFT | wxTOP, 8);
            m_row_sizer->Add(new wxStaticLine(m_row_panel), 0,
                             wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
        }

        // Action row
        const std::string action_id = entry->action_id;
        auto* row = new wxPanel(m_row_panel, wxID_ANY);
        auto* hs  = new wxBoxSizer(wxHORIZONTAL);

        // Category column (greyed)
        auto* cat_col = new wxStaticText(row, wxID_ANY, entry->category);
        cat_col->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        hs->Add(cat_col, 2, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

        // Action label
        auto* action_lbl = new wxStaticText(row, wxID_ANY, entry->label);
        if (!entry->description.IsEmpty())
            action_lbl->SetToolTip( entry->is_locked? _L("This keybind is hardcoded and cannot be changed") : entry->description);

        hs->Add(action_lbl, 4, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        

        // KeyBindWidget
        auto* kbw = new KeyBindWidget(row, entry->action_id, entry->keycode,
        [this, action_id](int keycode, KeybindModifier mod) {
                KeybindConfig::get().set(action_id, keycode, mod);
                KeybindConfig::get().save();
                // Immediately refresh all row states so undo buttons update
                refresh_all_modified_states();
            });
        if (!entry->description.IsEmpty())
            kbw->SetToolTip( entry->is_locked ? _L("This keybind is hardcoded and cannot be changed") :entry->description);
        hs->Add(kbw, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);  // proportion 0, fixed width
        
        // Only show if modified
        const bool is_modified = (entry->keycode   != entry->default_keycode ||
                                   entry->modifier != entry->default_modifier);
        
        auto* btn_undo = new wxButton(row, wxID_ANY, _L("Undo"),
                       wxDefaultPosition, wxSize(FromDIP(50), -1),
                       wxBU_EXACTFIT);

        // locked entries can never be reset (they have no meaningful "modified" state)
        if (entry->is_locked) {
            btn_undo->Enable(false);
            btn_undo->SetToolTip(_L("This keybind cannot be changed"));
        } else {
            btn_undo->Enable(is_modified);
        }
        
        // Style it to look inactive when not modified
        if (!is_modified)
            btn_undo->SetForegroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        btn_undo->Bind(wxEVT_BUTTON, [this, action_id](wxCommandEvent&) {
            on_reset_row(action_id);
        });
        hs->Add(btn_undo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);

        row->SetSizer(hs);
        m_row_sizer->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, 2);
        m_rows.push_back({action_id, kbw, row, btn_undo});
    }

    m_row_panel->Layout();
    Layout();
}

void KeybindPrefsPanel::on_search(const wxString& text)
{
    populate_rows(text);
}

void KeybindPrefsPanel::refresh_all_modified_states()
{
    auto& reg = KeybindRegistry::get();
    for (auto& row : m_rows) {
        const KeybindEntry* e = reg.find_action(row.action_id);
        if (!e || !row.widget) continue;

        if (e->is_locked) continue;   // locked rows never change state

        bool modified = (e->keycode  != e->default_keycode ||
                         e->modifier != e->default_modifier);

        row.widget->set_keycode(e->keycode);
        row.widget->set_modified(modified);

        if (row.undo_btn) {
            row.undo_btn->Enable(modified);
            row.undo_btn->SetForegroundColour(
                modified
                ? wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT)
                : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            row.undo_btn->Refresh();
        }
    }
    m_row_panel->Layout();
    Layout();
}

void KeybindPrefsPanel::on_reset_all(wxCommandEvent&)
{
    int res = wxMessageBox(
        _L("Reset all keybindings to their defaults?"),
        _L("Reset keybindings"),
        wxYES_NO | wxICON_QUESTION, this);
    if (res != wxYES) return;

    KeybindConfig::get().reset_all();
    populate_rows(m_search->GetValue());
}

void KeybindPrefsPanel::on_reset_row(const std::string& action_id)
{
    KeybindConfig::get().reset_action(action_id);
    const KeybindEntry* e = KeybindRegistry::get().find_action(action_id);
    if (!e) return;

    for (auto& row : m_rows) {
        if (row.action_id == action_id && row.widget) {
            row.widget->set_keycode(e->keycode);
            row.widget->set_modified(false);
            if (row.undo_btn)
                row.undo_btn->Enable(false);
            row.row_panel->Layout();
            break;
        }
    }
    // Also refresh any row that was unbound due to conflict resolution
    refresh_all_modified_states();
}

void KeybindPrefsPanel::on_export(wxCommandEvent&)
{
    wxFileDialog dlg(this, _L("Export keybindings"),
                     wxEmptyString, "keybinds",
                     "JSON files (*.json)|*.json|INI files (*.ini)|*.ini",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    wxString path = dlg.GetPath();
    std::string fmt = wxFileName(path).GetExt().Lower() == "ini" ? "ini" : "json";
    if (!KeybindConfig::get().export_to_file(path, fmt))
        wxMessageBox(_L("Failed to export keybindings."),
                     _L("Export error"), wxOK | wxICON_ERROR, this);
}

void KeybindPrefsPanel::on_import(wxCommandEvent&)
{
    wxFileDialog dlg(this, _L("Import keybindings"),
                     wxEmptyString, wxEmptyString,
                     "Keybind files (*.json;*.ini)|*.json;*.ini",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    wxString err = KeybindConfig::get().import_from_file(dlg.GetPath());
    if (!err.empty()) {
        wxMessageBox(_L("Import failed: ") + err,
                     _L("Import error"), wxOK | wxICON_ERROR, this);
        return;
    }
    populate_rows(m_search->GetValue());
}

bool KeybindPrefsPanel::commit()
{
    for (const auto& row : m_rows) {
        if (row.widget && row.widget->has_conflict()) {
            wxMessageBox(
                _L("Please resolve all keybind conflicts before closing."),
                _L("Keybind conflict"), wxOK | wxICON_WARNING, this);
            return false;
        }
    }
    KeybindConfig::get().save();
    return true;
}

} // namespace GUI
} // namespace Slic3r
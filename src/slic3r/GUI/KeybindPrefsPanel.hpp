#pragma once
#include <wx/panel.h>
#include <wx/srchctrl.h>
#include <vector>
#include <string>

#include "KeybindRegistry.hpp"

namespace Slic3r {
namespace GUI {

class KeyBindWidget;

struct KeybindRow {
    std::string    action_id;
    KeyBindWidget* widget   { nullptr };
    wxWindow*      row_panel{ nullptr };
    wxButton*      undo_btn  { nullptr };
};

class KeybindPrefsPanel : public wxPanel
{
public:
    KeybindPrefsPanel(wxWindow* parent);
    bool commit();

private:
    void build_ui();
    void on_conflict_mode_changed(KeybindConflictMode new_mode);
    void populate_rows(const wxString& filter = wxEmptyString);
    void clear_rows();

    void on_search(const wxString& text);
    void refresh_all_modified_states();
    void on_reset_all(wxCommandEvent&);
    void on_reset_row(const std::string& action_id);
    void on_export(wxCommandEvent&);
    void on_import(wxCommandEvent&);
    
    wxSearchCtrl* m_search     { nullptr };
    wxSizer*      m_row_sizer  { nullptr };  // owned by this panel directly
    wxPanel*      m_row_panel  { nullptr };  // panel that holds all rows

    std::vector<KeybindRow> m_rows;
};

} // namespace GUI
} // namespace Slic3r
#include "PrintStatusCustomizationPanel.hpp"
#include "PrintStatusThemeManager.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
 
#include <wx/filedlg.h>
#include <wx/statline.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/image.h>
 
namespace Slic3r {
namespace GUI {
 
// ---------------------------------------------------------------------------
// Custom event
// ---------------------------------------------------------------------------
wxDEFINE_EVENT(EVT_PRINT_STATUS_THEME_CHANGED, wxCommandEvent);
 
// ---------------------------------------------------------------------------
// Drag-and-drop target
// ---------------------------------------------------------------------------
class StateRowDropTarget : public wxFileDropTarget
{
public:
    StateRowDropTarget(PrintStatusCustomizationPanel* panel, int row_index)
        : m_panel(panel), m_row(row_index) {}
 
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override
    {
        if (filenames.empty()) return false;
        wxString path = filenames[0];
        wxString ext  = wxFileName(path).GetExt().Lower();
        if (ext != "gif" && ext != "png") {
            wxMessageBox("Only .gif and .png files are supported.",
                         "Unsupported file type", wxOK | wxICON_WARNING, m_panel);
            return false;
        }
        m_panel->OnFileChosen(m_row, path);
        return true;
    }
 
private:
    PrintStatusCustomizationPanel* m_panel;
    int                            m_row;
};
 
// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(PrintStatusCustomizationPanel, wxPanel)
    EVT_CHOICE(wxID_ANY, PrintStatusCustomizationPanel::OnThemeChanged)
wxEND_EVENT_TABLE()
 
// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
PrintStatusCustomizationPanel::PrintStatusCustomizationPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    BuildUI();
 
    m_editing_index = PrintStatusThemeManager::Get().ActiveIndex();
    RebuildThemeDropdown();
 
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i) {
        RefreshPreview(i);
        RefreshRowLabel(i);
    }
}
 
// ---------------------------------------------------------------------------
// BuildUI
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::BuildUI()
{
    // Use a vertical sizer directly — the wxScrolledWindow provides the
    // scroll container but we size it to show all content by default.
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(6);
    BuildTopBar(root);
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
    BuildGrid(root);
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
    BuildFooter(root);
    root->AddSpacer(6);
    
    SetSizerAndFit(root);   // measures all children and sets min size
}
 
void PrintStatusCustomizationPanel::BuildTopBar(wxSizer* root)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(8);
 
    auto* lbl = new wxStaticText(this, wxID_ANY, "Theme:");
    lbl->SetFont(lbl->GetFont().Bold());
    row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
 
    m_theme_dropdown = new wxChoice(this, wxID_ANY);
    m_theme_dropdown->SetMinSize(wxSize(160, -1));
    row->Add(m_theme_dropdown, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    // ↑ proportion=1 so dropdown takes available space, not a fixed width
 
    m_btn_new = new wxButton(this, wxID_ANY, "+ New",
                             wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btn_new->Bind(wxEVT_BUTTON, &PrintStatusCustomizationPanel::OnNewTheme, this);
    row->Add(m_btn_new, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
 
    m_btn_import = new wxButton(this, wxID_ANY, "Import zip...",
                                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btn_import->Bind(wxEVT_BUTTON, &PrintStatusCustomizationPanel::OnImportZip, this);
    row->Add(m_btn_import, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
 
    m_btn_export = new wxButton(this, wxID_ANY, "Export zip...",
                                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btn_export->Bind(wxEVT_BUTTON, &PrintStatusCustomizationPanel::OnExportZip, this);
    row->Add(m_btn_export, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
 
    m_btn_delete = new wxButton(this, wxID_ANY, "Delete",
                                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_btn_delete->Bind(wxEVT_BUTTON, &PrintStatusCustomizationPanel::OnDeleteTheme, this);
    row->Add(m_btn_delete, 0, wxALIGN_CENTER_VERTICAL);
 
    row->AddSpacer(8);
    root->Add(row, 0, wxEXPAND);
}
 
void PrintStatusCustomizationPanel::BuildGrid(wxSizer* root)
{
    // Column header
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    header->AddSpacer(16);
    auto mkHdr = [&](const wxString& txt, int minW) {
        auto* s = new wxStaticText(this, wxID_ANY, txt);
        wxFont f = s->GetFont();
        f.SetPointSize(f.GetPointSize() - 1);
        s->SetFont(f);
        s->SetForegroundColour(wxColour(150, 150, 150));
        if (minW > 0)
            s->SetMinSize(wxSize(minW, -1));
        header->Add(s, minW > 0 ? 0 : 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    };
    mkHdr("State",   120);
    mkHdr("Asset: click to browse or drag a .gif / .png here", 0);
    mkHdr("Preview",  50);
    header->AddSpacer(32 + 16);
    root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    root->AddSpacer(4);
 
    const auto& states = PrintStatusThemeManager::AllStates();
    m_rows.resize(states.size());
 
    static const std::vector kDotColour = {
        wxColour( 180, 180, 180), // IDLE
        wxColour( 80,  200, 160), // SLICING
        wxColour( 130, 200, 70), // SLICED
        wxColour( 80,  160, 240), // SENDING
        wxColour( 240, 170, 60), // PREPARE
        wxColour( 80,  200, 160), // RUNNING
        wxColour( 240, 170, 60), // PAUSE
        wxColour( 175, 160, 240), // FILAMENT_CHANGE
        wxColour( 180, 180, 180), // CALIBRATING
        wxColour( 80,  200, 160), // FINISH
        wxColour( 240, 90,  90), // FAILED
        wxColour( 180, 180, 180), // OFFLINE
        wxColour( 240, 120, 40),  // HEATING   
        wxColour( 80,  200, 160),  // LEVELING
        wxColour( 240, 90,  90),  // ERROR_PAUSE
    };
 
    for (int i = 0; i < (int)states.size(); ++i) {
        PrintState state = states[i];
        auto& row = m_rows[i];
 
        auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
        row_sizer->AddSpacer(16);
 
        // Dot
        wxColour dot_col = (i < kDotColour.size()) ? kDotColour[i] : wxColour(180, 180, 180); // use color from static color vector or default to grey if no entry exists
        auto* dot = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(8, 8));
        dot->SetBackgroundColour(dot_col);
        row_sizer->Add(dot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
 
        // State name
        auto* name_lbl = new wxStaticText(
            this, wxID_ANY,
            PrintStatusThemeManager::StateName(state),
            wxDefaultPosition, wxSize(96, -1));
        row_sizer->Add(name_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
 
        // File picker — expands to fill full available width.
        // No badge/label alongside it; filename shown as hover tooltip instead.
        auto* pick_panel = new wxPanel(this, wxID_ANY);
        pick_panel->SetBackgroundColour(GetBackgroundColour());
        auto* pick_sizer = new wxBoxSizer(wxHORIZONTAL);
 
        int idx = i;  // capture for lambdas
        //Fixed width so all Browse buttons are the same size across all rows.
        //// FromDIP(220) scales correctly on HiDPI displays.
        row.file_picker = new wxFilePickerCtrl(
            pick_panel, wxID_ANY,
            wxEmptyString,
            "Choose icon file",
            "Image files (*.gif;*.png)|*.gif;*.png",
            wxDefaultPosition, wxDefaultSize,
            wxFLP_OPEN | wxFLP_FILE_MUST_EXIST);
        
        row.file_picker->Bind(wxEVT_FILEPICKER_CHANGED,
            [this, idx](wxFileDirPickerEvent& e) {
                OnFileChosen(idx, e.GetPath());
            });
        
        pick_sizer->Add(row.file_picker, 1, wxALIGN_CENTER_VERTICAL);
 
        // Badge is hidden filename shown as tooltip instead.
        // We keep the pointer alive for RefreshRowLabel() logic but
        // never add it to any sizer.
        row.badge = new wxStaticText(pick_panel, wxID_ANY, wxEmptyString);
        row.badge->Hide();
        
        /* code for if we want the file name next to the browse button. Incompatible with row.badge->Hide();
        row.badge = new wxStaticText(pick_panel, wxID_ANY, "None2");
        wxFont bf = row.badge->GetFont();
        bf.SetPointSize(bf.GetPointSize() - 2);
        row.badge->SetFont(bf);
        row.badge->SetForegroundColour(wxColour(150, 150, 150));
        pick_sizer->Add(row.badge, 0, wxALIGN_CENTER_VERTICAL);
        */
        pick_panel->SetSizerAndFit(pick_sizer);
        pick_panel->SetDropTarget(new StateRowDropTarget(this, i));
        row_sizer->Add(pick_panel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
 
        // ── Preview wrapper panel ─────────────────────────────────────────
        // We use a fixed-size wrapper wxPanel so we never need to remove/
        // re-insert items from the parent sizer (avoids GetItemIndex()).
        // RefreshPreview() destroys and recreates the inner ctrl inside
        // this wrapper instead.
        row.preview_wrapper = new wxPanel(this, wxID_ANY,
                                          wxDefaultPosition, wxSize(32, 32));
        row.preview_wrapper->SetBackgroundColour(GetBackgroundColour());
        row.preview_wrapper_sizer = new wxBoxSizer(wxHORIZONTAL);
        row.preview_wrapper->SetSizer(row.preview_wrapper_sizer);
 
        // Start with an AnimationCtrl inside the wrapper.
        auto* anim = new wxAnimationCtrl(
            row.preview_wrapper, wxID_ANY, wxNullAnimation,
            wxDefaultPosition, wxSize(32, 32),
            wxAC_DEFAULT_STYLE | wxAC_NO_AUTORESIZE | wxBORDER_NONE);
        row.preview_ctrl    = anim;
        row.preview_is_anim = true;
        row.preview_wrapper_sizer->Add(anim, 0, wxALIGN_CENTER_VERTICAL);
 
        row_sizer->Add(row.preview_wrapper, 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
 
        // Clear button
        row.btn_clear = new wxButton(this, wxID_ANY, "x",
                                     wxDefaultPosition, wxSize(24, 24),
                                     wxBORDER_NONE);
        row.btn_clear->SetToolTip("Revert to Default for this state");
        row.btn_clear->Bind(wxEVT_BUTTON, [this, idx](wxCommandEvent&) {
            m_pending.erase(idx);
            const auto& themes = PrintStatusThemeManager::Get().Themes();
            if (m_editing_index > 0 && m_editing_index < (int)themes.size()) {
                wxString err;
                PrintStatusThemeManager::Get().ClearStateAsset(
                    m_editing_index,
                    PrintStatusThemeManager::AllStates()[idx],
                    err);
            }
            RefreshPreview(idx);
            RefreshRowLabel(idx);
        });
        row_sizer->Add(row.btn_clear, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->AddSpacer(16);
 
        auto* col = new wxBoxSizer(wxVERTICAL);
        col->Add(row_sizer, 0, wxEXPAND);
        if (i < (int)states.size() - 1)
            col->Add(new wxStaticLine(this, wxID_ANY,
                                     wxDefaultPosition, wxDefaultSize,
                                     wxLI_HORIZONTAL),
                     0, wxEXPAND | wxLEFT | wxRIGHT, 16);
        root->Add(col, 0, wxEXPAND);
    }
}
 
void PrintStatusCustomizationPanel::BuildFooter(wxSizer* root)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(16);
 
    m_footer_label = new wxStaticText(this, wxID_ANY, "");
    m_footer_label->SetForegroundColour(wxColour(150, 150, 150));
    row->Add(m_footer_label, 1, wxALIGN_CENTER_VERTICAL);
 
    auto* btn_cancel = new wxButton(this, wxID_ANY, "Cancel");
    btn_cancel->Bind(wxEVT_BUTTON, &PrintStatusCustomizationPanel::OnCancel, this);
    row->Add(btn_cancel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
 
    m_btn_apply = new wxButton(this, wxID_ANY, "Apply");
    m_btn_apply->Bind(wxEVT_BUTTON, &PrintStatusCustomizationPanel::OnApply, this);
    row->Add(m_btn_apply, 0, wxALIGN_CENTER_VERTICAL);
 
    row->AddSpacer(16);
    root->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
}
 
// ---------------------------------------------------------------------------
// RebuildThemeDropdown
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::RebuildThemeDropdown()
{
    if (!m_theme_dropdown) return;
    m_theme_dropdown->Clear();
 
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    for (const auto& t : themes)
        m_theme_dropdown->Append(wxString::FromUTF8(t.display_name));
 
    int active = PrintStatusThemeManager::Get().ActiveIndex();
    if (active < (int)themes.size())
        m_theme_dropdown->SetSelection(active);
    m_editing_index = active;
 
    RefreshEditableState();
    UpdateFooterLabel();
}
 
// ---------------------------------------------------------------------------
// RefreshPreview  — uses wrapper panel, no sizer position needed
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::RefreshPreview(int row_index)
{
    auto& row   = m_rows[row_index];
    PrintState s = PrintStatusThemeManager::AllStates()[row_index];
 
    // Resolve path: pending edit → theme file → empty (show nothing)
    wxString path;
    auto it = m_pending.find(row_index);
    if (it != m_pending.end()) {
        path = it->second;
    } else {
        const auto& themes = PrintStatusThemeManager::Get().Themes();
        if (m_editing_index > 0 && m_editing_index < (int)themes.size())
            path = PrintStatusThemeManager::Get().ResolveForTheme(
                       themes[m_editing_index], s);
    }
 
    if (path.empty()) {
        // No override — stop/clear the animation.
        if (row.preview_is_anim) {
            auto* anim = dynamic_cast<wxAnimationCtrl*>(row.preview_ctrl);
            if (anim) { anim->Stop(); anim->SetAnimation(wxNullAnimation); }
        }
        return;
    }
 
    wxString ext = wxFileName(path).GetExt().Lower();
 
    if (ext == "gif") {
        if (!row.preview_is_anim) {
            // Swap wxStaticBitmap → wxAnimationCtrl inside the wrapper.
            row.preview_wrapper_sizer->Clear(true);  // destroys old ctrl
            auto* anim = new wxAnimationCtrl(
                row.preview_wrapper, wxID_ANY, wxNullAnimation,
                wxDefaultPosition, wxSize(32, 32),
                wxAC_DEFAULT_STYLE | wxAC_NO_AUTORESIZE | wxBORDER_NONE);
            row.preview_wrapper_sizer->Add(anim, 0, wxALIGN_CENTER_VERTICAL);
            row.preview_ctrl    = anim;
            row.preview_is_anim = true;
            row.preview_wrapper->Layout();
        }
        wxAnimation anim_data;
        if (anim_data.LoadFile(path, wxANIMATION_TYPE_GIF)) {
            auto* ctrl = dynamic_cast<wxAnimationCtrl*>(row.preview_ctrl);
            if (ctrl) { ctrl->SetAnimation(anim_data); ctrl->Play(); }
        }
 
    } else if (ext == "png") {
        if (row.preview_is_anim) {
            // Swap wxAnimationCtrl → wxStaticBitmap inside the wrapper.
            row.preview_wrapper_sizer->Clear(true);  // destroys old ctrl
            auto* bmp = new wxStaticBitmap(
                row.preview_wrapper, wxID_ANY, wxNullBitmap,
                wxDefaultPosition, wxSize(32, 32));
            row.preview_wrapper_sizer->Add(bmp, 0, wxALIGN_CENTER_VERTICAL);
            row.preview_ctrl    = bmp;
            row.preview_is_anim = false;
            row.preview_wrapper->Layout();
        }
        wxImage img;
        if (img.LoadFile(path, wxBITMAP_TYPE_PNG) && img.IsOk()) {
            img.Rescale(32, 32, wxIMAGE_QUALITY_HIGH);
            auto* bmp = dynamic_cast<wxStaticBitmap*>(row.preview_ctrl);
            if (bmp) bmp->SetBitmap(wxBitmap(img));
        }
    }
}
 
// ---------------------------------------------------------------------------
// RefreshRowLabel
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::RefreshRowLabel(int row_index)
{
    auto& row = m_rows[row_index];
    PrintState state = PrintStatusThemeManager::AllStates()[row_index];
 
    // Determine what file is currently assigned for this state
    bool has_pending = m_pending.count(row_index) > 0;
 
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    wxString disk_path;
    if (m_editing_index > 0 && m_editing_index < (int)themes.size()) {
        disk_path = PrintStatusThemeManager::Get().ResolveForTheme(
            themes[m_editing_index], state);
    }
 
    bool has_override = has_pending || !disk_path.empty();
 
    // Build tooltip text: always show the canonical filename,
    // plus the source if it's a custom override
    wxString canonical = PrintStatusThemeManager::StateFilename(state);
    wxString tooltip;
 
    if (has_pending) {
        wxString fname = wxFileName(m_pending[row_index]).GetFullName();
        tooltip = canonical + "\nCustom: " + fname;
    } else if (!disk_path.empty()) {
        wxString fname = wxFileName(disk_path).GetFullName();
        tooltip = canonical + "\nTheme: " + fname;
    } else {
        tooltip = canonical + "\n(using built-in default)";
    }
 
    // Apply tooltip to the file picker, its internal button, and the preview
    if (row.file_picker)    row.file_picker->SetToolTip(tooltip);
    if (row.preview_wrapper) row.preview_wrapper->SetToolTip(tooltip);
    if (row.btn_clear)      row.btn_clear->SetToolTip("Clear override — revert to default");
 
    // Update clear button enabled state
    const auto& themes_ref = PrintStatusThemeManager::Get().Themes();
    bool theme_editable = (m_editing_index > 0
                              && m_editing_index < (int)themes_ref.size()
                              && !themes_ref[m_editing_index].is_bundled);
    if (row.btn_clear)
       row.btn_clear->Enable(has_override && theme_editable);
    // badge pointer kept alive but hidden — no label to update
    UpdateFooterLabel();
}
 
void PrintStatusCustomizationPanel::UpdateFooterLabel()
{
    if (!m_footer_label) return;
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    if (m_editing_index < 0 || m_editing_index >= (int)themes.size()) return;
 
    const auto& theme = themes[m_editing_index];
    int custom_count = 0;
    if (!theme.is_builtin()) {
        for (const auto& state : PrintStatusThemeManager::AllStates()) {
            wxString p = PrintStatusThemeManager::Get().ResolveForTheme(theme, state);
            if (!p.empty()) ++custom_count;
        }
    }
    for (auto& kv : m_pending)
        if (!kv.second.empty()) ++custom_count;
 
    m_footer_label->SetLabel(
        wxString::Format("Editing: %s   -   %d of %d states customized",
                         wxString::FromUTF8(theme.display_name),
                         custom_count,
                         (int)PrintStatusThemeManager::AllStates().size()));
}
 
// ---------------------------------------------------------------------------
// OnFileChosen
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::OnFileChosen(int row_index,
                                                   const wxString& path)
{
    if (path.empty()) return;
    wxString ext = wxFileName(path).GetExt().Lower();
    if (ext != "gif" && ext != "png") {
        wxMessageBox("Only .gif and .png files are supported.",
                     "Unsupported file type", wxOK | wxICON_WARNING, this);
        return;
    }
    m_pending[row_index] = path;
    RefreshPreview(row_index);
    RefreshRowLabel(row_index);
}
 
// ---------------------------------------------------------------------------
// Theme management events
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::OnThemeChanged(wxCommandEvent& /*evt*/)
{
    int sel = m_theme_dropdown->GetSelection();
    if (sel < 0) return;
    m_pending.clear();
    m_editing_index = sel;
 
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i) {
        RefreshPreview(i);
        RefreshRowLabel(i);
    }
    RefreshEditableState();
    UpdateFooterLabel();
}
 
void PrintStatusCustomizationPanel::OnNewTheme(wxCommandEvent& /*evt*/)
{
    // ── Build dialog ──────────────────────────────────────────────────────
    wxDialog dlg(this, wxID_ANY, "Create new theme",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
 
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->AddSpacer(12);
 
    // Name field
    auto* name_row = new wxBoxSizer(wxHORIZONTAL);
    name_row->Add(new wxStaticText(&dlg, wxID_ANY, "Name:"),
                  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* name_ctrl = new wxTextCtrl(&dlg, wxID_ANY, "My Theme",
                                     wxDefaultPosition, wxSize(220, -1));
    name_row->Add(name_ctrl, 1, wxEXPAND);
    outer->Add(name_row, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
    outer->AddSpacer(8);
 
    // Author field
    auto* author_row = new wxBoxSizer(wxHORIZONTAL);
    author_row->Add(new wxStaticText(&dlg, wxID_ANY, "Author:"),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* author_ctrl = new wxTextCtrl(&dlg, wxID_ANY, "",
                                       wxDefaultPosition, wxSize(220, -1));
    author_row->Add(author_ctrl, 1, wxEXPAND);
    outer->Add(author_row, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
    outer->AddSpacer(8);
 
    // Description field
    auto* desc_row = new wxBoxSizer(wxHORIZONTAL);
    desc_row->Add(new wxStaticText(&dlg, wxID_ANY, "Description:"),
                  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* desc_ctrl = new wxTextCtrl(&dlg, wxID_ANY, "",
                                     wxDefaultPosition, wxSize(220, -1));
    desc_row->Add(desc_ctrl, 1, wxEXPAND);
    outer->Add(desc_row, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
    outer->AddSpacer(12);
 
    outer->Add(new wxStaticLine(&dlg), 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
    outer->AddSpacer(8);
 
    // ── Source choice ─────────────────────────────────────────────────────
    auto* src_lbl = new wxStaticText(&dlg, wxID_ANY, "Start from:");
    outer->Add(src_lbl, 0, wxLEFT, 16);
    outer->AddSpacer(6);
 
    // Radio: blank
    auto* radio_blank = new wxRadioButton(&dlg, wxID_ANY, "Blank (no icons)",
                                          wxDefaultPosition, wxDefaultSize,
                                          wxRB_GROUP);
    outer->Add(radio_blank, 0, wxLEFT, 32);
    outer->AddSpacer(4);
 
    // Radio: duplicate active
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    int active_idx = PrintStatusThemeManager::Get().ActiveIndex();
    wxString active_name = wxString::FromUTF8(
        themes[active_idx].display_name);
    auto* radio_active = new wxRadioButton(&dlg, wxID_ANY,
        "Duplicate active theme  (" + active_name + ")");
    outer->Add(radio_active, 0, wxLEFT, 32);
    outer->AddSpacer(4);
 
    // Radio: duplicate chosen
    auto* radio_choose_row = new wxBoxSizer(wxHORIZONTAL);
    auto* radio_choose = new wxRadioButton(&dlg, wxID_ANY, "Duplicate:");
    radio_choose_row->Add(radio_choose, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
 
    // Theme picker dropdown (enabled only when radio_choose selected)
    auto* src_dropdown = new wxChoice(&dlg, wxID_ANY);
    for (const auto& t : themes)
        src_dropdown->Append(wxString::FromUTF8(t.display_name));
    src_dropdown->SetSelection(active_idx);
    src_dropdown->Enable(false);   // starts disabled
    radio_choose_row->Add(src_dropdown, 1, wxALIGN_CENTER_VERTICAL);
    outer->Add(radio_choose_row, 0, wxEXPAND | wxLEFT | wxRIGHT, 32);
 
    outer->AddSpacer(12);
 
    // ── Buttons ───────────────────────────────────────────────────────────
    auto* btn_row = dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    outer->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
 
    dlg.SetSizerAndFit(outer);
    dlg.CentreOnParent();
 
    // Enable/disable src_dropdown based on radio selection
    auto update_dropdown = [&](wxCommandEvent&) {
        src_dropdown->Enable(radio_choose->GetValue());
    };
    radio_blank->Bind(wxEVT_RADIOBUTTON, update_dropdown);
    radio_active->Bind(wxEVT_RADIOBUTTON, update_dropdown);
    radio_choose->Bind(wxEVT_RADIOBUTTON, update_dropdown);
 
    if (dlg.ShowModal() != wxID_OK) return;
 
    // ── Read values ───────────────────────────────────────────────────────
    wxString name   = name_ctrl->GetValue().Trim();
    wxString author = author_ctrl->GetValue().Trim();
    wxString desc   = desc_ctrl->GetValue().Trim();
 
    if (name.empty()) {
        wxMessageBox("Please enter a name for the theme.",
                     "Name required", wxOK | wxICON_WARNING, this);
        return;
    }
 
    wxString err;
    int new_idx = -1;
 
    if (radio_blank->GetValue()) {
        // ── Blank theme ───────────────────────────────────────────────────
        new_idx = PrintStatusThemeManager::Get().CreateTheme(
            name.ToStdString(), author.ToStdString(), desc.ToStdString(), err);
 
    } else {
        // ── Duplicate ────────────────────────────────────────────────────
        int src_idx = radio_active->GetValue()
                          ? active_idx
                          : src_dropdown->GetSelection();
        if (src_idx < 0) src_idx = active_idx;
 
        bool ok = PrintStatusThemeManager::Get().DuplicateTheme(
            src_idx, name.ToStdString(),
            author.ToStdString(), desc.ToStdString(), err);
        if (ok) {
            // DuplicateTheme calls SetActiveTheme internally
            new_idx = PrintStatusThemeManager::Get().ActiveIndex();
        }
    }
 
    if (new_idx < 0) {
        wxMessageBox("Could not create theme: " + err,
                     "Error", wxOK | wxICON_ERROR, this);
        return;
    }
 
    m_pending.clear();
    RebuildThemeDropdown();
    m_theme_dropdown->SetSelection(new_idx);
    m_editing_index = new_idx;
 
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i) {
        RefreshPreview(i);
        RefreshRowLabel(i);
    }
    
    RefreshEditableState();
}
 
void PrintStatusCustomizationPanel::OnImportZip(wxCommandEvent& /*evt*/)
{
    wxFileDialog dlg(this, "Import theme zip", "", "",
                     "Zip files (*.zip)|*.zip",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
 
    wxString err;
    // switch_to_new=true (default) — manager switches active theme internally
    bool ok = PrintStatusThemeManager::Get().ImportZip(dlg.GetPath(), err);
    if (!ok) {
        wxMessageBox("Import failed: " + err, "Error",
                     wxOK | wxICON_ERROR, this);
        return;
    }
 
    // Manager already switched; sync panel UI to the new active index.
    m_pending.clear();
    RebuildThemeDropdown();
 
    // Sync dropdown selection to what the manager now has active
    int active = PrintStatusThemeManager::Get().ActiveIndex();
    m_theme_dropdown->SetSelection(active);
    m_editing_index = active;
 
    // Refresh all rows to show the new theme's assets
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i) {
        RefreshPreview(i);
        RefreshRowLabel(i);
    }
 
    RefreshEditableState();
    UpdateFooterLabel();
}
 
void PrintStatusCustomizationPanel::OnExportZip(wxCommandEvent& /*evt*/)
{
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    if (m_editing_index <= 0 || m_editing_index >= (int)themes.size()) return;
 
    wxString default_name =
        wxString::FromUTF8(themes[m_editing_index].id) + ".zip";
    wxFileDialog dlg(this, "Export theme as zip", "", default_name,
                     "Zip files (*.zip)|*.zip",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
 
    wxString err;
    if (!PrintStatusThemeManager::Get().ExportZip(
            m_editing_index, dlg.GetPath(), err))
        wxMessageBox("Export failed: " + err, "Error",
                     wxOK | wxICON_ERROR, this);
    else
        wxMessageBox("Theme exported successfully.", "Done",
                     wxOK | wxICON_INFORMATION, this);
}
 
void PrintStatusCustomizationPanel::OnDeleteTheme(wxCommandEvent& /*evt*/)
{
    if (m_editing_index <= 0) return;
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    if (m_editing_index >= (int)themes.size()) return;
 
    wxString name = wxString::FromUTF8(themes[m_editing_index].display_name);
    if (wxMessageBox("Delete theme \"" + name + "\"? This cannot be undone.",
                     "Confirm delete", wxYES_NO | wxICON_WARNING, this) != wxYES)
        return;
 
    wxString err;
    if (!PrintStatusThemeManager::Get().DeleteTheme(m_editing_index, err)) {
        wxMessageBox("Delete failed: " + err, "Error",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    m_pending.clear();
    RebuildThemeDropdown();
 
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i) {
        RefreshPreview(i);
        RefreshRowLabel(i);
    }
}
 
// ---------------------------------------------------------------------------
// Apply / Cancel
// ---------------------------------------------------------------------------
void PrintStatusCustomizationPanel::OnApply(wxCommandEvent& /*evt*/)
{
    ApplyChanges();
}
 
void PrintStatusCustomizationPanel::OnCancel(wxCommandEvent& /*evt*/)
{
    m_pending.clear();
    int active = PrintStatusThemeManager::Get().ActiveIndex();
    m_theme_dropdown->SetSelection(active);
    m_editing_index = active;
 
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i) {
        RefreshPreview(i);
        RefreshRowLabel(i);
    }
    RefreshEditableState();
    UpdateFooterLabel();
}

void PrintStatusCustomizationPanel::RefreshEditableState()
{
    const auto& themes = PrintStatusThemeManager::Get().Themes();
    bool is_default = (m_editing_index == 0);
    bool is_bundled = (!is_default
                       && m_editing_index < (int)themes.size()
                       && themes[m_editing_index].is_bundled);
    bool read_only  = is_default || is_bundled;
 
    // Top bar buttons
    if (m_btn_export) m_btn_export->Enable(!is_default);
    if (m_btn_delete) m_btn_delete->Enable(!is_default && !is_bundled);
    // ↑ Prevent deletion of bundled themes (can always re-import)
 
    // Per-row controls
    for (int i = 0; i < (int)m_rows.size(); ++i) {
        if (m_rows[i].file_picker)
            m_rows[i].file_picker->Enable(!read_only);
        if (m_rows[i].btn_clear)
            m_rows[i].btn_clear->Enable(false); // refreshed per-row in RefreshRowLabel
    }
}

void PrintStatusCustomizationPanel::ApplyChanges()
{
    // 1. Commit pending file copies to the theme folder.
    const auto& themes_ref = PrintStatusThemeManager::Get().Themes();
    if (m_editing_index > 0) {
        const auto& states = PrintStatusThemeManager::AllStates();
        for (auto& kv : m_pending) {
            if (kv.second.empty()) continue;
            wxString err;
            PrintStatusThemeManager::Get().SetStateAsset(
                m_editing_index, states[kv.first], kv.second, err);
        }
    }
    m_pending.clear();
 
    // 2. Activate the selected theme.
    PrintStatusThemeManager::Get().SetActiveTheme(m_editing_index);
 
    // 3. Notify MainFrame.
    // wxGetApp() returns a reference — use . not ->.
    // mainframe is MainFrame* — call GetEventHandler() for wxPostEvent.
    if (wxGetApp().mainframe) {
        wxCommandEvent evt(EVT_PRINT_STATUS_THEME_CHANGED);
        evt.SetEventObject(this);
        wxPostEvent(wxGetApp().mainframe->GetEventHandler(), evt);
    }
 
    // 4. Refresh labels.
    const auto& states = PrintStatusThemeManager::AllStates();
    for (int i = 0; i < (int)states.size(); ++i)
        RefreshRowLabel(i);
 
    UpdateFooterLabel();
}
 
} // namespace GUI
} // namespace Slic3r
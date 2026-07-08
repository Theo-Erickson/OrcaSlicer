#pragma once

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/filepicker.h>
#include <wx/animate.h>
#include <wx/dnd.h>
 
#include <vector>
#include <unordered_map>
 
#include "PrintStatusThemeManager.hpp"
 
namespace Slic3r {
namespace GUI {
 
wxDECLARE_EVENT(EVT_PRINT_STATUS_THEME_CHANGED, wxCommandEvent);
 
class StateRowDropTarget;
 
class PrintStatusCustomizationPanel : public wxPanel
{
public:
    explicit PrintStatusCustomizationPanel(wxWindow* parent);
    ~PrintStatusCustomizationPanel() override = default;
 
    void ApplyChanges();
    void RebuildThemeDropdown();
 
private:
    void BuildUI();
    void BuildTopBar(wxSizer* parent_sizer);
    void BuildGrid(wxSizer* parent_sizer);
    void BuildFooter(wxSizer* parent_sizer);
 
    void RefreshPreview(int row_index);
    void RefreshRowLabel(int row_index);
    void UpdateFooterLabel();
    void OnFileChosen(int row_index, const wxString& path);
 
    void OnThemeChanged(wxCommandEvent& evt);
    void OnNewTheme    (wxCommandEvent& evt);
    void OnImportZip   (wxCommandEvent& evt);
    void OnExportZip   (wxCommandEvent& evt);
    void OnDeleteTheme (wxCommandEvent& evt);
    void OnRestoreDefaults(wxCommandEvent& evt);
    void OnApply       (wxCommandEvent& evt);
    void OnCancel      (wxCommandEvent& evt);
    
    void RefreshEditableState();
 
    std::unordered_map<int, wxString> m_pending;
    int m_editing_index { 0 };
 
    wxChoice*     m_theme_dropdown { nullptr };
    wxButton*     m_btn_new        { nullptr };
    wxButton*     m_btn_import     { nullptr };
    wxButton*     m_btn_export     { nullptr };
    wxButton*     m_btn_delete     { nullptr };
    wxButton*     m_btn_restore    { nullptr };
    wxStaticText* m_footer_label   { nullptr };
    wxButton*     m_btn_apply      { nullptr };
 
    struct RowWidgets {
        // Wrapper panel — holds the preview ctrl, never moves in the sizer.
        // We destroy/recreate the inner ctrl inside it when swapping GIF↔PNG.
        wxPanel*           preview_wrapper        { nullptr };
        wxSizer*           preview_wrapper_sizer  { nullptr };
 
        wxWindow*          preview_ctrl           { nullptr }; // wxAnimationCtrl or wxStaticBitmap
        bool               preview_is_anim        { true };
        wxFilePickerCtrl*  file_picker            { nullptr };
        wxStaticText*      badge                  { nullptr };
        wxButton*          btn_clear              { nullptr };
    };
    std::vector<RowWidgets> m_rows;
 
    friend class StateRowDropTarget;
 
    wxDECLARE_EVENT_TABLE();
};
 
} // namespace GUI
} // namespace Slic3r
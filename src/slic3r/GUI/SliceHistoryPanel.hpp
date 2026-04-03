#pragma once
#include <wx/popupwin.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include "SliceHistoryManager.hpp"

namespace Slic3r { namespace GUI {

class Plater;

// Appears as a popup anchored below the History button in the Preview toolbar.
// Dismisses automatically when the user clicks outside (like a combo dropdown).
class SliceHistoryPanel : public wxPopupTransientWindow {
public:
    SliceHistoryPanel(wxWindow* parent,
                      Plater*  plater,
                      SliceHistoryManager* mgr);

    // Rebuild from manager contents; pass current global print config for diff
    void refresh(const DynamicPrintConfig* current_config);

    // Show anchored below a button
    void popup_below(wxWindow* anchor);

private:
    Plater*              m_plater;
    SliceHistoryManager* m_mgr;
    wxNotebook*          m_notebook  = nullptr;
    wxButton*            m_clear_btn = nullptr;

    const DynamicPrintConfig* m_last_config = nullptr; // non-owning

    void build_tabs();
    void on_restore(wxCommandEvent& evt);
    void on_clear_history(wxCommandEvent&);
    
    void OnDismiss() override;
    wxWindow* m_anchor_btn = nullptr;

};

}} // namespace Slic3r::GUI
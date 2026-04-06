#pragma once

// FavoritesPanel.hpp
// The scrollable panel that fills the sidebar when the ★ tab is selected.
// It subscribes to FavoritesManager and rebuilds itself on any change.
// Items appear grouped by their originating tab, in canonical tab order,
// and within each group by sort_order — matching the original sidebar layout.

#include <wx/wx.h>
#include <wx/scrolwin.h>

namespace Slic3r {
namespace GUI {
class Tab;

class FavoritesPanel : public wxScrolledWindow
{
public:
    explicit FavoritesPanel(wxWindow* parent, Tab* owner_tab);
    ~FavoritesPanel() override;

    // Called by the sidebar when this tab becomes visible.
    void refresh();

private:
    void rebuild();

    // Subscriber id returned by FavoritesManager::subscribe()
    int  m_sub_id { -1 };

    wxPanel* m_inner { nullptr };
    
    Tab* m_owner_tab { nullptr };
};

} // namespace GUI
} // namespace Slic3r
#pragma once

// StarButton.hpp
// A lightweight wxControl that shows an empty or filled star.
// It is placed to the left of each config option label by
// OG_CustomCtrl::add_star_button().  Width is exactly STAR_WIDTH px
// so layout stays aligned regardless of font scaling.

#include <wx/wx.h>

namespace Slic3r {
namespace GUI {

wxDECLARE_EVENT(EVT_STAR_TOGGLED, wxCommandEvent);

class StarButton : public wxWindow
{
public:
    static constexpr int STAR_WIDTH  = 20;
    static constexpr int STAR_HEIGHT = 20;

    StarButton(wxWindow* parent, const std::string& opt_key);

    // Called externally (e.g. FavoritesManager callback) to sync visual state
    // without re-emitting the toggle event.
    void set_favorited(bool favorited, bool refresh = true);
    bool is_favorited() const { return m_favorited; }

    const std::string& opt_key() const { return m_opt_key; }

private:
    void on_paint(wxPaintEvent&);
    void on_mouse_enter(wxMouseEvent&);
    void on_mouse_leave(wxMouseEvent&);
    void on_left_up(wxMouseEvent&);

    void render(wxDC& dc);

    std::string m_opt_key;
    bool        m_favorited { false };
    bool        m_hovered   { false };
};

} // namespace GUI
} // namespace Slic3r
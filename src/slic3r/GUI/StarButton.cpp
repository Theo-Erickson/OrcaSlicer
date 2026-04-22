#include "StarButton.hpp"
#include "FavoritesManager.hpp"

#include <wx/dcbuffer.h>
#include <cmath>

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_STAR_TOGGLED, wxCommandEvent);

// Five-point star path, scaled into (w, h) rectangle.
static void draw_star(wxDC& dc, int cx, int cy, int outer_r, int inner_r)
{
    static const double PI = std::acos(-1.0);
    wxPoint pts[10];
    for (int i = 0; i < 10; ++i) {
        double angle = PI / 2.0 + i * 2.0 * PI / 10.0;  // offset so top point is up
        int    r     = (i % 2 == 0) ? outer_r : inner_r;
        pts[i] = wxPoint(cx + static_cast<int>(r * std::cos(angle)),
                         cy - static_cast<int>(r * std::sin(angle)));
    }
    dc.DrawPolygon(10, pts);
}

StarButton::StarButton(wxWindow* parent, const std::string& opt_key)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(STAR_WIDTH, STAR_HEIGHT))
    , m_opt_key(opt_key)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Initially invisible; the parent row makes us visible on hover.
    Hide();

    // Sync initial state from manager in case settings were loaded.
    m_favorited = FavoritesManager::get().is_favorited(opt_key);
    if (m_favorited)
        Show();

    Bind(wxEVT_PAINT,       &StarButton::on_paint,       this);
    Bind(wxEVT_ENTER_WINDOW,&StarButton::on_mouse_enter, this);
    Bind(wxEVT_LEAVE_WINDOW,&StarButton::on_mouse_leave, this);
    Bind(wxEVT_LEFT_UP,     &StarButton::on_left_up,     this);
    Bind(wxEVT_SET_CURSOR, [](wxSetCursorEvent& e) {
        e.SetCursor(wxCursor(wxCURSOR_HAND));
    });
}

void StarButton::set_favorited(bool favorited, bool refresh)
{
    if (m_favorited == favorited) return;
    m_favorited = favorited;
    Show(m_favorited || m_hovered);
    if (refresh) Refresh();
}

void StarButton::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    render(dc);
}

void StarButton::render(wxDC& dc)
{
    // Transparent background — match the parent panel.
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    const int cx = STAR_WIDTH  / 2;
    const int cy = STAR_HEIGHT / 2;
    const int r_outer = 7;
    const int r_inner = 3;

    if (m_favorited) {
        // Filled gold star
        dc.SetBrush(wxBrush(wxColour(249, 226, 175)));
        dc.SetPen(wxPen(wxColour(200, 170, 80), 1));
    } else if (m_hovered) {
        // Hollow star, accent colour
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(wxColour(137, 180, 250), 1));
    } else {
        // Should not reach here (we Hide() in this state)
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(wxColour(100, 100, 120), 1));
    }

    draw_star(dc, cx, cy, r_outer, r_inner);
}

void StarButton::on_mouse_enter(wxMouseEvent&)
{
    m_hovered = true;
    Show();
    Refresh();
}

void StarButton::on_mouse_leave(wxMouseEvent&)
{
    m_hovered = false;
    if (!m_favorited)
        Hide();
    else
        Refresh();
}

void StarButton::on_left_up(wxMouseEvent&)
{
    // Toggle in the manager — the manager notifies all subscribers
    // (including FavoritesPanel) so we don't need to call set_favorited
    // ourselves; a subscriber will call it for us. But we update locally
    // immediately to keep the feel snappy.
    bool now_fav = FavoritesManager::get().toggle(m_opt_key);
    set_favorited(now_fav);

    // Force parent to refresh so the star redraws in correct state
    GetParent()->Refresh();

    wxCommandEvent evt(EVT_STAR_TOGGLED, GetId());
    evt.SetString(m_opt_key);
    evt.SetInt(now_fav ? 1 : 0);
    GetEventHandler()->ProcessEvent(evt);
}

} // namespace GUI
} // namespace Slic3r
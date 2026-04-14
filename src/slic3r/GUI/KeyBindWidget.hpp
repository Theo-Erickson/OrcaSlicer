#pragma once
#include <wx/panel.h>
#include <wx/button.h>
#include <functional>

namespace Slic3r { namespace GUI {

class KeyBindWidget : public wxPanel
{
public:
    using ChangeCallback = std::function<void(int keycode)>;

    KeyBindWidget(wxWindow* parent, int initial_keycode, ChangeCallback on_change);

    void set_keycode(int keycode);
    int  get_keycode() const { return m_keycode; }

    static wxString keycode_to_label(int keycode);

private:
    void enter_capture_mode();
    void exit_capture_mode(int keycode);  // 0 = ESC/cancelled

    wxButton*      m_btn;
    int            m_keycode  { 0 };
    bool           m_capturing{ false };
    ChangeCallback m_on_change;
};

}} // namespace Slic3r::GUI
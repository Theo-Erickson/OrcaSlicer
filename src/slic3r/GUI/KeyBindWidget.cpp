#include "KeyBindWidget.hpp"
#include <wx/sizer.h>
#include <wx/defs.h>
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

wxString KeyBindWidget::keycode_to_label(int keycode)
{
    if (keycode <= 0)          return ("[unbound]");
    switch (keycode) {
        case WXK_SPACE:            return L("Space");
        case WXK_RETURN:           return L("Enter");
        case WXK_TAB:              return L("Tab");
        case WXK_ESCAPE:           return L("Escape");
        case WXK_DELETE:           return L("Delete");
        case WXK_BACK:             return L("Backspace");
        case WXK_INSERT:           return L("Insert");
        case WXK_HOME:             return L("Home");
        case WXK_END:              return L("End");
        case WXK_PAGEUP:           return L("Page Up");
        case WXK_PAGEDOWN:         return L("Page Down");
        case WXK_UP:               return L("Up");
        case WXK_DOWN:             return L("Down");
        case WXK_LEFT:             return L("Left");
        case WXK_RIGHT:            return L("Right");
        case WXK_F1:               return L("F1");
        case WXK_F2:               return L("F2");
        case WXK_F3:               return L("F3");
        case WXK_F4:               return L("F4");
        case WXK_F5:               return L("F5");
        case WXK_F6:               return L("F6");
        case WXK_F7:               return L("F7");
        case WXK_F8:               return L("F8");
        case WXK_F9:               return L("F9");
        case WXK_F10:              return L("F10");
        case WXK_F11:              return L("F11");
        case WXK_F12:              return L("F12");
        default:
            // Printable ASCII range
            if (keycode >= 33 && keycode <= 126)
                return wxString((wchar_t)keycode);
            wxString outKey = wxString::Format(("Key(%d)"), keycode);
            return L(outKey.utf8_string());
        }
}

KeyBindWidget::KeyBindWidget(wxWindow* parent, int initial_keycode,
                             ChangeCallback on_change)
    : wxPanel(parent, wxID_ANY)
    , m_keycode(initial_keycode)
    , m_on_change(std::move(on_change))
{
    m_btn = new wxButton(this, wxID_ANY, keycode_to_label(m_keycode),
                         wxDefaultPosition, wxSize(120, -1));

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_btn, 0);
    SetSizer(sizer);

    m_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        enter_capture_mode();
    });

    m_btn->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& evt) {
        if (!m_capturing) { evt.Skip(); return; }

        int key = evt.GetKeyCode();

        // Ignore bare modifier keypresses — wait for a real key
        if (key == WXK_SHIFT   || key == WXK_ALT     ||
            key == WXK_CONTROL || key == WXK_WINDOWS_LEFT ||
            key == WXK_WINDOWS_RIGHT) {
            evt.Skip();
            return;
        }

        if (key == WXK_ESCAPE)
            exit_capture_mode(0);   // cancelled — keep old value
        else
            exit_capture_mode(key);
    });

    // If the button loses focus while capturing, cancel
    m_btn->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& evt) {
        if (m_capturing)
            exit_capture_mode(0);
        evt.Skip();
    });
}

void KeyBindWidget::set_keycode(int keycode)
{
    m_keycode = keycode;
    m_btn->SetLabel(keycode_to_label(keycode));
}

void KeyBindWidget::enter_capture_mode()
{
    m_capturing = true;
    m_btn->SetLabel(L("Press a key... (Esc to cancel)"));
    m_btn->SetFocus();
}

void KeyBindWidget::exit_capture_mode(int keycode)
{
    m_capturing = false;
    if (keycode != 0) {
        m_keycode = keycode;
        if (m_on_change)
            m_on_change(m_keycode);
    }
    m_btn->SetLabel(keycode_to_label(m_keycode));
}

}} // namespace Slic3r::GUI
#include "KeyBindWidget.hpp"
#include "KeybindRegistry.hpp"
#include "slic3r/GUI/I18N.hpp"
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
            return wxString::Format("Key(%d)", keycode);
    }
}

KeyBindWidget::KeyBindWidget(wxWindow* parent, const std::string& action_id,
                             int initial_keycode, ChangeCallback on_change)
    : wxPanel(parent, wxID_ANY)
    , m_action_id(action_id)
    , m_keycode(initial_keycode)
    , m_on_change(std::move(on_change))
{
    // Check if already modified from default at construction time
    const KeybindEntry* e = KeybindRegistry::get().find_action(action_id);
    if (e) {
        m_is_modified = (e->keycode != e->default_keycode ||
                         e->modifier != e->default_modifier);
    }
    
    // Pass along the locked state from the entry into the widget member
    m_locked = e->is_locked;

    // Button that you click on to start capturing the keybind
    m_btn = new wxButton(this, wxID_ANY, keycode_to_label(m_keycode),
                         wxDefaultPosition, wxSize(FromDIP(90), -1));

    // show a lock icon and disable the button for locked entries
    if (m_locked) {
        m_btn->SetToolTip(_L("This keybind is hardcoded and cannot be changed"));
        m_btn->Enable(false);
        // Optionally prepend a lock glyph — works on all platforms without image loading:
        m_btn->SetLabel(wxString::FromUTF8("\xf0\x9f\x94\x92 ") + keycode_to_label(m_keycode));
    }

    m_conflict_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_conflict_label->SetForegroundColour(*wxRED);
    m_conflict_label->Hide();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_btn, 0);
    sizer->Add(m_conflict_label, 0, wxTOP, 2);
    SetSizer(sizer);

    update_modified_appearance();

    m_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_locked)          
            enter_capture_mode();
    });

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& evt) {
        if (!m_capturing) { evt.Skip(); return; }

        int key = evt.GetKeyCode();
        if (key == WXK_SHIFT || key == WXK_ALT ||
            key == WXK_CONTROL || key == WXK_WINDOWS_LEFT ||
            key == WXK_WINDOWS_RIGHT) {
            evt.Skip(); return;
        }

        if (key == WXK_ESCAPE) {
            m_conflict_label->Hide();
            m_has_conflict = false;
            exit_capture_mode(0);
            return;
        }

        KeybindModifier mod = KeybindModifier::None;
        if (evt.ControlDown()) mod = mod | KeybindModifier::Ctrl;
        if (evt.ShiftDown())   mod = mod | KeybindModifier::Shift;
        m_pending_modifier = mod;

        handle_new_key(key, mod);
    });

    m_btn->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& evt) {
        if (m_capturing) {
            m_conflict_label->Hide();
            m_has_conflict = false;
            exit_capture_mode(0);
        }
        evt.Skip();
    });
}

int KeyBindWidget::update_conflict(int keycode, KeybindModifier mod)
{
    const wxString* conflict = KeybindRegistry::get().find_conflict(keycode, mod);
    if (conflict) {
        m_conflict_label->SetLabel(
            wxString::Format(_L("Already used by: %s"), *conflict));
        m_conflict_label->Show();
        m_has_conflict = true;
        if (m_enforce_noConflicts) // if the widget is set to enforce no conflicts
        {
            keycode = 0;
            set_keycode(keycode); // set code to null 
        }
    } else {
        m_conflict_label->Hide();
        m_has_conflict = false;
    }
    // Re-layout so the label appears/disappears cleanly
    if (auto* p = GetParent(); p != nullptr)
        p->Layout();
    return keycode;
}

void KeyBindWidget::set_keycode(int keycode)
{
    m_keycode = keycode;
    m_btn->SetLabel(keycode_to_label(keycode));
    m_btn->SetMinSize(wxSize(FromDIP(90), -1));
    m_btn->GetParent()->Layout();
}

void KeyBindWidget::enter_capture_mode()
{
    // early exit if the widget is locked
    if (m_locked) return;
    
    m_capturing = true;
    m_conflict_label->Hide();
    m_has_conflict = false;
    m_btn->SetLabel(_L("Press a key..."));
    // Give it a highlighted border so it's obvious it's listening
    m_btn->SetBackgroundColour(wxColour(200, 230, 255));
    m_btn->SetFocus();
}

void KeyBindWidget::exit_capture_mode(int keycode)
{
    m_capturing = false;
    m_btn->SetBackgroundColour(wxNullColour);
    if (keycode != 0) {
        m_keycode  = keycode;
        m_modifier = m_pending_modifier;

        // Check if now differs from default
        const KeybindEntry* e = KeybindRegistry::get().find_action(m_action_id);
        if (e)
            m_is_modified = (keycode != e->default_keycode ||
                             m_modifier != e->default_modifier);

        if (m_on_change)
            m_on_change(m_keycode, m_modifier);
    }
    m_btn->SetLabel(keycode_to_label(m_keycode));
    m_btn->SetMinSize(wxSize(FromDIP(90), -1));
    update_modified_appearance();
}

void KeyBindWidget::handle_new_key(int key, KeybindModifier mod)
{
    auto& reg = KeybindRegistry::get();
    std::string conflict_action = reg.find_conflict_action(key, mod, m_action_id);

    auto conflict_label_for = [&](const std::string& action_id) -> wxString {
        const KeybindEntry* e = reg.find_action(action_id);
        return e ? e->label : wxString::FromUTF8(action_id);
    };

    switch (reg.get_conflict_mode()) {

    case KeybindConflictMode::Ignore:
        // Just apply — don't care about conflicts
        update_conflict(key, mod);  // still show label for info
        exit_capture_mode(key);
        break;

    case KeybindConflictMode::Warn:
        if (!conflict_action.empty()) {
            wxString conflict_name = conflict_label_for(conflict_action);
            wxString msg = wxString::Format(
                _L("'%s' is already used by '%s'.\n\nUnbind '%s' and assign here?"),
                keycode_to_label(key), conflict_name, conflict_name);
            int res = wxMessageBox(msg, _L("Keybind conflict"),
                                   wxYES_NO | wxCANCEL | wxICON_WARNING, this);
            if (res == wxYES) {
                // Unbind conflicting action FIRST so remap() succeeds
                KeybindRegistry::get().unbind(conflict_action);
                // Now remap will find the key free
                update_conflict(key, mod);
                exit_capture_mode(key);
                // Notify panel to refresh unbound row
                wxCommandEvent ev(wxEVT_COMMAND_TEXT_UPDATED, GetId());
                ev.SetString(wxString::FromUTF8(conflict_action));
                wxPostEvent(GetParent(), ev);
            } else if (res == wxNO) {
                m_conflict_label->Hide();
                m_has_conflict = false;
                exit_capture_mode(0);
            }
            // wxCANCEL stays in capture mode
        } else {
            update_conflict(key, mod);
            exit_capture_mode(key);
        }
        break;

    case KeybindConflictMode::Disallow:
        if (!conflict_action.empty()) {
            wxString conflict_name = conflict_label_for(conflict_action);
            m_conflict_label->SetLabel(
                wxString::Format(_L("Conflict: already used by '%s'"),
                                 conflict_name));
            m_conflict_label->Show();
            m_has_conflict = true;
            if (auto* p = GetParent(); p) p->Layout();
            // Don't call exit_capture_mode — stay capturing
            // Show the key they pressed on the button temporarily
            wxString mod_str;
            if (mod & KeybindModifier::Ctrl)  mod_str += "Ctrl+";
            if (mod & KeybindModifier::Shift) mod_str += "Shift+";
            m_btn->SetLabel(mod_str + keycode_to_label(key) + " (conflict)");
        } else {
            update_conflict(key, mod);
            exit_capture_mode(key);
        }
        break;
    }
}

void KeyBindWidget::update_modified_appearance()
{
    // Change button text color if modified from default
    if (m_is_modified) {
        m_btn->SetForegroundColour(wxColour(100, 180, 255)); // blue tint
    } else {
        m_btn->SetForegroundColour(wxNullColour);
    }
    m_btn->Refresh();
}

void KeyBindWidget::set_modified(bool modified)
{
    m_is_modified = modified;
    update_modified_appearance();
}

}} // namespace Slic3r::GUI
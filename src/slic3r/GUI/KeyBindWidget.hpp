#pragma once
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <functional>

#include "KeybindRegistry.hpp"

namespace Slic3r { namespace GUI {
enum class KeybindModifier : uint8_t;

class KeyBindWidget : public wxPanel
{
public:
    using ChangeCallback = std::function<void(int keycode, KeybindModifier mod)>;

    KeyBindWidget(wxWindow* parent, const std::string& action_id,
                  int initial_keycode, ChangeCallback on_change);
    
    void set_keycode(int keycode);
    int  get_keycode() const { return m_keycode; }
    bool has_conflict() const { return m_has_conflict; }
    
    void set_modified(bool modified);

    static wxString keycode_to_label(int keycode);

private:
    void enter_capture_mode();
    void exit_capture_mode(int keycode);  // 0 = ESC/cancelled
    void handle_new_key(int key, KeybindModifier mod);
    void update_modified_appearance();
    int  update_conflict(int keycode, KeybindModifier mod);

    wxButton*      m_btn;
    wxStaticText*  m_conflict_label { nullptr };
    int            m_keycode        { 0 };
    bool           m_capturing      { false };
    bool           m_has_conflict   { false };
    ChangeCallback m_on_change;
    // If true, attempting to set the keycode to an existing keybind will unbind the key
    bool           m_enforce_noConflicts { false };
    KeybindModifier m_modifier { KeybindModifier::None };
    
    std::string m_action_id;
    bool        m_is_modified { false };  // true if differs from default
    KeybindModifier         m_pending_modifier;
};

}} // namespace Slic3r::GUI
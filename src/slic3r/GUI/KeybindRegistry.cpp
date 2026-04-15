#include "KeybindRegistry.hpp"
#include "I18N.hpp"
#include <wx/defs.h>
#include <algorithm>

namespace Slic3r {
namespace GUI {

KeybindRegistry& KeybindRegistry::get()
{
    static KeybindRegistry instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

const wxString* KeybindRegistry::find_conflict(int keycode,
                                                KeybindModifier mod) const
{
    auto it = m_by_key.find({keycode, mod});
    if (it == m_by_key.end()) return nullptr;
    auto it2 = m_by_action.find(it->second);
    if (it2 == m_by_action.end()) return nullptr;
    return &it2->second.label;
}

std::string KeybindRegistry::lookup(int keycode, KeybindModifier mod) const
{
    auto it = m_by_key.find({keycode, mod});
    return it != m_by_key.end() ? it->second : "";
}

const KeybindEntry* KeybindRegistry::find_action(
    const std::string& action_id) const
{
    auto it = m_by_action.find(action_id);
    return it != m_by_action.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Remapping
// ---------------------------------------------------------------------------

bool KeybindRegistry::remap(const std::string& action_id,
                             int keycode, KeybindModifier mod)
{
    auto it = m_by_action.find(action_id);
    if (it == m_by_action.end()) return false;

    KeybindEntry& entry = it->second;

    // Check if target key is taken by a DIFFERENT action
    auto conflict_it = m_by_key.find({keycode, mod});
    if (conflict_it != m_by_key.end() &&
        conflict_it->second != action_id) {
        // Key is taken — caller must unbind the other first
        return false;
        }

    // Safe to remap — remove old key mapping
    if (entry.keycode != 0)
        m_by_key.erase({entry.keycode, entry.modifier});

    entry.keycode  = keycode;
    entry.modifier = mod;
    if (keycode != 0)
        m_by_key[{keycode, mod}] = action_id;

    return true;
}

void KeybindRegistry::reset_action(const std::string& action_id)
{
    auto it = m_by_action.find(action_id);
    if (it == m_by_action.end()) return;

    KeybindEntry& entry = it->second;

    // Remove current key mapping
    m_by_key.erase({entry.keycode, entry.modifier});

    // If the default key is now occupied by something else, we can't restore.
    // In practice this shouldn't happen for built-ins but handle it gracefully.
    auto conflict = m_by_key.find({entry.default_keycode,
                                    entry.default_modifier});
    if (conflict == m_by_key.end()) {
        entry.keycode   = entry.default_keycode;
        entry.modifier  = entry.default_modifier;
        m_by_key[{entry.keycode, entry.modifier}] = action_id;
    }
    // else: leave the action unbound — UI should show it as unbound
}

void KeybindRegistry::reset_all()
{
    m_by_key.clear();
    for (auto& [id, entry] : m_by_action) {
        entry.keycode  = entry.default_keycode;
        entry.modifier = entry.default_modifier;
        if (entry.keycode != 0)
            m_by_key[{entry.keycode, entry.modifier}] = id;
    }
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

std::vector<const KeybindEntry*> KeybindRegistry::all_sorted() const
{
    std::vector<const KeybindEntry*> result;
    result.reserve(m_by_action.size());
    for (const auto& [id, entry] : m_by_action)
        result.push_back(&entry);

    std::sort(result.begin(), result.end(),
        [](const KeybindEntry* a, const KeybindEntry* b) {
            if (a->category != b->category)
                return a->category < b->category;
            return a->label < b->label;
        });
    return result;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void KeybindRegistry::register_builtin(int keycode, 
                                        KeybindModifier mod,
                                        const std::string& action_id,
                                        const wxString& label,
                                        const wxString& category,
                                        const wxString& description)
{
    KeybindEntry entry;
    entry.action_id        = action_id;
    entry.label            = label;
    entry.category         = category;
    entry.description      = description;
    entry.keycode          = keycode;
    entry.modifier         = mod;
    entry.default_keycode  = keycode;
    entry.default_modifier = mod;

    m_by_action[action_id]    = entry;
    if (keycode != 0)
        m_by_key[{keycode, mod}] = action_id;
}

void KeybindRegistry::register_default_canvas_keybinds()
{
    auto& reg = KeybindRegistry::get();

    // Edit
    reg.register_builtin('Z', KeybindModifier::Ctrl, "undo",       _L("Undo"),       _L("Edit"), _L("Undo the last action"));
    reg.register_builtin('Y', KeybindModifier::Ctrl, "redo",       _L("Redo"),       _L("Edit"), _L("Redo the previously undone action"));
    reg.register_builtin('C', KeybindModifier::None, "copy",       _L("Copy"),       _L("Edit"), _L("Copy selected objects to clipboard"));
    reg.register_builtin('V', KeybindModifier::None, "paste",      _L("Paste"),      _L("Edit"), _L("Paste objects from clipboard"));
    reg.register_builtin('X', KeybindModifier::None, "cut",        _L("Cut"),        _L("Edit"), _L("Cut selected objects to clipboard"));
    reg.register_builtin('D', KeybindModifier::None, "delete_all", _L("Delete all"), _L("Edit"), _L("Remove all objects from the current plate"));
    reg.register_builtin('K', KeybindModifier::Ctrl, "clone",      _L("Clone"),      _L("Edit"), _L("Duplicate selected objects"));

    // Objects
    reg.register_builtin(WXK_DELETE, KeybindModifier::None, "delete",         _L("Delete"),          _L("Objects"), _L("Delete selected objects"));
    reg.register_builtin('+',        KeybindModifier::None, "increase_instances", _L("Add instance"),    _L("Objects"), _L("Add another instance of the selected object"));
    reg.register_builtin('-',        KeybindModifier::None, "decrease_instances", _L("Remove instance"), _L("Objects"), _L("Remove one instance of the selected object"));

    // Navigation
    reg.register_builtin(WXK_TAB,    KeybindModifier::None,  "collapse_sidebar",  _L("Collapse sidebar"),      _L("Navigation"), _L("Toggle the right-side settings panel"));
    reg.register_builtin(WXK_TAB,    KeybindModifier::Shift, "switch_3d_preview", _L("Switch 3D / Preview"),   _L("Navigation"), _L("Switch between 3D editor and preview mode"));
    reg.register_builtin(WXK_SPACE,  KeybindModifier::None,  "transform_cycle",   _L("Cycle transform tools"), _L("Navigation"), _L("Cycle between Move, Rotate, and Scale gizmos"));
    reg.register_builtin(WXK_F5,     KeybindModifier::None,  "reload_printer_url", _L("Reload printer URL"),   _L("Navigation"), _L("Reload the connected printer's URL"));
    reg.register_builtin('?',        KeybindModifier::None,  "show_shortcuts",    _L("Show shortcuts"),        _L("Navigation"), _L("Display keyboard shortcut reference"));
    reg.register_builtin('I',        KeybindModifier::None,  "zoom_in",           _L("Zoom in"),               _L("Navigation"), _L("Zoom the camera in"));
    reg.register_builtin('O',        KeybindModifier::None,  "zoom_out",          _L("Zoom out"),              _L("Navigation"), _L("Zoom the camera out"));
    reg.register_builtin(WXK_LEFT,   KeybindModifier::None,  "move_left",         _L("Move left (-X)"),        _L("Navigation"), _L("Nudge selected object in the -X direction"));
    reg.register_builtin(WXK_RIGHT,  KeybindModifier::None,  "move_right",        _L("Move right (+X)"),       _L("Navigation"), _L("Nudge selected object in the +X direction"));
    reg.register_builtin(WXK_UP,     KeybindModifier::None,  "move_up",           _L("Move up (+Y)"),          _L("Navigation"), _L("Nudge selected object in the +Y direction"));
    reg.register_builtin(WXK_DOWN,   KeybindModifier::None,  "move_down",         _L("Move down (-Y)"),        _L("Navigation"), _L("Nudge selected object in the -Y direction"));
    reg.register_builtin(WXK_PAGEUP, KeybindModifier::None,  "rotate_plus_45",    _L("Rotate +45 deg"),        _L("Navigation"), _L("Rotate selected object 45 degrees counter-clockwise"));
    reg.register_builtin(WXK_PAGEDOWN, KeybindModifier::None,"rotate_minus_45",   _L("Rotate -45 deg"),        _L("Navigation"), _L("Rotate selected object 45 degrees clockwise"));

    // View
    reg.register_builtin('A', KeybindModifier::None,  "arrange",           _L("Arrange"),              _L("View"), _L("Auto-arrange all objects on the plate"));
    reg.register_builtin('A', KeybindModifier::Shift, "arrange_partplate", _L("Arrange plate"),        _L("View"), _L("Auto-arrange objects on the current plate only"));
    reg.register_builtin('Q', KeybindModifier::None,  "orient",            _L("Orient"),               _L("View"), _L("Auto-orient all objects for optimal printing"));
    reg.register_builtin('Q', KeybindModifier::Shift, "orient_partplate",  _L("Orient plate"),         _L("View"), _L("Auto-orient objects on the current plate only"));
    reg.register_builtin('C', KeybindModifier::None,  "toggle_gcode_window", _L("Toggle G-code window"), _L("View"), _L("Show or hide the G-code when within preview window"));

    // Preview
    reg.register_builtin(WXK_HOME,   KeybindModifier::None, "preview_first_move", _L("First move"),  _L("Preview"), _L("Jump to the first toolpath move"));
    reg.register_builtin(WXK_END,    KeybindModifier::None, "preview_last_move",  _L("Last move"),   _L("Preview"), _L("Jump to the last toolpath move"));
}

} // namespace GUI
} // namespace Slic3r
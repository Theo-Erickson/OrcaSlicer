#pragma once
#include <wx/string.h>
#include <unordered_map>
#include <string>

namespace Slic3r {
namespace GUI {

enum class KeybindModifier : uint8_t {
    None  = 0,
    Ctrl  = 1 << 0,
    Shift = 1 << 1,
    Alt   = 1 << 2,
};

inline KeybindModifier operator|(KeybindModifier a, KeybindModifier b) {
    return static_cast<KeybindModifier>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(KeybindModifier a, KeybindModifier b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

struct KeybindEntry {
    std::string     action_id;
    // User accessible label
    wxString        label;
    // Used for UI categorization
    wxString        category;
    // on hover tooltip
    wxString        description;
    // Current (possibly remapped) binding
    int             keycode         { 0 };
    KeybindModifier modifier        { KeybindModifier::None };
    // Original default: never changes after registration
    int             default_keycode { 0 };
    KeybindModifier default_modifier{ KeybindModifier::None };
    // whether a given keybind should allow re-binding. If true, the action is likely hardcoded
    bool            is_locked       { false };
};

struct KeybindKey {
    int             keycode;
    KeybindModifier modifier;

    bool operator==(const KeybindKey& o) const {
        return keycode == o.keycode && modifier == o.modifier;
    }
};

struct KeybindKeyHash {
    size_t operator()(const KeybindKey& k) const {
        return std::hash<int>()(k.keycode) ^
               (std::hash<uint8_t>()(static_cast<uint8_t>(k.modifier)) << 16);
    }
};

enum class KeybindConflictMode {
    Ignore   = 0,
    Warn     = 1,
    Disallow = 2,
};

class KeybindRegistry {
public:
    static KeybindRegistry& get();

    // --- Query by key ---
    // Returns nullptr if nothing is bound to this key
    const wxString* find_conflict(int keycode, KeybindModifier mod) const;
    // Returns action_id or "" if unbound
    std::string lookup(int keycode, KeybindModifier mod) const;

    // --- Query by action ---
    // Returns nullptr if action_id not found
    const KeybindEntry* find_action(const std::string& action_id) const;

    // --- Remapping ---
    // Remap an action to a new key. Returns false if the key is already taken.
    bool remap(const std::string& action_id, int keycode, KeybindModifier mod);
    // Reset a single action to its default
    void reset_action(const std::string& action_id);
    // Reset every action to its default
    void reset_all();

    // --- Iteration (for UI / serialization) ---
    // Returns all entries sorted by category then label
    std::vector<const KeybindEntry*> all_sorted() const;

    // --- Registration (call once at startup) ---
    void register_builtin(int keycode, KeybindModifier mod,
                          const std::string& action_id,
                          const wxString& label,
                          const wxString& category = wxString("General"),
                          const wxString& description = wxString(""),
                          const bool isLocked = false);

    static void register_default_canvas_keybinds();
    
    KeybindConflictMode get_conflict_mode() const { return m_conflict_mode; }
    void set_conflict_mode(KeybindConflictMode mode) { m_conflict_mode = mode; }

    // Returns the action_id that holds this key, excluding the given action
    std::string find_conflict_action(int keycode, KeybindModifier mod,
                                     const std::string& exclude_action) const {
        auto it = m_by_key.find({keycode, mod});
        if (it == m_by_key.end()) return "";
        if (it->second == exclude_action) return "";
        return it->second;
    }

    // Unbind an action (set keycode to 0)
    void unbind(const std::string& action_id) {
        auto it = m_by_action.find(action_id);
        if (it == m_by_action.end()) return;
        m_by_key.erase({it->second.keycode, it->second.modifier});
        it->second.keycode  = 0;
        it->second.modifier = KeybindModifier::None;
    }

private:
    KeybindRegistry() = default;

    // Two maps kept in sync:
    // action_id  → entry  (owns the data)
    // KeybindKey → action_id  (fast lookup by key)
    std::unordered_map<std::string, KeybindEntry>      m_by_action;
    std::unordered_map<KeybindKey, std::string,
                       KeybindKeyHash>                  m_by_key;
    
    KeybindConflictMode m_conflict_mode { KeybindConflictMode::Warn };
};

} // namespace GUI
} // namespace Slic3r
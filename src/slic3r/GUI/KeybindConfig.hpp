#pragma once
#include "KeybindRegistry.hpp"
#include <wx/string.h>

namespace Slic3r {
namespace GUI {

// Handles persistence (load/save), import and export of keybindings.
// Sits on top of KeybindRegistry — always keeps them in sync.
class KeybindConfig {
public:
    static KeybindConfig& get();

    // Call after register_default_canvas_keybinds().
    // Loads keybinds.json from the user data dir and overlays any saved remaps.
    void load();

    // Writes current state to keybinds.json.
    void save() const;

    // Remap one action. Returns false if the key is already taken.
    bool set(const std::string& action_id, int keycode, KeybindModifier mod);

    // Reset one action and save.
    void reset_action(const std::string& action_id);

    // Reset everything to defaults and delete the saved file.
    void reset_all();

    // Export current bindings.
    // format: "json" or "ini"
    bool export_to_file(const wxString& path, const std::string& format) const;

    // Import bindings from file, auto-detecting format from extension.
    // Returns a human-readable error string, or "" on success.
    wxString import_from_file(const wxString& path);

    static wxString default_save_path();

    void set_conflict_mode(KeybindConflictMode mode)
    {
        KeybindRegistry::get().set_conflict_mode(mode);
        save();
    }
    
private:
    KeybindConfig() = default;

    // Serialization helpers
    bool save_json(const wxString& path) const;
    bool save_ini (const wxString& path) const;
    wxString load_json(const wxString& path);
    wxString load_ini (const wxString& path);
};

} // namespace GUI
} // namespace Slic3r
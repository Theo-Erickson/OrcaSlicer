#include "KeybindConfig.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"   // data_dir()

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/log.h>

// nlohmann/json is already vendored in OrcaSlicer
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace Slic3r {
namespace GUI {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string modifier_to_string(KeybindModifier mod)
{
    std::string s;
    if (mod & KeybindModifier::Ctrl)  s += "ctrl+";
    if (mod & KeybindModifier::Shift) s += "shift+";
    if (mod & KeybindModifier::Alt)   s += "alt+";
    return s;
}

static KeybindModifier string_to_modifier(const std::string& s)
{
    KeybindModifier mod = KeybindModifier::None;
    if (s.find("ctrl+")  != std::string::npos) mod = mod | KeybindModifier::Ctrl;
    if (s.find("shift+") != std::string::npos) mod = mod | KeybindModifier::Shift;
    if (s.find("alt+")   != std::string::npos) mod = mod | KeybindModifier::Alt;
    return mod;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

KeybindConfig& KeybindConfig::get()
{
    static KeybindConfig instance;
    return instance;
}

wxString KeybindConfig::default_save_path()
{
    return wxString::FromUTF8(
        (boost::filesystem::path(Slic3r::data_dir()) / "keybinds.json")
        .string());
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void KeybindConfig::load()
{
    wxString path = default_save_path();
    if (!wxFileName::FileExists(path))
        return; // no saved file — defaults stand

    wxString err = load_json(path);
    if (!err.empty())
        wxLogWarning("KeybindConfig: failed to load %s: %s",
                     path, err);
}

void KeybindConfig::save() const
{
    if (!save_json(default_save_path()))
        wxLogWarning("KeybindConfig: failed to save keybinds.json");
}

// KeybindConfig::set should attempt unbind of conflicting key first
// since handle_new_key already got user confirmation:
bool KeybindConfig::set(const std::string& action_id,
                         int keycode, KeybindModifier mod)
{
    auto& reg = KeybindRegistry::get();
    // If something else holds this key, unbind it first
    // (caller has already shown conflict UI and gotten approval)
    std::string conflict = reg.find_conflict_action(keycode, mod, action_id);
    if (!conflict.empty())
        reg.unbind(conflict);
    return reg.remap(action_id, keycode, mod);
}

void KeybindConfig::reset_action(const std::string& action_id)
{
    KeybindRegistry::get().reset_action(action_id);
    save();
}

void KeybindConfig::reset_all()
{
    KeybindRegistry::get().reset_all();
    // Delete the saved file so there's no stale data
    wxString path = default_save_path();
    if (wxFileName::FileExists(path))
        wxRemoveFile(path);
}

bool KeybindConfig::export_to_file(const wxString& path,
                                    const std::string& format) const
{
    if (format == "json") return save_json(path);
    if (format == "ini")  return save_ini(path);
    return false;
}

wxString KeybindConfig::import_from_file(const wxString& path)
{
    wxString ext = wxFileName(path).GetExt().Lower();
    wxString err;
    if (ext == "json")
        err = load_json(path);
    else if (ext == "ini")
        err = load_ini(path);
    else
        err = _L("Unknown file format (expected .json or .ini)");

    if (err.empty())
        save(); // persist the imported bindings
    return err;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

bool KeybindConfig::save_json(const wxString& path) const
{
    json root;
    root["version"] = 1;
    json bindings = json::array();

    root["conflict_mode"] = static_cast<int>(KeybindRegistry::get().get_conflict_mode());
    
    for (const KeybindEntry* e :
         KeybindRegistry::get().all_sorted()) {
        // Only write entries that differ from their default
        if (e->keycode   == e->default_keycode &&
            e->modifier  == e->default_modifier)
            continue;

        json entry;
        entry["action"]   = e->action_id;
        entry["keycode"]  = e->keycode;
        entry["modifier"] = modifier_to_string(e->modifier);
        bindings.push_back(entry);
    }
    root["bindings"] = bindings;

    try {
        std::ofstream f(path.ToStdString());
        f << root.dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

wxString KeybindConfig::load_json(const wxString& path)
{
    try {
        std::ifstream f(path.ToStdString());
        if (!f.is_open())
            return _L("Could not open file");

        json root;
        f >> root;

        if (!root.contains("bindings"))
            return _L("Missing 'bindings' key");

        if (root.contains("conflict_mode")) {
            int mode = root["conflict_mode"].get<int>();
            if (mode >= 0 && mode <= 2)
                KeybindRegistry::get().set_conflict_mode(
                    static_cast<KeybindConflictMode>(mode));
        }
        
        auto& reg = KeybindRegistry::get();
        for (const auto& entry : root["bindings"]) {
            std::string action   = entry.value("action",   "");
            int         keycode  = entry.value("keycode",  0);
            std::string mod_str  = entry.value("modifier", "");
            KeybindModifier mod  = string_to_modifier(mod_str);

            if (action.empty() || keycode == 0) continue;

            if (!reg.remap(action, keycode, mod)) {
                // Conflict or unknown action — skip silently,
                // the user will see it hasn't changed in the UI
                wxLogWarning("KeybindConfig: skipped '%s' (conflict or unknown)",
                             action);
            }
        }
        return ""; // success
    } catch (const std::exception& ex) {
        return wxString::FromUTF8(ex.what());
    }
}

// ---------------------------------------------------------------------------
// INI
// ---------------------------------------------------------------------------

bool KeybindConfig::save_ini(const wxString& path) const
{
    std::ostringstream ss;
    ss << "# OrcaSlicer keybinds\n";
    ss << "# Format: action = modifier keycode\n\n";

    wxString current_cat;
    for (const KeybindEntry* e :
         KeybindRegistry::get().all_sorted()) {
        if (e->category != current_cat) {
            current_cat = e->category;
            ss << "\n[" << current_cat.ToUTF8() << "]\n";
        }
        ss << e->action_id << " = "
           << modifier_to_string(e->modifier)
           << e->keycode << "\n";
    }

    try {
        std::ofstream f(path.ToStdString());
        f << ss.str();
        return f.good();
    } catch (...) {
        return false;
    }
}

wxString KeybindConfig::load_ini(const wxString& path)
{
    try {
        std::ifstream f(path.ToStdString());
        if (!f.is_open())
            return _L("Could not open file");

        auto& reg = KeybindRegistry::get();
        std::string line;
        while (std::getline(f, line)) {
            // Strip comments and section headers
            if (line.empty() || line[0] == '#' || line[0] == '[')
                continue;

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string action = line.substr(0, eq);
            std::string value  = line.substr(eq + 1);

            // Trim whitespace
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t"));
                s.erase(s.find_last_not_of(" \t") + 1);
            };
            trim(action);
            trim(value);

            // value = "ctrl+shift+65"  — modifier prefix + raw keycode int
            KeybindModifier mod = string_to_modifier(value);
            // Strip modifier prefix to get the keycode number
            size_t pos = value.rfind('+');
            std::string keycode_str = (pos != std::string::npos)
                                      ? value.substr(pos + 1)
                                      : value;
            int keycode = 0;
            try { keycode = std::stoi(keycode_str); }
            catch (...) { continue; }

            if (!reg.remap(action, keycode, mod))
                wxLogWarning("KeybindConfig INI: skipped '%s'", action);
        }
        return "";
    } catch (const std::exception& ex) {
        return wxString::FromUTF8(ex.what());
    }
}

} // namespace GUI
} // namespace Slic3r
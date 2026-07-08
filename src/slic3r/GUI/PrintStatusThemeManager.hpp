#pragma once
// PrintStatusThemeManager.hpp
//
// Singleton that owns the print-status icon theme system.
//
// FOLDER LAYOUT (all under data_dir()):
//
//   print_status_themes/
//       mytheme/
//           theme.json          ← manifest (name, author, description)
//           status_idle.gif
//           status_running.gif
//           ... (any subset of the 12 states)
//       scifi/
//           theme.json
//           status_idle.gif
//           ...
//
// The active theme name is persisted in AppConfig under key
// "print_status_active_theme".  The built-in "Default" theme is never
// stored on disk — it falls back to PrintStatusIconGIF.hpp embedded bytes.
//
// ZIP IMPORT / EXPORT:
//   Uses wxZipInputStream / wxZipOutputStream (bundled with wxWidgets —
//   no new dependencies).  A valid theme zip contains theme.json at its
//   root plus any number of status_*.gif / status_*.png files.
//
// THREAD SAFETY:
//   All public methods must be called from the main thread only.
//   (Same contract as AppConfig itself.)

#include <wx/wx.h>
#include <wx/filename.h>

#include <string>
#include <vector>
#include <unordered_map>

#include "PrintStatusIcon.hpp"   // for PrintState enum

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// ThemeInfo — lightweight metadata for one installed theme
// ---------------------------------------------------------------------------
struct ThemeInfo {
    std::string id;           // folder name, used as unique key
    std::string display_name; // from theme.json "name" field (falls back to id)
    std::string author;       // optional
    std::string description;  // optional
    wxFileName  root_dir;     // absolute path to the theme folder
    bool        is_bundled { false };  // true = lives in resources/themes/
    bool        is_locked  { false };  // UI editing blocked, user can unlock
 
    // "Default" uses embedded bytes, no folder
    bool is_builtin() const { return id == "Default"; }
};

// ---------------------------------------------------------------------------
// PrintStatusThemeManager
// ---------------------------------------------------------------------------
class PrintStatusThemeManager
{
public:
    // Singleton access.
    static PrintStatusThemeManager& Get();

    // Call once at startup (after AppConfig is ready).
    // Scans data_dir()/print_status_themes/ and loads the active theme name.
    void Init();

    // ── Theme list ──────────────────────────────────────────────────────────

    // All installed themes, including "Default" at index 0.
    const std::vector<ThemeInfo>& Themes() const { return m_themes; }

    // Index of the currently active theme in Themes().
    int ActiveIndex() const { return m_active_index; }

    // ThemeInfo for the active theme.
    const ThemeInfo& ActiveTheme() const { return m_themes[m_active_index]; }

    // ── Activation ──────────────────────────────────────────────────────────

    // Switch to the theme at the given index.  Persists to AppConfig.
    // Call PrintStatusIcon::ForceRefresh() after this.
    void SetActiveTheme(int index);

    // Convenience: switch by theme id string.
    void SetActiveThemeById(const std::string& id);

    // ── Asset resolution ────────────────────────────────────────────────────

    // Returns the absolute path to the GIF/PNG for a given state in the
    // active theme, or wxEmptyString if the theme has no override for that
    // state (caller should fall back to embedded bytes).
    wxString Resolve(PrintState state) const;

    // Same but for an arbitrary theme (used by the preview panel).
    wxString ResolveForTheme(const ThemeInfo& theme, PrintState state) const;

    // ── Import / Export ─────────────────────────────────────────────────────

    // Import a zip file.  Extracts into print_status_themes/<theme_id>/.
    // Returns true on success.  Sets err_msg on failure.
    bool ImportZip(const wxString& zip_path, wxString& err_msg, bool rescan = true,  bool switch_to_new = true);

    // Import a loose folder (copies it into print_status_themes/).
    bool ImportFolder(const wxString& folder_path, wxString& err_msg);

    // Export the theme at the given index to a zip file at out_path.
    bool ExportZip(int theme_index, const wxString& out_path, wxString& err_msg);

    // ── Theme management ────────────────────────────────────────────────────

    // Create a new empty theme folder with a stub theme.json.
    // Returns the index of the new theme, or -1 on failure.
    int CreateTheme(const std::string& name, const std::string& author,
                    const std::string& description, wxString& err_msg);

    // Delete a user theme by index (built-in "Default" cannot be deleted).
    bool DeleteTheme(int index, wxString& err_msg);
    
    // Create a theme based on an existing theme. Works as a copy or for testing changes
    bool DuplicateTheme(int src_index, const std::string& new_name,
                        const std::string& author,
                        const std::string& description,
                        wxString& err_msg);
    
    // Copy a single asset file into a theme's folder for a given state.
    // Accepts .gif or .png.  Overwrites if already present.
    bool SetStateAsset(int theme_index, PrintState state,
                       const wxString& src_path, wxString& err_msg);

    // Remove a state override from a theme (falls back to Default).
    bool ClearStateAsset(int theme_index, PrintState state, wxString& err_msg);

    // Re-scan the themes directory (call after external folder changes).
    void Rescan();

    // ── Helpers ─────────────────────────────────────────────────────────────

    // Canonical filename for a given state ("status_idle.gif", etc.)
    static wxString StateFilename(PrintState state);

    // Human-readable state name ("Idle", "Running", etc.)
    static wxString StateName(PrintState state);

    // All 12 states in display order.
    static const std::vector<PrintState>& AllStates();
    wxFileName                            ResourcesThemesDir();
    wxFileName                            UserThemesDir() const;

    // Absolute paths to the pristine factory-default theme zips bundled under
    // resources/.../themes/_DEFAULT BACKUP/.  Used by the "Restore Defaults"
    // UI to re-import factory themes (overwriting user copies of the same id).
    std::vector<wxFileName>               DefaultThemeZips();

    // Root folder for all themes.
    wxFileName ThemesDir() const { return m_themes_dir; }

    // Is the theme locked for read-only
    bool IsLocked(int index) const;
    
    // Change whether or not bundled themes can be edited
    void SetLocked(int index, bool locked);
    
private:
    PrintStatusThemeManager() = default;

    void ScanThemesDir();
    ThemeInfo LoadThemeInfo(const wxFileName& dir) const;
    void WriteThemeJson(const ThemeInfo& info) const;
    bool EnsureThemesDir(wxString& err_msg);
    static bool ExtractZipToDir(const wxString& zip_path,
                                const wxFileName& out_dir,
                                wxString* err_out);

    wxFileName              m_themes_dir; // alias kept for compatibility
    wxFileName              m_user_themes_dir;  // data_dir/print_status_themes/
    std::vector<ThemeInfo>  m_themes;       // index 0 is always "Default"
    int                     m_active_index  { 0 };
    bool                    m_initialized   { false };
};

} // namespace GUI
} // namespace Slic3r
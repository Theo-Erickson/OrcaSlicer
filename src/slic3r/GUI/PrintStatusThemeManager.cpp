#include "PrintStatusThemeManager.hpp"
 
#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/stdpaths.h>
 
#include <nlohmann/json.hpp>
#include "GUI_App.hpp"
#include "PrintStatusIconGIF.hpp"

namespace Slic3r {
namespace GUI {
 
using json = nlohmann::json;
 
static constexpr const char* CFG_KEY = "print_status_active_theme";
 
// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
PrintStatusThemeManager& PrintStatusThemeManager::Get()
{
    static PrintStatusThemeManager instance;
    return instance;
}
 
// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
wxString PrintStatusThemeManager::StateFilename(PrintState state)
{
    switch (state) {
    case PrintState::IDLE:            return "status_idle.gif";
    case PrintState::SLICING:         return "status_slicing.gif";
    case PrintState::SLICED:          return "status_sliced.gif";
    case PrintState::SENDING:         return "status_sending.gif";
    case PrintState::PREPARE:         return "status_prepare.gif";
    case PrintState::RUNNING:         return "status_running.gif";
    case PrintState::PAUSE:           return "status_pause.gif";
    case PrintState::FILAMENT_CHANGE: return "status_filament_change.gif";
    case PrintState::CALIBRATING:     return "status_calibrating.gif";
    case PrintState::FINISH:          return "status_finish.gif";
    case PrintState::FAILED:          return "status_failed.gif";
    case PrintState::OFFLINE:         return "status_offline.gif";
    case PrintState::HEATING:         return "status_heating.gif";
    case PrintState::LEVELING:        return "status_leveling.gif";
    case PrintState::ERROR_PAUSE:     return "status_error_pause.gif";    
    default:                          return "status_idle.gif";
    }
}
 
wxString PrintStatusThemeManager::StateName(PrintState state)
{
    switch (state) {
    case PrintState::IDLE:            return "Idle";
    case PrintState::SLICING:         return "Slicing";
    case PrintState::SLICED:          return "Sliced";
    case PrintState::SENDING:         return "Sending";
    case PrintState::PREPARE:         return "Preparing";
    case PrintState::RUNNING:         return "Running";
    case PrintState::PAUSE:           return "Paused";
    case PrintState::FILAMENT_CHANGE: return "Filament Change";
    case PrintState::CALIBRATING:     return "Calibrating";
    case PrintState::FINISH:          return "Finished";
    case PrintState::FAILED:          return "Failed";
    case PrintState::OFFLINE:         return "Offline";
    case PrintState::HEATING:         return "Heating";
    case PrintState::LEVELING:        return "Leveling";
    case PrintState::ERROR_PAUSE:     return "Error Pause";
    default:                          return "Unknown";
    }
}
 
const std::vector<PrintState>& PrintStatusThemeManager::AllStates()
{
    static const std::vector kAll = {
        PrintState::IDLE,
        PrintState::SLICING,
        PrintState::SLICED,
        PrintState::SENDING,
        PrintState::PREPARE,
        PrintState::RUNNING,
        PrintState::PAUSE,
        PrintState::FILAMENT_CHANGE,
        PrintState::CALIBRATING,
        PrintState::FINISH,
        PrintState::FAILED,
        PrintState::OFFLINE,
        PrintState::HEATING,
        PrintState::LEVELING,
        PrintState::ERROR_PAUSE
    };
    return kAll;
}
 
// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------
 
// Cross-platform path to <app>/resources/themes/
//   Windows: <exe_dir>/resources/themes/
//   macOS:   OrcaSlicer.app/Contents/Resources/themes/
//   Linux:   /usr/share/OrcaSlicer/themes/  (or wherever installed)
wxFileName PrintStatusThemeManager::ResourcesThemesDir()
{
    wxFileName dir(wxStandardPaths::Get().GetResourcesDir(), "");
    dir.AppendDir("images");
    dir.AppendDir("print_status");
    dir.AppendDir("gifs");
    dir.AppendDir("themes");
    return dir;
}
 
wxFileName PrintStatusThemeManager::UserThemesDir() const
{
    return m_user_themes_dir;
}

bool PrintStatusThemeManager::IsLocked(int index) const
{
    if (index < 0 || index >= (int)m_themes.size()) return true;
    if (index == 0) return true;   // Default always locked (no folder)
    return m_themes[index].is_locked;
}
 
void PrintStatusThemeManager::SetLocked(int index, bool locked)
{
    if (index <= 0 || index >= (int)m_themes.size()) return;
    m_themes[index].is_locked = locked;
    // Persist the unlocked state in theme.json so it survives restart.
    // We add an optional "locked" key, absent means default (true for bundled).
    WriteThemeJson(m_themes[index]);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void PrintStatusThemeManager::Init()
{
    if (m_initialized) return;
    m_initialized = true;
 
    wxString data = wxGetApp().app_config->get("data_dir");
    if (data.empty())
        data = wxStandardPaths::Get().GetUserDataDir();
 
    m_user_themes_dir = wxFileName(data, "");
    m_user_themes_dir.AppendDir("print_status_themes");
    m_themes_dir = m_user_themes_dir;
    
    ScanThemesDir();
 
    std::string saved = wxGetApp().app_config->get(CFG_KEY);
    if (!saved.empty()) {
        for (int i = 0; i < (int)m_themes.size(); ++i) {
            if (m_themes[i].id == saved) {
                m_active_index = i;
                break;
            }
        }
    }
}
 
// ---------------------------------------------------------------------------
// ScanThemesDir  — three passes
// ---------------------------------------------------------------------------
void PrintStatusThemeManager::ScanThemesDir()
{
    m_themes.clear();
 
    // ── Index 0: Default (no folder, uses embedded GIFs) ─────────────────
    ThemeInfo builtin;
    builtin.id           = "Default";
    builtin.display_name = "Default";
    builtin.description  = "Built-in default theme (embedded GIFs)";
    builtin.is_bundled   = false;
    m_themes.push_back(std::move(builtin));
 
    // ── Pass 1: bundled themes from resources/.../themes/ ────────────────
    // Zips in that folder are auto-extracted into subfolders on first run.
    // Subsequent runs find the already-extracted folders and skip extraction.
    wxFileName res_dir = ResourcesThemesDir();
    if (res_dir.DirExists()) {
        // Step 1a: extract any .zip files that don't yet have a folder
        wxDir zip_dir(res_dir.GetPath());
        if (zip_dir.IsOpened()) {
            wxString zip_name;
            bool ok = zip_dir.GetFirst(&zip_name, "*.zip", wxDIR_FILES);
            while (ok) {
                wxFileName zip_path(res_dir.GetPath(), zip_name);
                wxString   theme_id = zip_path.GetName();   // filename without .zip
 
                wxFileName out_dir(res_dir.GetPath(), "");
                out_dir.AppendDir(theme_id);
 
                if (!out_dir.DirExists()) {
                    // Extract once — creates <themes>/<id>/ beside the zip
                    ExtractZipToDir(zip_path.GetFullPath(), out_dir, /*err=*/nullptr);
                }
                ok = zip_dir.GetNext(&zip_name);
            }
        }
 
        // Step 1b: scan the extracted subdirectories
        wxDir dir(res_dir.GetPath());
        if (dir.IsOpened()) {
            wxString entry;
            bool ok = dir.GetFirst(&entry, wxEmptyString, wxDIR_DIRS);
            while (ok) {
                wxFileName sub(res_dir.GetPath(), "");
                sub.AppendDir(entry);
                ThemeInfo info = LoadThemeInfo(sub);
                if (!info.id.empty()) {
                    info.is_bundled = true;
                    m_themes.push_back(std::move(info));
                }
                ok = dir.GetNext(&entry);
            }
        }
    }
 
    // ── Pass 2: auto-extract loose zips dropped into user dir ────────────
    if (m_user_themes_dir.DirExists()) {
        wxDir zip_scan(m_user_themes_dir.GetPath());
        if (zip_scan.IsOpened()) {
            wxString entry;
            bool ok = zip_scan.GetFirst(&entry, "*.zip", wxDIR_FILES);
            while (ok) {
                wxFileName zip_path(m_user_themes_dir.GetPath(), entry);
                wxString err;
                ImportZip(zip_path.GetFullPath(), err,
                          /*rescan=*/false, /*switch_to_new=*/false);
                wxRemoveFile(zip_path.GetFullPath());
                ok = zip_scan.GetNext(&entry);
            }
        }
    }
 
    // ── Pass 3: user-installed theme subfolders ───────────────────────────
    if (m_user_themes_dir.DirExists()) {
        wxDir dir(m_user_themes_dir.GetPath());
        if (dir.IsOpened()) {
            wxString entry;
            bool ok = dir.GetFirst(&entry, wxEmptyString, wxDIR_DIRS);
            while (ok) {
                wxFileName sub(m_user_themes_dir.GetPath(), "");
                sub.AppendDir(entry);
                ThemeInfo info = LoadThemeInfo(sub);
                if (!info.id.empty()) {
                    info.is_bundled = false;
                    m_themes.push_back(std::move(info));
                }
                ok = dir.GetNext(&entry);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LoadThemeInfo
// ---------------------------------------------------------------------------
ThemeInfo PrintStatusThemeManager::LoadThemeInfo(const wxFileName& dir) const
{
    ThemeInfo info;
    info.root_dir = dir;
    info.id       = dir.GetDirs().Last().ToStdString();
 
    wxFileName json_file(dir.GetPath(), "theme.json");
    if (json_file.FileExists()) {
        try {
            wxFileInputStream fis(json_file.GetFullPath());
            if (fis.IsOk()) {
                wxString content;
                wxStringOutputStream sos(&content);
                fis.Read(sos);
                auto j = json::parse(content.ToStdString(), nullptr, false);
                if (!j.is_discarded()) {
                    if (j.contains("name") && j["name"].is_string())
                        info.display_name = j["name"].get<std::string>();
                    if (j.contains("author") && j["author"].is_string())
                        info.author = j["author"].get<std::string>();
                    if (j.contains("description") && j["description"].is_string())
                        info.description = j["description"].get<std::string>();
                    if (j.contains("locked") && j["locked"].is_boolean())
                        info.is_locked = j["locked"].get<bool>();
                }
            }
        } catch (...) {}
    }
 
    if (info.display_name.empty())
        info.display_name = info.id;
 
    return info;
}
 
void PrintStatusThemeManager::WriteThemeJson(const ThemeInfo& info) const
{
    json j;
    j["name"]        = info.display_name;
    j["author"]      = info.author;
    j["description"] = info.description;
    // Only write "locked" if it differs from the default for this theme type.
    // Bundled themes default to locked=true; user themes to locked=false.
    bool expected_default = info.is_bundled;
    if (info.is_locked != expected_default)
        j["locked"] = info.is_locked;
 
    wxFileName json_file(info.root_dir.GetPath(), "theme.json");
    wxFileOutputStream fos(json_file.GetFullPath());
    if (fos.IsOk()) {
        std::string s = j.dump(2);
        fos.Write(s.c_str(), s.size());
    }
}
 
// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------
void PrintStatusThemeManager::SetActiveTheme(int index)
{
    if (index < 0 || index >= (int)m_themes.size()) return;
    m_active_index = index;
    wxGetApp().app_config->set(CFG_KEY, m_themes[index].id);
    wxGetApp().app_config->save();
}
 
void PrintStatusThemeManager::SetActiveThemeById(const std::string& id)
{
    for (int i = 0; i < (int)m_themes.size(); ++i) {
        if (m_themes[i].id == id) { SetActiveTheme(i); return; }
    }
}
 
// ---------------------------------------------------------------------------
// Asset resolution
// ---------------------------------------------------------------------------
static wxString TryResolve(const wxFileName& dir, const wxString& stem)
{
    for (const wxString& ext : { wxString("gif"), wxString("png") }) {
        wxFileName f(dir.GetPath(), stem + "." + ext);
        if (f.FileExists()) return f.GetFullPath();
    }
    return wxEmptyString;
}
 
wxString PrintStatusThemeManager::Resolve(PrintState state) const
{
    return ResolveForTheme(m_themes[m_active_index], state);
}
 
wxString PrintStatusThemeManager::ResolveForTheme(const ThemeInfo& theme,
                                                    PrintState state) const
{
    // Guard first — before any root_dir access
    if (theme.is_builtin()) return wxEmptyString;
 
    wxString stem = StateFilename(state).BeforeLast('.');
    return TryResolve(theme.root_dir, stem);
}
 
// ---------------------------------------------------------------------------
// EnsureThemesDir (user dir only)
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::EnsureThemesDir(wxString& err_msg)
{
    if (!m_user_themes_dir.DirExists()) {
        if (!wxFileName::Mkdir(m_user_themes_dir.GetPath(), wxS_DIR_DEFAULT,
                               wxPATH_MKDIR_FULL)) {
            err_msg = "Could not create themes directory: " +
                      m_user_themes_dir.GetPath();
            return false;
        }
    }
    return true;
}

bool PrintStatusThemeManager::ExtractZipToDir(const wxString& zip_path,
                                               const wxFileName& out_dir,
                                               wxString* err_out)
{
    wxFileInputStream fis(zip_path);
    if (!fis.IsOk()) { if (err_out) *err_out = "Cannot open: " + zip_path; return false; }
 
    wxZipInputStream zis(fis);
    if (!zis.IsOk()) { if (err_out) *err_out = "Not a zip: " + zip_path; return false; }
 
    if (!wxFileName::Mkdir(out_dir.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        if (err_out) *err_out = "Cannot create dir: " + out_dir.GetPath();
        return false;
    }
 
    std::unique_ptr<wxZipEntry> entry;
    while ((entry.reset(zis.GetNextEntry()), entry != nullptr)) {
        wxString name = entry->GetName();
        if (name.Contains("/") || name.Contains("\\")) continue;
        if (name != "theme.json" && !name.StartsWith("status_")) continue;
 
        wxFileName out_file(out_dir.GetPath(), name);
        wxFileOutputStream fos(out_file.GetFullPath());
        if (!fos.IsOk()) continue;
        zis.Read(fos);
    }
    return true;
}

// ---------------------------------------------------------------------------
// ImportZip
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::ImportZip(const wxString& zip_path,
                                         wxString& err_msg,
                                         bool rescan,
                                         bool switch_to_new)
{
    if (!EnsureThemesDir(err_msg)) return false;
 
    wxFileInputStream fis(zip_path);
    if (!fis.IsOk()) {
        err_msg = "Cannot open zip file: " + zip_path;
        return false;
    }
 
    wxZipInputStream zis(fis);
    if (!zis.IsOk()) {
        err_msg = "Not a valid zip file: " + zip_path;
        return false;
    }
 
    wxFileName zfn(zip_path);
    std::string theme_id = zfn.GetName().ToStdString();
 
    wxFileName out_dir(m_user_themes_dir.GetPath(), "");
    out_dir.AppendDir(theme_id);
 
    if (!wxFileName::Mkdir(out_dir.GetPath(), wxS_DIR_DEFAULT,
                           wxPATH_MKDIR_FULL)) {
        err_msg = "Could not create theme folder: " + out_dir.GetPath();
        return false;
    }
    
    wxString ext_err;
    if (!ExtractZipToDir(zip_path, out_dir, &ext_err)) {
       err_msg = ext_err; return false;
    }
 
    std::unique_ptr<wxZipEntry> entry;
    while ((entry.reset(zis.GetNextEntry()), entry != nullptr)) {
        wxString name = entry->GetName();
        if (name.Contains("/") || name.Contains("\\")) continue;
        if (name != "theme.json" && !name.StartsWith("status_")) continue;
 
        wxFileName out_file(out_dir.GetPath(), name);
        wxFileOutputStream fos(out_file.GetFullPath());
        if (!fos.IsOk()) continue;
        zis.Read(fos);
    }
    
    if (rescan) {
        Rescan();
        if (switch_to_new) {
            for (int i = 0; i < (int)m_themes.size(); ++i) {
                if (m_themes[i].id == theme_id) {
                    SetActiveTheme(i);
                    break;
                }
            }
        }
    }
 
    return true;
}
 
// ---------------------------------------------------------------------------
// ImportFolder
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::ImportFolder(const wxString& folder_path,
                                            wxString& err_msg)
{
    if (!EnsureThemesDir(err_msg)) return false;
 
    wxFileName src(folder_path, "");
    if (!src.DirExists()) {
        err_msg = "Source folder does not exist: " + folder_path;
        return false;
    }
 
    std::string theme_id = src.GetDirs().Last().ToStdString();
    wxFileName  dst(m_user_themes_dir.GetPath(), "");
    dst.AppendDir(theme_id);
 
    if (!wxFileName::Mkdir(dst.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        err_msg = "Could not create destination folder: " + dst.GetPath();
        return false;
    }
 
    wxDir dir(src.GetPath());
    if (!dir.IsOpened()) {
        err_msg = "Cannot open source folder: " + folder_path;
        return false;
    }
 
    wxString file;
    bool ok = dir.GetFirst(&file, wxEmptyString, wxDIR_FILES);
    while (ok) {
        if (file == "theme.json" || file.StartsWith("status_")) {
            wxString from = src.GetPath() + wxFileName::GetPathSeparator() + file;
            wxString to   = dst.GetPath() + wxFileName::GetPathSeparator() + file;
            wxCopyFile(from, to, true);
        }
        ok = dir.GetNext(&file);
    }
 
    Rescan();
 
    // Switch to the newly imported theme
    for (int i = 0; i < (int)m_themes.size(); ++i) {
        if (m_themes[i].id == theme_id) {
            SetActiveTheme(i);
            break;
        }
    }
 
    return true;
}
 
// ---------------------------------------------------------------------------
// ExportZip
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::ExportZip(int theme_index,
                                         const wxString& out_path,
                                         wxString& err_msg)
{
    if (theme_index < 0 || theme_index >= (int)m_themes.size()) {
        err_msg = "Invalid theme index."; return false;
    }
    const ThemeInfo& theme = m_themes[theme_index];
    if (theme.is_builtin()) {
        err_msg = "The Default theme cannot be exported."; return false;
    }
 
    wxFileOutputStream fos(out_path);
    if (!fos.IsOk()) { err_msg = "Cannot create output file: " + out_path; return false; }
 
    wxZipOutputStream zos(fos);
    wxDir dir(theme.root_dir.GetPath());
    if (!dir.IsOpened()) { err_msg = "Cannot open theme folder."; return false; }
 
    wxString file;
    bool ok = dir.GetFirst(&file, wxEmptyString, wxDIR_FILES);
    while (ok) {
        if (file == "theme.json" || file.StartsWith("status_")) {
            wxString full = theme.root_dir.GetPath() +
                            wxFileName::GetPathSeparator() + file;
            wxFileInputStream fis(full);
            if (fis.IsOk()) { zos.PutNextEntry(file); fis.Read(zos); }
        }
        ok = dir.GetNext(&file);
    }
    zos.Close();
    return true;
}
 
// ---------------------------------------------------------------------------
// CreateTheme
// ---------------------------------------------------------------------------
int PrintStatusThemeManager::CreateTheme(const std::string& name,
                                          const std::string& author,
                                          const std::string& description,
                                          wxString& err_msg)
{
    if (!EnsureThemesDir(err_msg)) return -1;
 
    std::string id = name;
    for (char& c : id)
        if (!std::isalnum(c) && c != '_' && c != '-') c = '_';
 
    wxFileName dir(m_user_themes_dir.GetPath(), "");
    dir.AppendDir(id);
 
    if (dir.DirExists()) {
        err_msg = "A theme named '" + wxString(id) + "' already exists.";
        return -1;
    }
 
    if (!wxFileName::Mkdir(dir.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        err_msg = "Could not create theme folder: " + dir.GetPath();
        return -1;
    }
 
    ThemeInfo info;
    info.id           = id;
    info.display_name = name;
    info.author       = author;
    info.description  = description;
    info.root_dir     = dir;
    info.is_bundled   = false;
    WriteThemeJson(info);
 
    Rescan();
    for (int i = 0; i < (int)m_themes.size(); ++i)
        if (m_themes[i].id == id) return i;
    return -1;
}
 
// ---------------------------------------------------------------------------
// DeleteTheme
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::DeleteTheme(int index, wxString& err_msg)
{
    if (index <= 0 || index >= (int)m_themes.size()) {
        err_msg = "Cannot delete the Default theme."; return false;
    }
    // Allow deleting locked bundled themes, lock is just an edit guard, not
    // a delete guard. User can always re-import from zip to restore.
    if (m_themes[index].is_locked) {
         err_msg = "Unlock this theme before deleting it."; return false;
    }
 
    wxString path = m_themes[index].root_dir.GetPath();
    wxDir dir(path);
    if (dir.IsOpened()) {
        wxString file;
        bool ok = dir.GetFirst(&file, wxEmptyString, wxDIR_FILES);
        while (ok) {
            wxRemoveFile(path + wxFileName::GetPathSeparator() + file);
            ok = dir.GetNext(&file);
        }
    }
    if (!wxRmdir(path)) {
        err_msg = "Could not remove theme folder: " + path; return false;
    }
    if (m_active_index == index) {
        m_active_index = 0;
        wxGetApp().app_config->set(CFG_KEY, "Default");
        wxGetApp().app_config->save();
    }
    Rescan();
    return true;
}

bool PrintStatusThemeManager::DuplicateTheme(int src_index,
                                              const std::string& new_name,
                                              const std::string& author,
                                              const std::string& description,
                                              wxString& err_msg)
{
    if (src_index < 0 || src_index >= (int)m_themes.size()) {
        err_msg = "Invalid source theme."; return false;
    }
    if (!EnsureThemesDir(err_msg)) return false;
 
    std::string id = new_name;
    for (char& c : id)
        if (!std::isalnum(c) && c != '_' && c != '-') c = '_';
 
    wxFileName dst_dir(m_user_themes_dir.GetPath(), "");
    dst_dir.AppendDir(id);
 
    if (dst_dir.DirExists()) {
        err_msg = "A theme named '" + wxString(id) + "' already exists.";
        return false;
    }
    if (!wxFileName::Mkdir(dst_dir.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        err_msg = "Could not create theme folder."; return false;
    }
 
    if (src_index == 0) {
        // Duplicate Default — copy embedded GIFs to disk
        for (const PrintState& s : AllStates()) {
            size_t len = 0;
            const uint8_t* data = PrintStatusIconGIF::GetData(s, len);
            if (!data || !len) continue;
            wxFileName dst_file(dst_dir.GetPath(), StateFilename(s));
            wxFileOutputStream fos(dst_file.GetFullPath());
            if (fos.IsOk()) fos.Write(data, len);
        }
    } else {
        // Copy status_*.gif/png from source folder
        const ThemeInfo& src = m_themes[src_index];
        if (src.root_dir.DirExists()) {
            wxDir dir(src.root_dir.GetPath());
            if (dir.IsOpened()) {
                wxString file;
                bool ok = dir.GetFirst(&file, wxEmptyString, wxDIR_FILES);
                while (ok) {
                    if (file.StartsWith("status_")) {
                        wxCopyFile(src.root_dir.GetPath() +
                                       wxFileName::GetPathSeparator() + file,
                                   dst_dir.GetPath() +
                                       wxFileName::GetPathSeparator() + file,
                                   true);
                    }
                    ok = dir.GetNext(&file);
                }
            }
        }
    }
 
    ThemeInfo info;
    info.id           = id;
    info.display_name = new_name;
    info.author       = author;
    info.description  = description;
    info.root_dir     = dst_dir;
    info.is_bundled   = false;
    WriteThemeJson(info);
 
    Rescan();
    for (int i = 0; i < (int)m_themes.size(); ++i)
        if (m_themes[i].id == id) { SetActiveTheme(i); return true; }
 
    err_msg = "Theme created but not found after rescan.";
    return false;
}

// ---------------------------------------------------------------------------
// SetStateAsset
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::SetStateAsset(int theme_index, PrintState state,
                                             const wxString& src_path,
                                             wxString& err_msg)
{
    if (theme_index <= 0 || theme_index >= (int)m_themes.size()) {
        err_msg = "Cannot modify the Default theme."; return false;
    }
    if (m_themes[theme_index].is_bundled) {
        err_msg = "Built-in themes cannot be edited.\n"
                  "Use '+ New' → 'Duplicate' to create your own editable copy.";
        return false;
    }
    wxFileName src(src_path);
    wxString ext = src.GetExt().Lower();
    if (ext != "gif" && ext != "png") {
        err_msg = "Only .gif and .png files are supported."; return false;
    }
    const ThemeInfo& theme = m_themes[theme_index];
    wxString stem = StateFilename(state).BeforeLast('.');
    for (const wxString& other : { wxString("gif"), wxString("png") }) {
        if (other == ext) continue;
        wxFileName old_file(theme.root_dir.GetPath(), stem + "." + other);
        if (old_file.FileExists()) wxRemoveFile(old_file.GetFullPath());
    }
    wxFileName dst(theme.root_dir.GetPath(), stem + "." + ext);
    if (!wxCopyFile(src_path, dst.GetFullPath(), true)) {
        err_msg = "Could not copy file to theme folder."; return false;
    }
    return true;
}
 
// ---------------------------------------------------------------------------
// ClearStateAsset
// ---------------------------------------------------------------------------
bool PrintStatusThemeManager::ClearStateAsset(int theme_index, PrintState state,
                                               wxString& err_msg)
{
    if (theme_index <= 0 || theme_index >= (int)m_themes.size()) {
        err_msg = "Cannot modify the Default theme."; return false;
    }
    if (m_themes[theme_index].is_bundled) {
        err_msg = "Built-in themes cannot be edited."; return false;
    }
    const ThemeInfo& theme = m_themes[theme_index];
    wxString stem = StateFilename(state).BeforeLast('.');
    bool removed = false;
    for (const wxString& ext : { wxString("gif"), wxString("png") }) {
        wxFileName f(theme.root_dir.GetPath(), stem + "." + ext);
        if (f.FileExists()) { wxRemoveFile(f.GetFullPath()); removed = true; }
    }
    if (!removed) { err_msg = "No asset found for that state."; return false; }
    return true;
}

void PrintStatusThemeManager::Rescan()
{
    std::string active_id = m_themes[m_active_index].id;
    ScanThemesDir();
    m_active_index = 0;
    for (int i = 0; i < (int)m_themes.size(); ++i) {
        if (m_themes[i].id == active_id) {
            m_active_index = i;
            break;
        }
    }
}

} // namespace GUI
} // namespace Slic3r
// ProjectVCBackupManager.cpp
// Drop into: src/slic3r/GUI/
// Add to src/slic3r/CMakeLists.txt under SLIC3R_GUI_SOURCES.

#include "ProjectVCBackupManager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

// Filesystem — OrcaSlicer already requires C++17, so std::filesystem is fine.
#include <filesystem>
namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace Slic3r { namespace GUI {

// ============================================================
//  VCBackup helpers
// ============================================================

std::string VCBackup::filename() const
{
    fs::path p(VCBackup_path);
    return p.filename().string();
}

std::string VCBackup::date_string() const
{
    if (timestamp == 0)
        return "—";
    std::tm* tm = std::localtime(&timestamp);
    char     buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d  %H:%M", tm);
    return buf;
}

std::string VCBackup::relative_age() const
{
    if (timestamp == 0)
        return "";
    auto now   = static_cast<std::time_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    long delta = static_cast<long>(now - timestamp);
    if (delta < 60)
        return "just now";
    if (delta < 3600)
        return std::to_string(delta / 60) + " min ago";
    if (delta < 86400)
        return std::to_string(delta / 3600) + " hr ago";
    if (delta < 86400 * 7)
        return std::to_string(delta / 86400) + " days ago";
    if (delta < 86400 * 30)
        return std::to_string(delta / 86400 / 7) + " weeks ago";
    return std::to_string(delta / 86400 / 30) + " months ago";
}

// ============================================================
//  Construction
// ============================================================

ProjectVCBackupManager::ProjectVCBackupManager(const std::string& data_dir, size_t max_VCBackups_per_project)
    : m_max_VCBackups(max_VCBackups_per_project)
{
    m_default_VCBackup_dir = (fs::path(data_dir) / "orca_vc_backups").string();
    m_VCBackup_dir         = m_default_VCBackup_dir;
 
    // Load any previously saved custom path
    load_config();
 
    // Ensure the active dir exists
    make_dirs(m_default_VCBackup_dir);
}

// ============================================================
//  Backup directory management
// ============================================================
 
void ProjectVCBackupManager::set_backup_dir(const std::string& new_dir)
{
    if (new_dir.empty()) return;
    m_VCBackup_dir = new_dir;
    make_dirs(m_VCBackup_dir);
    save_config();
}
 
void ProjectVCBackupManager::reset_backup_dir()
{
    m_VCBackup_dir = m_default_VCBackup_dir;
    make_dirs(m_VCBackup_dir);
    save_config();
}
 
MigrationResult ProjectVCBackupManager::migrate_backups_to(
    const std::string& new_dir,
    std::function<void(size_t, size_t)> progress_cb)
{
    MigrationResult result;
    result.new_dir = new_dir;
 
    if (new_dir.empty() || new_dir == m_VCBackup_dir) {
        result.error_message = "Target directory is the same as the current one.";
        return result;
    }
 
    if (!make_dirs(new_dir)) {
        result.error_message = "Could not create target directory: " + new_dir;
        return result;
    }
 
    // Collect every .3mf file under the current backup root
    std::vector<fs::path> all_files;
    if (fs::exists(m_VCBackup_dir)) {
        for (const auto& entry : fs::recursive_directory_iterator(m_VCBackup_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".3mf")
                all_files.push_back(entry.path());
        }
    }
 
    size_t total = all_files.size();
    size_t done  = 0;
 
    for (const auto& src : all_files) {
        // Reconstruct the relative path under the new root
        auto rel = fs::relative(src, m_VCBackup_dir);
        fs::path dst_path = fs::path(new_dir) / rel;
 
        std::error_code ec;
        fs::create_directories(dst_path.parent_path(), ec);
        if (ec) { ++result.files_failed; continue; }
 
        fs::copy_file(src, dst_path, fs::copy_options::overwrite_existing, ec);
        if (ec) { ++result.files_failed; }
        else    { ++result.files_moved; }
 
        ++done;
        if (progress_cb) progress_cb(done, total);
    }
 
    // Only switch the active dir if at least one file moved (or there was
    // nothing to move), i.e. the operation was not a total failure.
    if (result.files_failed == 0 || result.files_moved > 0) {
        m_VCBackup_dir = new_dir;
        save_config();
    } else {
        result.error_message = "All files failed to copy. Backup directory not changed.";
    }
 
    return result;
}

// ============================================================
//  Core API
// ============================================================

std::string ProjectVCBackupManager::capture_VCBackup(const std::string& source_path)
{
    if (source_path.empty() || !fs::exists(source_path))
        return "";

    std::string dir = VCBackup_dir_for(source_path);
    if (!make_dirs(dir))
        return "";

    std::string ts  = timestamp_string();
    std::string dst = (fs::path(dir) / (ts + ".3mf")).string();

    if (!copy_file(source_path, dst))
        return "";

    prune(dir);
    return dst;
}

std::vector<VCBackup> ProjectVCBackupManager::list_VCBackups(const std::string& source_path) const
{
    std::vector<VCBackup> result;

    std::string dir = VCBackup_dir_for(source_path);
    if (!fs::exists(dir))
        return result;

    for (const std::filesystem::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        auto path = entry.path();
        auto fn = path.filename();
        auto stringN = fn.string(); 
        std::string fname = stringN;
        
        // Expect format: YYYY-MM-DD_HH-MM-SS.3mf  (23 chars including ext)
        if (fname.size() < 23 || fname.substr(fname.size() - 4) != ".3mf")
            continue;

        auto name = path == fn;
        
        VCBackup snap;
        snap.VCBackup_path = entry.path().string();
        snap.source_path   = source_path;

        // Parse timestamp from filename stem: "2026-03-19_14-22-05"
        std::string        stem = fname.substr(0, 19);
        std::tm            tm{};
        std::istringstream ss(stem);
        ss >> std::get_time(&tm, "%Y-%m-%d_%H-%M-%S");
        if (!ss.fail())
            snap.timestamp = std::mktime(&tm);

        result.push_back(std::move(snap));
    }

    // Newest first
    std::sort(result.begin(), result.end(), [](const VCBackup& a, const VCBackup& b) { return a.timestamp > b.timestamp; });

    return result;
}

bool ProjectVCBackupManager::restore(const std::string& VCBackup_path, const std::string& destination_path)
{
    if (!fs::exists(VCBackup_path))
        return false;

    std::string dst = destination_path;
    if (dst.empty()) {
        // Derive original path from the VCBackup's parent directory name
        // which is the sanitised project name — we can't perfectly reverse
        // the sanitisation, so require the caller to supply destination_path.
        return false;
    }

    // Back up the current file before overwriting
    if (fs::exists(dst)) {
        std::string backup = dst + ".bak";
        fs::copy_file(dst, backup, fs::copy_options::overwrite_existing);
    }

    return copy_file(VCBackup_path, dst);
}

bool ProjectVCBackupManager::delete_VCBackup(const std::string& VCBackup_path)
{
    std::error_code ec;
    return fs::remove(VCBackup_path, ec);
}

bool ProjectVCBackupManager::delete_all_VCBackups(const std::string& source_path)
{
    std::string dir = VCBackup_dir_for(source_path);
    if (!fs::exists(dir))
        return true;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return !ec;
}

std::string ProjectVCBackupManager::VCBackup_dir_for(const std::string& source_path) const
{
    return (fs::path(m_default_VCBackup_dir) / sanitise_name(source_path)).string();
}

// ============================================================
//  Config persistence
// ============================================================
 
std::string ProjectVCBackupManager::config_file_path() const
{
    return (fs::path(m_data_dir) / "vc_config.json").string();
}
 
void ProjectVCBackupManager::load_config()
{
    std::ifstream ifs(config_file_path());
    if (!ifs.is_open()) return;
    try {
        json root = json::parse(ifs);
        std::string saved = root.value("backup_dir", "");
        if (!saved.empty())
            m_VCBackup_dir = saved;
    } catch (...) {}
}
 
void ProjectVCBackupManager::save_config() const
{
    json root;
    root["backup_dir"] = m_VCBackup_dir;
    std::ofstream ofs(config_file_path());
    if (ofs.is_open())
        ofs << root.dump(2);
}

// ============================================================
//  Private helpers
// ============================================================

std::string ProjectVCBackupManager::sanitise_name(const std::string& source_path)
{
    // Take just the filename stem (no directory, no extension)
    fs::path    p(source_path);
    std::string stem = p.stem().string();

    // Replace anything that isn't alphanumeric, hyphen, or dot with '_'
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.')
            c = '_';
    }

    // Collapse multiple consecutive underscores
    std::string out;
    out.reserve(stem.size());
    bool last_was_us = false;
    for (char c : stem) {
        if (c == '_') {
            if (!last_was_us)
                out += c;
            last_was_us = true;
        } else {
            out += c;
            last_was_us = false;
        }
    }

    // Guard against empty (shouldn't happen in practice)
    if (out.empty())
        out = "unnamed_project";
    return out;
}

std::string ProjectVCBackupManager::timestamp_string(std::time_t t)
{
    if (t == 0)
        t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm* tm = std::localtime(&t);
    char     buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", tm);
    return buf;
}

bool ProjectVCBackupManager::copy_file(const std::string& src, const std::string& dst)
{
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool ProjectVCBackupManager::make_dirs(const std::string& path)
{
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

void ProjectVCBackupManager::prune(const std::string& project_dir) const
{
    // Collect all .3mf files in the project dir, sort oldest first
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(project_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".3mf")
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end()); // lexicographic = chronological for our format

    // Remove oldest beyond the cap
    while (files.size() > m_max_VCBackups) {
        std::error_code ec;
        fs::remove(files.front(), ec);
        files.erase(files.begin());
    }
}

size_t ProjectVCBackupManager::count_backup_files(const std::string& root)
{
    if (!fs::exists(root)) return 0;
    size_t n = 0;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file() && e.path().extension() == ".3mf")
            ++n;
    return n;
}

wxButton* ProjectVCBackupManager::make_button(wxWindow* parent, const wxString& label, HistoryBtnStyle style)
{
    auto* btn = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxSize(-1, 28));
    btn->SetWindowStyle(wxBORDER_NONE);

    wxColour bg, fg;
    switch (style) {
    case HistoryBtnStyle::Primary:
        bg = wxColour(26, 161, 121); // OrcaSlicer teal accent
        fg = wxColour(255, 255, 255);
        break;
    case HistoryBtnStyle::Danger:
        bg = wxColour(61, 26, 26);    // dark red surface
        fg = wxColour(240, 149, 149); // soft red text
        break;
    case HistoryBtnStyle::Secondary:
    default:
        bg = wxColour(55, 55, 55);    // dark neutral surface
        fg = wxColour(210, 210, 210); // light text
        break;
    }

    btn->SetBackgroundColour(bg);
    btn->SetForegroundColour(fg);
    btn->SetOwnBackgroundColour(bg);
    btn->SetOwnForegroundColour(fg);
    btn->Refresh();

    return btn;
}
}} // namespace Slic3r::GUI
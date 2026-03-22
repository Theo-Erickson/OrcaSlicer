// ProjectVCBackupManager.cpp
// Drop into: src/slic3r/GUI/
// Add to src/slic3r/CMakeLists.txt under SLIC3R_GUI_SOURCES.

#include "ProjectVCBackupManager.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

// Filesystem — OrcaSlicer already requires C++17, so std::filesystem is fine.
#include <filesystem>
namespace fs = std::filesystem;

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
    fs::path base = fs::path(data_dir) / "orca_history" / "VCBackups";
    m_base_dir    = base.string();
    make_dirs(m_base_dir);
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
    return (fs::path(m_base_dir) / sanitise_name(source_path)).string();
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

wxButton* ProjectVCBackupManager::make_button(wxWindow* parent, const wxString& label, BtnStyle style)
{
    auto* btn = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxSize(-1, 28));
    btn->SetWindowStyle(wxBORDER_NONE);

    wxColour bg, fg;
    switch (style) {
    case BtnStyle::Primary:
        bg = wxColour(26, 161, 121); // OrcaSlicer teal accent
        fg = wxColour(255, 255, 255);
        break;
    case BtnStyle::Danger:
        bg = wxColour(61, 26, 26);    // dark red surface
        fg = wxColour(240, 149, 149); // soft red text
        break;
    case BtnStyle::Secondary:
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
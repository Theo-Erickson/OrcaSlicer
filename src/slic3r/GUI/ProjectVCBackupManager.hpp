#pragma once
// ProjectVCBackupManager.hpp
// Drop into: src/slic3r/GUI/
//
// Pure C++ class — no wxWidgets dependency.
// Manages timestamped .3mf VCBackup copies for the History panel's
// version-control feature.
//
// Directory layout on disk:
//
//   <data_dir>/orca_history/
//     VCBackups/
//       <sanitised_project_name>/
//         2026-03-19_14-22-05.3mf
//         2026-03-18_09-04-11.3mf
//         ...
//     history.json          <- written by HistoryPanel, not here
//
// Each project directory is named after a sanitised version of the
// original filename (no extension, illegal chars replaced with '_').
// VCBackups are sorted newest-first by filename (lexicographic on the
// timestamp prefix is sufficient).

#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <cstdint>

namespace Slic3r { namespace GUI {

enum class BtnStyle { Primary, Secondary, Danger };

// ---------------------------------------------------------------------------
// One VCBackup on disk
// ---------------------------------------------------------------------------
struct VCBackup //Version Control VCBackup
{
    std::string VCBackup_path; ///< Full path to the .3mf copy
    std::string source_path;   ///< Original project path at time of capture
    std::time_t timestamp{0};

    std::string filename() const;     ///< e.g. "2026-03-19_14-22-05.3mf"
    std::string date_string() const;  ///< e.g. "2026-03-19  14:22"
    std::string relative_age() const; ///< e.g. "3 days ago"
};

// ---------------------------------------------------------------------------
// ProjectVCBackupManager
// ---------------------------------------------------------------------------
class ProjectVCBackupManager
{
public:
    // max_VCBackups_per_project: oldest VCBackups beyond this cap are pruned
    explicit ProjectVCBackupManager(const std::string& data_dir, size_t max_VCBackups_per_project = 20);

    // -----------------------------------------------------------------------
    // Core API
    // -----------------------------------------------------------------------

    /// Copy `source_path` into the VCBackup store.
    /// Returns the path of the newly created VCBackup file, or "" on failure.
    std::string capture_VCBackup(const std::string& source_path);

    /// List all VCBackups for a given project file, newest first.
    std::vector<VCBackup> list_VCBackups(const std::string& source_path) const;

    /// Restore a VCBackup: copies `VCBackup_path` to `destination_path`.
    /// `destination_path` defaults to the original source if empty.
    /// Returns true on success.
    bool restore(const std::string& VCBackup_path, const std::string& destination_path = "");

    /// Delete a single VCBackup file.
    bool delete_VCBackup(const std::string& VCBackup_path);

    /// Delete all VCBackups for a given project.
    bool delete_all_VCBackups(const std::string& source_path);

    /// Returns the directory where VCBackups for this project are stored.
    std::string VCBackup_dir_for(const std::string& source_path) const;

    // -----------------------------------------------------------------------
    // Settings
    // -----------------------------------------------------------------------
    size_t max_VCBackups_per_project() const { return m_max_VCBackups; }
    void   set_max_VCBackups(size_t n) { m_max_VCBackups = n; }

private:
    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Turn a project filename into a safe directory name.
    /// e.g. "my bracket (v2).3mf" -> "my_bracket__v2_"
    static std::string sanitise_name(const std::string& source_path);

    /// Build a timestamp string suitable for use in a filename.
    /// e.g. "2026-03-19_14-22-05"
    static std::string timestamp_string(std::time_t t = 0);

    /// Copy src -> dst, creating parent directories as needed.
    /// Returns true on success.
    static bool copy_file(const std::string& src, const std::string& dst);

    /// Create all directories in path (like mkdir -p).
    static bool make_dirs(const std::string& path);

    /// Remove VCBackups beyond the cap, oldest first.
    void prune(const std::string& project_dir) const;

    std::string m_base_dir; ///< <data_dir>/orca_history/VCBackups/
    size_t      m_max_VCBackups;
    
    // Shared button factory with explicit colour styling
    wxButton* make_button(wxWindow* parent, const wxString& label, BtnStyle style);
};

}} // namespace Slic3r::GUI
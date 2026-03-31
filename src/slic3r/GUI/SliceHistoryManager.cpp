#include "SliceHistoryManager.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Slic3r { namespace GUI {

void SliceHistoryManager::push_snapshot(const std::string&        print_time,
                                         double                    filament_g,
                                         double                    filament_mm,
                                         int                       object_count,
                                         const std::string&        preset_name,
                                         const DynamicPrintConfig& cfg)
{
    if (m_snapshots.size() >= MAX_SNAPSHOTS)
        m_snapshots.pop_front();

    ++m_counter;

    std::time_t t  = std::time(nullptr);
    std::tm*    lt = std::localtime(&t);
    std::ostringstream ts;
    ts << "Snap " << m_counter << " – "
       << std::setw(2) << std::setfill('0') << lt->tm_hour << ":"
       << std::setw(2) << std::setfill('0') << lt->tm_min;

    SliceSnapshot snap;
    snap.label        = ts.str();
    snap.print_time   = print_time;
    snap.filament_g   = filament_g;
    snap.filament_mm  = filament_mm;
    snap.object_count = object_count;
    snap.preset_name  = preset_name;
    snap.config       = cfg;   // deep copy of global print config

    m_snapshots.push_back(std::move(snap));
}

std::vector<ConfigDiff> SliceHistoryManager::diff(
    size_t snapshot_idx, const DynamicPrintConfig& current) const
{
    std::vector<ConfigDiff> result;
    if (snapshot_idx >= m_snapshots.size()) return result;

    const DynamicPrintConfig& snap_cfg = m_snapshots[snapshot_idx].config;

    for (auto key : snap_cfg.keys()) {
        std::string snap_val = snap_cfg.opt_serialize(key);
        std::string curr_val = current.has(key)
            ? current.opt_serialize(key)
            : "(not set)";
        if (snap_val != curr_val)
            result.push_back({ key, snap_val, curr_val });
    }
    return result;
}

}} // namespace Slic3r::GUI
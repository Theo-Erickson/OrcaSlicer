#pragma once
#include <string>
#include <vector>
#include <deque>
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI {

struct SliceSnapshot {
    std::string           label;          // "Snap 1 – 14:32"
    std::string           print_time;     // "1h 23m"
    double                filament_g  = 0;
    double                filament_mm = 0;
    int                   object_count = 0;  // number of objects on plate
    std::string           preset_name;    // name of the print preset
    DynamicPrintConfig    config;         // global print config only
};

struct ConfigDiff {
    std::string key;
    std::string snap_value;    // value at snapshot time
    std::string curr_value;    // current value
};

class SliceHistoryManager {
public:
    static constexpr size_t MAX_SNAPSHOTS = 4;

    void push_snapshot(const std::string&        print_time,
                       double                    filament_g,
                       double                    filament_mm,
                       int                       object_count,
                       const std::string&        preset_name,
                       const DynamicPrintConfig& global_print_config);

    size_t count() const { return m_snapshots.size(); }
    const SliceSnapshot& get(size_t idx) const { return m_snapshots[idx]; }
    const std::deque<SliceSnapshot>& all() const { return m_snapshots; }

    // Returns keys that differ between snapshot and current global config
    std::vector<ConfigDiff> diff(size_t snapshot_idx,
                                 const DynamicPrintConfig& current_config) const;

private:
    std::deque<SliceSnapshot> m_snapshots;
    int m_counter = 0;
};

}} // namespace Slic3r::GUI
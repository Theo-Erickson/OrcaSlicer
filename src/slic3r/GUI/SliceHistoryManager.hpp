#pragma once
#include <string>
#include <vector>
#include <deque>
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI {

// Per-extruder filament usage broken down by role category
struct ExtruderFilamentUsage {
    int           extruder_id  = 0;
    std::string   color_hex;        // e.g. "#FF6600"
    std::string   material_name;    // e.g. "Generic PLA"

    double        model_mm    = 0.0;
    double        model_g     = 0.0;
    double        support_mm  = 0.0;
    double        support_g   = 0.0;
    double        flush_mm    = 0.0;
    double        flush_g     = 0.0;
    double        tower_mm    = 0.0;   
    double        tower_g     = 0.0;   
    double        other_mm    = 0.0;  // wipe tower, etc.
    double        other_g     = 0.0;
    double        total_mm    = 0.0;
    double        total_g     = 0.0;
};

struct SliceSnapshot {
    std::string           label;
    std::string           print_time;
    int                   object_count = 0;
    std::string           preset_name;
    DynamicPrintConfig    config;

    // Legacy totals (kept for backward compat)
    double                filament_g   = 0;
    double                filament_mm  = 0;

    // NEW: per-extruder breakdown
    std::vector<ExtruderFilamentUsage> extruder_usages;

    // NEW: cross-extruder category totals
    double  total_model_g   = 0.0;
    double  total_support_g = 0.0;
    double  total_flush_g   = 0.0;
    double  total_tower_g   = 0.0;
    double  total_other_g   = 0.0;
};

struct ConfigDiff {
    std::string key;
    std::string snap_value;
    std::string curr_value;
};

class SliceHistoryManager {
public:
    static constexpr size_t MAX_SNAPSHOTS = 4;

    void push_snapshot(const std::string&                      print_time,
                       double                                  filament_g,
                       double                                  filament_mm,
                       int                                     object_count,
                       const std::string&                      preset_name,
                       const DynamicPrintConfig&               cfg,
                       std::vector<ExtruderFilamentUsage>      extruder_usages);

    size_t count() const { return m_snapshots.size(); }
    const SliceSnapshot& get(size_t idx) const { return m_snapshots[idx]; }
    const std::deque<SliceSnapshot>& all() const { return m_snapshots; }

    std::vector<ConfigDiff> diff(size_t snapshot_idx,
                                 const DynamicPrintConfig& current_config) const;
    void remove(size_t idx); 
    void clear();

private:
    std::deque<SliceSnapshot> m_snapshots;
    int m_counter = 0;
};

}} // namespace Slic3r::GUI
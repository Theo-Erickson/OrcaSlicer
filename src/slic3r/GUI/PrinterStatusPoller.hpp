#pragma once
// PrinterStatusPoller.hpp
//
// A wxTimer-based poller that runs on the main thread, reads the
// currently-selected MachineObject's state every ~500ms, and calls
// MainFrame::m_print_status_icon->SetState() when it changes.
//
// WHY A POLLER INSTEAD OF A CALLBACK?
// MachineObject is updated by the MQTT/network thread writing fields
// directly (stage_curr, print_status, mc_print_percent, m_is_online).
// There is no existing callback or wxEvent fired when these fields change.
// StatusPanel uses the same timer pattern — we do the same.
//
// THREAD SAFETY:
// The timer fires on the main thread (EVT_TIMER is dispatched by wx's
// event loop).  We read MachineObject fields here — this is the same
// pattern StatusPanel uses, so it is safe.

#include <wx/wx.h>
#include <wx/timer.h>
#include "PrintStatusIcon.hpp"   // for PrintState enum

namespace Slic3r {
namespace GUI {

// Commented map of print status int to description. Copied from DeviceManager.cpp
/*
    case 1:   // Auto bed leveling
    case 2:   // Heatbed preheating
    case 3:   // Vibration compensation
    case 4:   // Changing filament
    case 5:   // M400 pause
    case 6:   // Paused (filament ran out)
    case 7:   // Heating nozzle
    case 8:   // Calibrating dynamic flow
    case 9:   // Scanning bed surface
    case 10:  // Inspecting first layer
    case 11:  // Identifying build plate type
    case 12:  // Calibrating Micro Lidar
    case 13:  // Homing toolhead
    case 14:  // Cleaning nozzle tip
    case 15:  // Checking extruder temperature
    case 16:  // Paused by the user
    case 17:  // Pause (front cover fall off)
    case 18:  // Calibrating the micro lidar
    case 19:  // Calibrating flow ratio
    case 20:  // Pause (nozzle temperature malfunction)
    case 21:  // Pause (heatbed temperature malfunction)
    case 22:  // Filament unloading
    case 23:  // Pause (step loss)
    case 24:  // Filament loading
    case 25:  // Motor noise cancellation
    case 26:  // Pause (AMS offline)
    case 27:  // Pause (low speed of heatbreak fan)
    case 28:  // Pause (chamber temperature control problem)
    case 29:  // Cooling chamber
    case 30:  // Pause (G-code inserted by user)
    case 31:  // Motor noise showoff
    case 32:  // Pause (nozzle clumping)
    case 33:  // Pause (cutter error)
    case 34:  // Pause (first layer error)
    case 35:  // Pause (nozzle clog)
    case 36:  // Measuring motion precision
    case 37:  // Enhancing motion precision
    case 38:  // Measure motion accuracy
    case 39:  // Nozzle offset calibration
    case 40:  // High temperature auto bed leveling
    case 41:  // Auto Check: Quick Release Lever
    case 42:  // Auto Check: Door and Upper Cover
    case 43:  // Laser Calibration    
    case 44:  // Auto Check: Platform
    case 45:  // Confirming BirdsEye Camera location
    case 46:  // Calibrating BirdsEye Camera
    case 47:  // Auto bed leveling phase 1
    case 48:  // Auto bed leveling phase 2
    case 49:  // Heating chamber
    case 50:  // Cooling heatbed
    case 51:  // Printing calibration lines
    case 52:  // Auto Check: Material
    case 53:  // Live View Camera Calibration
    case 54:  // Waiting for heatbed to reach target temperature
    case 55:  // Auto Check: Material Position
    case 56:  // Cutting Module Offset Calibration
    case 57:  // Measuring Surface
    case 58:  // Thermal Preconditioning
    
    // cases from 59-65 unavailable
    
    case 65:  // Calibrating nozzle clumping detection
        return PrintState::PREPARE;
   
*/

// Maps Bambu mc_print_stage integer values to PrintState enum.
// This covers all stages from get_stage_string() in DeviceManager.cpp.
inline PrintState stage_to_print_state(int stage, const std::string& print_status)
{
    // First check the high-level print_status string.
    // "FAILED" and "FINISH" override stage-based logic.
    if (print_status == "FAILED") return PrintState::FAILED;
    if (print_status == "FINISH") return PrintState::FINISH;

    // Map stage integers to PrintState.
    // Grouped by what they mean to the user:
    switch (stage) {
    // ── Actively printing ──────────────────────────────────────────
    case 0:   // "Printing" — main print stage
        return PrintState::RUNNING;

    // ── Preparation / calibration before first layer ───────────────
    case 3:   // Vibration compensation
    case 8:   // Calibrating dynamic flow
    case 10:  // Inspecting first layer
    case 11:  // Identifying build plate type
    case 12:  // Calibrating Micro Lidar
    case 13:  // Homing toolhead
    case 14:  // Cleaning nozzle tip
    case 18:  // Calibrating the micro lidar
    case 19:  // Calibrating flow ratio
    case 25:  // Motor noise cancellation
    case 31:  // Motor noise showoff
    case 36:  // Measuring motion precision
    case 37:  // Enhancing motion precision
    case 38:  // Measure motion accuracy
    case 39:  // Nozzle offset calibration
    case 41:  // Auto Check: Quick Release Lever
    case 42:  // Auto Check: Door and Upper Cover
    case 44:  // Auto Check: Platform
    case 45:  // Confirming BirdsEye Camera location
    case 46:  // Calibrating BirdsEye Camera
    case 51:  // Printing calibration lines
    case 52:  // Auto Check: Material
    case 53:  // Live View Camera Calibration
    case 55:  // Auto Check: Material Position
    case 56:  // Cutting Module Offset Calibration
    case 57:  // Measuring Surface
    case 65:  // Calibrating nozzle clumping detection
        return PrintState::PREPARE;

    // ── Calibration (longer / standalone calibration runs) ─────────
    // (These overlap with PREPARE visually but use the CALIBRATING icon)

    // ── Filament operations ────────────────────────────────────────
    case 4:   // Changing filament
    case 22:  // Filament unloading
    case 24:  // Filament loading
        return PrintState::FILAMENT_CHANGE;

    // ── User-triggered or automatic pauses ────────────────────────
    case 5:   // M400 pause
    case 16:  // Paused by the user
        return PrintState::PAUSE;

    // ── Cooling / heating chambers ─────────────────────────────────
    case 29:  // Cooling chamber
    case 40:  // High temperature auto bed leveling
    case 43:  // Laser Calibration
    case 50:  // Cooling heatbed
        return PrintState::PREPARE;
        
    // ── HEATING (heatbed/nozzle/chamber warming up) ──────────────────
    case 2:   // Heatbed preheating
    case 7:   // Heating nozzle
    case 15:  // Checking extruder temperature
    case 49:  // Heating chamber
    case 54:  // Waiting for heatbed to reach target temperature
    case 58:  // Thermal Preconditioning
        return PrintState::HEATING;

    // ── LEVELING (bed leveling specifically) ─────────────────────────
    case 1:   // Auto bed leveling
    case 9:   // Scanning bed surface
    case 47:  // Auto bed leveling phase 1
    case 48:  // Auto bed leveling phase 2
        return PrintState::LEVELING;

    // ── ERROR_PAUSE (hardware error, not user pause) ──────────────────
    case 6:   // Paused (filament ran out)
    case 17:  // Pause (front cover fall off)
    case 20:  // Pause (nozzle temperature malfunction)
    case 21:  // Pause (heatbed temperature malfunction)
    case 23:  // Pause (step loss)
    case 26:  // Pause (AMS offline)
    case 27:  // Pause (low speed of heatbreak fan)
    case 28:  // Pause (chamber temperature control problem)
    case 32:  // Pause (nozzle clumping)
    case 33:  // Pause (cutter error)
    case 34:  // Pause (first layer error)
    case 35:  // Pause (nozzle clog)
        return PrintState::ERROR_PAUSE;
        
    default:
        break;
    }

    // Fall through: use print_status string for anything unmapped.
    if (print_status == "RUNNING") return PrintState::RUNNING;
    if (print_status == "PAUSE")   return PrintState::PAUSE;
    if (print_status == "IDLE" || print_status == "INIT" || print_status.empty())
        return PrintState::IDLE;

    return PrintState::IDLE;
}

// ---------------------------------------------------------------------------
// PrinterStatusPoller
// ---------------------------------------------------------------------------
class PrinterStatusPoller : public wxEvtHandler
{
public:
    // icon: the widget to update (owned by MainFrame — lifetime is longer)
    explicit PrinterStatusPoller(PrintStatusIcon* icon);
    ~PrinterStatusPoller() override;

    void Start();   // begin polling
    void Stop();    // stop polling (call before MainFrame destructs)

private:
    void OnTimer(wxTimerEvent& evt);
    void PushState(PrintState state, int pct, const wxString& tooltip);

    PrintStatusIcon* m_icon;
    wxTimer          m_timer;

    // Track last pushed values to avoid redundant SetState calls. 
    PrintState  m_last_state   { PrintState::OFFLINE };
    int         m_last_pct     { -1 };

    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

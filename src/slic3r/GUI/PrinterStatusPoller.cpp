// PrinterStatusPoller.cpp

#include "PrinterStatusPoller.hpp"
#include "GUI_App.hpp"          // wxGetApp()
#include "DeviceManager.hpp"    // DeviceManager, MachineObject

#include <wx/string.h>

#include "DeviceCore/DevManager.h"

namespace Slic3r {
namespace GUI {

// Poll interval in milliseconds.
// 500ms matches StatusPanel's refresh rate — fast enough to feel responsive,
// slow enough to have no measurable CPU impact.
static constexpr int POLL_INTERVAL_MS = 500;

// ---------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(PrinterStatusPoller, wxEvtHandler)
    EVT_TIMER(wxID_ANY, PrinterStatusPoller::OnTimer)
wxEND_EVENT_TABLE()

PrinterStatusPoller::PrinterStatusPoller(PrintStatusIcon* icon)
    : m_icon(icon)
    , m_timer(this)
{
}

PrinterStatusPoller::~PrinterStatusPoller()
{
    m_timer.Stop();
}

void PrinterStatusPoller::Start()
{
    m_timer.Start(POLL_INTERVAL_MS);
}

void PrinterStatusPoller::Stop()
{
    m_timer.Stop();
}

// ---------------------------------------------------------------------------
// Main poll tick — runs on the main thread every 500ms
// ---------------------------------------------------------------------------
void PrinterStatusPoller::OnTimer(wxTimerEvent& /*evt*/)
{
    if (!m_icon) return;

    // Get the currently selected printer from DeviceManager.
    // wxGetApp().getDeviceManager() returns nullptr if the plugin is absent.
    auto* dm = wxGetApp().getDeviceManager();
    if (!dm) {
        PushState(PrintState::OFFLINE, -1, "No device manager");
        return;
    }

    MachineObject* obj = dm->get_selected_machine();
    if (!obj) {
        PushState(PrintState::OFFLINE, -1, "No printer selected");
        return;
    }

    // Printer is selected — check online status first.
    if (!obj->is_online()) {
        PushState(PrintState::OFFLINE, -1,
                  wxString::Format("Printer offline: %s",
                                   wxString::FromUTF8(obj->get_dev_name())));
        return;
    }

    // Printer is online — determine state from stage + print_status.
    PrintState state = stage_to_print_state(obj->stage_curr,
                                             obj->print_status);

    // Progress percentage — only meaningful while running.
    int pct = -1;
    if (state == PrintState::RUNNING ||
        state == PrintState::PREPARE)
    {
        pct = obj->mc_print_percent;
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
    }

    // Build a descriptive tooltip string.
    wxString tip;
    wxString printer_name = wxString::FromUTF8(obj->get_dev_name());

    switch (state) {
    case PrintState::RUNNING: {
        int secs = obj->mc_left_time;
        if (secs > 0) {
            int h = secs / 3600, m = (secs % 3600) / 60;
            tip = wxString::Format("%s \u2014 Printing %d%% \u2014 %dh %02dm left",
                                   printer_name, pct, h, m);
        } else {
            tip = wxString::Format("%s \u2014 Printing %d%%",
                                   printer_name, pct);
        }
        break;
    }
    case PrintState::PREPARE:
        // Show the specific stage name from DeviceManager.
        tip = wxString::Format("%s \u2014 %s",
                               printer_name,
                               wxString::FromUTF8(
                                   get_stage_string(obj->stage_curr).ToUTF8().data()));
        break;
    case PrintState::PAUSE:
        tip = wxString::Format("%s \u2014 Paused: %s",
                               printer_name,
                               wxString::FromUTF8(
                                   get_stage_string(obj->stage_curr).ToUTF8().data()));
        break;
    case PrintState::FILAMENT_CHANGE:
        tip = wxString::Format("%s \u2014 Filament change in progress",
                               printer_name);
        break;
    case PrintState::FINISH:
        tip = wxString::Format("%s \u2014 Print complete!", printer_name);
        break;
    case PrintState::FAILED:
        tip = wxString::Format("%s \u2014 Print failed (error %d)",
                               printer_name, obj->print_error);
        break;
    default:
        tip = wxString::Format("%s \u2014 %s",
                               printer_name,
                               wxString::FromUTF8(obj->print_status));
        break;
    }

    PushState(state, pct, tip);
}

// ---------------------------------------------------------------------------
// Only call SetState when something actually changed —
// avoids restarting the GIF animation every 500ms.
// ---------------------------------------------------------------------------
void PrinterStatusPoller::PushState(PrintState state, int pct,
                                     const wxString& tooltip)
{
    if (state == m_last_state && pct == m_last_pct)
        return;   // no change — skip

    m_last_state = state;
    m_last_pct   = pct;

    m_icon->SetState(state, pct);
    m_icon->SetStatusLabel(tooltip);
}

} // namespace GUI
} // namespace Slic3r

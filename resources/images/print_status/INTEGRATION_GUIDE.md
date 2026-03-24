# OrcaSlicer Animated Status Icons — Integration Guide

## What's in this package

```
orca_status_icons/
├── svg/                          # The 12 SVG icon files (edit these freely)
│   ├── status_idle.svg
│   ├── status_slicing.svg
│   ├── status_sliced.svg
│   ├── status_sending.svg
│   ├── status_prepare.svg
│   ├── status_running.svg
│   ├── status_pause.svg
│   ├── status_filament_change.svg
│   ├── status_calibrating.svg
│   ├── status_finish.svg
│   ├── status_failed.svg
│   └── status_offline.svg
├── src/
│   ├── PrintStatusIcon.hpp       # Widget class declaration
│   ├── PrintStatusIcon.cpp       # Widget implementation
│   ├── PrintStatusIconSVG.hpp    # Auto-generated embedded SVG strings
│   ├── MainFrame_integration_snippet.cpp   # Annotated patch examples
│   └── CMakeLists_patch.cmake    # Build system additions
└── generate_svg_header.py        # Regenerates PrintStatusIconSVG.hpp from SVGs
```

---

## How it works

### The rendering approach

OrcaSlicer is a wxWidgets app. wxWidgets has no native animated SVG widget, so
the icons use **wxWebView** — a thin wrapper around the OS's built-in web
engine (WebKit on macOS/Linux, WebView2 on Windows). Each icon is an SVG with
CSS `@keyframes` animations. The WebView is sized to match the toolbar height
and sits as a toolbar control item, exactly like a regular button would.

The WebView renders the SVG at native resolution inside the toolbar. When the
state changes, a new HTML page is loaded into the WebView (a millisecond
operation — no file I/O, it's just a string). For the `RUNNING` state, a
`<canvas>` overlay is drawn on top of the SVG via injected JavaScript to show
a circular progress arc that updates as `mc_percent` changes.

### Why not just use animated GIFs or APNG?

You could, and `wxAnimationCtrl` would handle them. But:
- GIFs are 256-colour and look terrible at small toolbar sizes.
- APNGs need pre-rendering at every DPI scale you want to support.
- SVG scales perfectly to any DPI, handles dark mode via CSS variables, and
  you can edit the animations in any text editor without recompiling anything.

### Why not wxSVGImage?

`wxSVGImage` (from wxSVG) renders static SVGs to bitmaps — it does not
run CSS animations. It's fine for static icons but won't animate.

---

## Step 1 — Prerequisites

### Windows
Install WebView2 Runtime (ships with Windows 11; for Windows 10 download from
Microsoft). The OrcaSlicer build already links `WebView2Loader.dll` for the
existing in-app browser, so no extra CMake work is needed.

### macOS
WebKit is built into the OS. No extra dependencies.

### Linux
Install `libgtk-3-dev` and `libwebkit2gtk-4.0-dev` (Ubuntu/Debian) or the
equivalent for your distro. Check that your wxWidgets build was compiled with
`--with-webview` (OrcaSlicer's CI builds already do this).

Verify wxWebView is available at runtime by checking:
```cpp
wxWebView::IsBackendAvailable(wxWebViewBackendDefault)
```
PrintStatusIcon::CreateWebView() already calls this implicitly and logs a
warning if it is unavailable, falling back gracefully (no crash).

---

## Step 2 — Copy the source files into the OrcaSlicer tree

```bash
# From the OrcaSlicer repo root:
cp orca_status_icons/src/PrintStatusIcon.hpp    src/slic3r/GUI/
cp orca_status_icons/src/PrintStatusIcon.cpp    src/slic3r/GUI/
cp orca_status_icons/src/PrintStatusIconSVG.hpp src/slic3r/GUI/

# Copy SVGs to resources (optional but recommended — lets users swap icons)
mkdir -p resources/icons/print_status
cp orca_status_icons/svg/*.svg resources/icons/print_status/
```

---

## Step 3 — Register the files in CMake

Open `src/slic3r/GUI/CMakeLists.txt`. Find the block that lists GUI `.cpp`
sources (search for `MainFrame.cpp`). Add two lines:

```cmake
# Existing lines (do not duplicate):
MainFrame.cpp
MainFrame.hpp

# ADD these:
PrintStatusIcon.cpp
PrintStatusIcon.hpp
```

Then add the install rule for the SVGs (paste from `CMakeLists_patch.cmake`):

```cmake
install(
    DIRECTORY   "${CMAKE_SOURCE_DIR}/resources/icons/print_status/"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/resources/icons/print_status"
    FILES_MATCHING PATTERN "*.svg"
)
```

---

## Step 4 — Add the member to MainFrame

Open `src/slic3r/GUI/MainFrame.hpp`. Add one line inside the class body:

```cpp
#include "PrintStatusIcon.hpp"   // add near the other GUI includes

class MainFrame : public DPIFrame {
    // ... existing members ...

    // [ADD] Animated printer status icon in toolbar
    PrintStatusIcon* m_print_status_icon { nullptr };
};
```

---

## Step 5 — Instantiate the widget in the toolbar

Open `src/slic3r/GUI/MainFrame.cpp`. Find the function that builds the top
toolbar (in OrcaSlicer this is typically `MainFrame::init_tabpanel()` or
`MainFrame::create_toolbar()`). Add after the toolbar is constructed:

```cpp
// Create the status icon widget (DPI-aware size)
m_print_status_icon = new PrintStatusIcon(m_toolbar, FromDIP(28));
m_toolbar->AddControl(m_print_status_icon);
m_toolbar->Realize();

// Clicking the icon opens the Device/Printer tab
m_print_status_icon->BindClickHandler([this]() {
    select_tab(size_t(TabPosition::tp3DEditor)); // adjust tab index
});

// Start in offline state
m_print_status_icon->SetState(PrintState::OFFLINE);
```

`FromDIP(28)` converts 28 logical pixels to the system DPI-scaled size.
At 100% DPI this is 28 px; at 200% it becomes 56 px. The SVGs are vector
so they scale cleanly regardless.

---

## Step 6 — Wire up slicer-side state changes

Still in `MainFrame.cpp`, in the function that binds slicer events (typically
near where `EVT_SLICING_UPDATE` and `EVT_PROCESS_COMPLETED` are bound):

```cpp
// Slicing is running — show progress in tooltip
Bind(EVT_SLICING_UPDATE, [this](SlicingStatusEvent& evt) {
    m_print_status_icon->SetState(PrintState::SLICING);
    m_print_status_icon->SetStatusLabel(
        wxString::Format("Slicing — %d%%", evt.status.percent));
});

// Slicing done, G-code ready to send
Bind(EVT_PROCESS_COMPLETED, [this](SlicingProcessCompletedEvent& evt) {
    if (evt.success())
        m_print_status_icon->SetState(PrintState::SLICED);
    else
        m_print_status_icon->SetState(PrintState::FAILED);
});

// Upload/send progress
Bind(EVT_PRINT_JOB_PROGRESS, [this](wxCommandEvent& evt) {
    m_print_status_icon->SetState(PrintState::SENDING, evt.GetInt());
});
```

---

## Step 7 — Wire up printer-side state changes

Open `src/slic3r/GUI/DeviceManager.cpp` (or whichever file handles the
Bambu MQTT / Klipper Moonraker polling callbacks). Find the function that
fires after parsing a new `gcode_state` / `mc_print_stage`.

The relevant function for Bambu printers is typically
`MachineObject::parse_state_changed_event()`. For Klipper it is wherever
the Moonraker WebSocket `notify_status_update` payload is processed.

Add a helper mapping and a call to update the icon:

```cpp
// Helper: convert gcode_state string to PrintState enum
static PrintState GcodeStateToPrintState(const std::string& s)
{
    if (s == "IDLE")    return PrintState::IDLE;
    if (s == "PREPARE") return PrintState::PREPARE;
    if (s == "RUNNING") return PrintState::RUNNING;
    if (s == "PAUSE")   return PrintState::PAUSE;
    if (s == "FINISH")  return PrintState::FINISH;
    if (s == "FAILED")  return PrintState::FAILED;
    return PrintState::OFFLINE;
}

// Inside your state-update handler, after parsing:
{
    PrintState ui_state = GcodeStateToPrintState(this->gcode_state);

    // Bambu mc_print_stage overrides for more granular states:
    if (this->mc_print_stage >= 2 && this->mc_print_stage <= 7)
        ui_state = PrintState::PREPARE;
    else if (this->mc_print_stage == 8)
        ui_state = PrintState::RUNNING;
    else if (this->mc_print_stage == 14)
        ui_state = PrintState::FINISH;
    else if (this->mc_print_stage == 17)
        ui_state = PrintState::PAUSE;
    else if (this->mc_print_stage == 20)
        ui_state = PrintState::FILAMENT_CHANGE;

    int progress = (ui_state == PrintState::RUNNING) ? this->mc_percent : -1;

    // wxGetApp().mainframe is the MainFrame* singleton in OrcaSlicer
    auto* frame = wxGetApp().mainframe;
    if (frame && frame->m_print_status_icon) {
        frame->m_print_status_icon->SetState(ui_state, progress);

        wxString label;
        if (ui_state == PrintState::RUNNING && progress >= 0)
            label = wxString::Format("Printing — %d%% — %s remaining",
                progress, FormatRemainingTime(this->mc_remaining_time));
        else if (ui_state == PrintState::PAUSE)
            label = "Paused — click to open printer";
        else if (ui_state == PrintState::FINISH)
            label = "Print complete!";
        else if (ui_state == PrintState::FAILED)
            label = "Print failed";
        frame->m_print_status_icon->SetStatusLabel(label);
    }
}
```

`SetState()` is thread-safe — it uses `wxControl::CallAfter()` internally, so
calling it from the MQTT worker thread is fine.

---

## Step 8 — Handle DPI changes

In `MainFrame.cpp`, find (or add) an override for `OnDpiChanged`:

```cpp
void MainFrame::OnDpiChanged(wxDPIChangedEvent& evt)
{
    if (m_print_status_icon) {
        int sz = FromDIP(28);
        m_print_status_icon->SetMinSize(wxSize(sz, sz));
        m_print_status_icon->SetMaxSize(wxSize(sz, sz));
        m_toolbar->Realize();
    }
    evt.Skip();   // let the base class handle the rest
}
```

---

## Step 9 — Regenerating the embedded SVG header

`PrintStatusIconSVG.hpp` is auto-generated from the SVG files. If you edit
any SVG or add a new one, regenerate it:

```bash
# From orca_status_icons/ directory:
python3 generate_svg_header.py

# Then copy the updated header:
cp src/PrintStatusIconSVG.hpp /path/to/OrcaSlicer/src/slic3r/GUI/
```

The generator reads every `status_*.svg` in the `svg/` folder and writes
a `wxString Get(PrintState)` function that returns the right SVG string.
The application always checks the on-disk SVG first (in `resources/icons/
print_status/`) so during development you can edit SVGs and just relaunch
without recompiling.

---

## Customising the icons

### Editing an existing animation

Open any SVG in a text editor. The CSS `@keyframes` block is at the top of the
`<style>` element. For example, to slow down the slicing scan line:

```css
/* status_slicing.svg — change 1.2s to 2.5s for a slower scan */
.scan-line { animation: scan 2.5s linear infinite; }
```

Save, run `generate_svg_header.py`, copy the header, rebuild — or just drop
the SVG into `resources/icons/print_status/` and relaunch.

### Adding a new state

1. Create `svg/status_YOUR_STATE.svg` following the same 72×72 viewBox pattern.
2. Add an entry to `STATE_MAP` in `generate_svg_header.py`.
3. Add the enum value to `PrintState` in `PrintStatusIcon.hpp`.
4. Add a `case` to `PrintStatusIcon::SVGFilename()` in `PrintStatusIcon.cpp`.
5. Add the new `gcode_state` string or `mc_print_stage` value in the mapping in
   `DeviceManager.cpp`.
6. Run the generator, copy files, rebuild.

### Sizing

The icons are 72×72 in the SVG coordinate space, but `wxWebView` scales them
to whatever `icon_size` you pass to the constructor. 28 px is typical for
OrcaSlicer's toolbar. If you find the icon blurry on a specific platform,
try passing `FromDIP(32)` — the vector SVG will simply scale up.

---

## Troubleshooting

**Icon shows as blank white box**
The wxWebView backend failed to initialise. On Linux, verify `libwebkit2gtk`
is installed and the build was configured with `--with-webview`. Check the log
for the "PrintStatusIcon: wxWebView not available" warning.

**Animations not running**
Some Linux WebKit builds disable CSS animations in off-screen WebViews. Set
`WEBKIT_DISABLE_COMPOSITING_MODE=1` as an environment variable to force
software rendering and re-test.

**Icon flickers when state changes**
This happens if you call `SetState()` very rapidly (e.g., every MQTT message).
Add a debounce: only call `SetState()` if the state or progress value actually
changed. `PrintStatusIcon::SetState()` already skips redundant updates when
state AND progress are both identical, but rapid oscillation between two states
will still flicker. In `DeviceManager.cpp`, only fire the update when
`gcode_state` changes, not on every poll tick.

**Progress arc not appearing**
The arc is drawn by injecting JavaScript after `wxEVT_WEBVIEW_LOADED` fires.
If the arc never appears, the loaded event may fire before the canvas element
is ready. You can work around this by delaying the JS injection by one event
loop tick: `CallAfter([this](){ UpdateProgressArc(m_progress); })` at the
end of `OnWebViewLoaded()`.

**High CPU on Windows**
WebView2 has a known issue where sub-100ms animation cycles (very fast CSS
animations) can spin a background thread. Slow down any animation with a
`< 0.5s` duration to at least `0.6s`.

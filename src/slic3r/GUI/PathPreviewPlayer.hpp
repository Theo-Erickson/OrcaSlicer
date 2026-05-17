#pragma once

// PathPreviewPlayer.hpp
// NEW FILE — add to: src/slic3r/GUI/
//
// A floating ImGui panel + corner trigger button that animates the
// G-code toolpath preview in OrcaSlicer's Preview GLCanvas.
//
// Integration points (see PathPreviewPlayer.cpp and GLCanvas3D edits below):
//   - GLCanvas3D owns one instance: PathPreviewPlayer m_path_player;
//   - GLCanvas3D::_render_imgui_layers() calls render_button() then render_panel()
//   - GLCanvas3D animation timer calls tick() each frame while is_playing()

#include <string>
#include "libslic3r/libslic3r.h"   // size_t, etc.

namespace Slic3r {
namespace GUI {

class GCodeViewer;  // forward — we hold a raw non-owning pointer

// ---------------------------------------------------------------------------
// PlaybackState
// All mutable animation state lives here so it can be reset cleanly.
// ---------------------------------------------------------------------------
struct PlaybackState {
    enum class Mode {
        XYPath,   // animate moves within the current layer
        Layers    // show one completed layer at a time, bottom to top
    };

    bool    playing          = false;
    int     current_layer    = 0;     // 1-based; 0 = not yet initialised
    int     total_layers     = 0;
    float   move_progress    = 0.f;   // 0.0 – 1.0 within current layer
    float   speed_multiplier = 1.f;
    Mode    mode             = Mode::XYPath;

    void reset() { *this = PlaybackState{}; }
};

// ---------------------------------------------------------------------------
// PathPreviewPlayer
// ---------------------------------------------------------------------------
class PathPreviewPlayer {
public:
    PathPreviewPlayer()  = default;
    ~PathPreviewPlayer() = default;

    // Call once after GCodeViewer has loaded a print.
    // viewer must outlive this object (owned by GLCanvas3D).
    void init(GCodeViewer* viewer, int total_layers);

    // Call every frame from GLCanvas3D::_render_imgui_layers().
    // canvas_w / canvas_h are the current GL viewport pixel dimensions.
    void render_button(float canvas_w, float canvas_h);
    void render_panel (float canvas_w, float canvas_h);

    // Called by wxTimer every ~16 ms while playing.
    // dt: elapsed seconds since last call (pass 1/60.f from a 60 fps timer).
    void tick(float dt);

    // Convenience queries used by GLCanvas3D timer management.
    bool is_playing()  const { return m_state.playing; }
    bool is_open()     const { return m_open; }

    // Reset everything (e.g. when a new file is loaded).
    void reset();

private:
    // -- UI helpers --
    void draw_transport_controls();
    void draw_mode_toggle();
    void draw_speed_combo();
    void draw_progress_scrubber();

    // -- Playback logic --
    void toggle_play();
    void toggle_open();
    void step_layer(int delta);        // +1 next layer, -1 prev layer
    void step_moves(float delta_pct);  // nudge move_progress by delta_pct
    void seek_progress(float pct);     // absolute seek within current layer
    void seek_layer(int layer);        // jump to specific layer, reset progress

    // Push the current PlaybackState into GCodeViewer's range APIs.
    void apply_to_viewer();

    // -- Members --
    GCodeViewer*   m_viewer       = nullptr;
    PlaybackState  m_state;
    bool           m_open         = false;

    // Seconds-per-full-layer at 1× speed.  Tweak to taste.
    static constexpr float k_seconds_per_layer = 2.0f;
    // Fraction of a layer advanced per step-button press.
    static constexpr float k_step_delta        = 0.05f;
};

} // namespace GUI
} // namespace Slic3r
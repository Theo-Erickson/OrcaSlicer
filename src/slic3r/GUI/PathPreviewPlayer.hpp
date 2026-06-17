#pragma once

// PathPreviewPlayer.hpp
// src/slic3r/GUI/
//
// CHANGES IN THIS VERSION:
//   - IconTheme enum: Hexagonal, Circular, Diamond
//   - Right-click context menu has radio buttons for theme selection
//   - Hovering a theme option previews it live (m_preview_theme)
//   - Theme + anchor persisted in AppConfig under [path_preview_player]

#include <string>
#include <array>
#include "libslic3r/libslic3r.h"
#include "PathPreviewExport.hpp"
#include "PathPreviewCameraMotion.hpp"

struct ImVec2;

namespace Slic3r {

class AppConfig;

namespace GUI {

class GCodeViewer;
class GLCanvas3D;

// ---------------------------------------------------------------------------
// PlaybackState
// All mutable animation state lives here so it can be reset cleanly.
// ---------------------------------------------------------------------------
struct PlaybackState {
    enum class Mode {
        XYPath,   // animate moves within the current layer
        Layers    // show one completed layer at a time, bottom to top
    };

    bool    playing           = false;
    int     current_layer     = 1;
    int     total_layers      = 0;
    float   move_progress     = 0.f;
    float   layer_timer       = 0.f;
    float   speed_multiplier  = 1.f;
    Mode    mode              = Mode::XYPath;

    void reset() { *this = PlaybackState{}; }
};

// ---------------------------------------------------------------------------
// ButtonAnchor
// ---------------------------------------------------------------------------
enum class ButtonAnchor {
    BottomRight = 0,
    BottomLeft,
    TopRight,
    TopLeft,
};

// ---------------------------------------------------------------------------
// IconTheme
// ---------------------------------------------------------------------------
enum class IconTheme {
    Hexagonal = 0,   // Hexagon shell, purple accent, pulse border animation
    Circular,        // Circle shell, teal accent, ring-glow animation
    Diamond,         // Rotated-square shell, amber accent, dash-travel animation
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
    void load_config(AppConfig* cfg);
    void save_config(AppConfig* cfg) const;

    // ── Accessors for PathPreviewExport ──────────────────────────────────────
    int   total_layers()     const { return m_state.total_layers; }
    int   current_layer()    const { return m_state.current_layer; }
    float move_progress()    const { return m_state.move_progress; }
    float speed_multiplier() const { return m_state.speed_multiplier; }

    void seek_to_layer(int layer) {
        m_state.current_layer = std::clamp(layer, 1, m_state.total_layers);
        m_state.move_progress = 0.f;
        m_state.layer_timer   = 0.f;
        apply_to_viewer();
    }

    void set_move_progress(float p) {
        m_state.move_progress = std::clamp(p, 0.f, 1.f);
        apply_to_viewer();
    }

    void force_play_from(int layer) {
        seek_to_layer(layer);
        m_state.playing = true;
    }

    void pause()                  { m_state.playing = false; }
    void set_panel_visible(bool v){ m_open = v; }

    // Call after init() to wire the GL canvas into the export system.
    void set_export_canvas(GLCanvas3D* canvas) {
        m_export.set_canvas(canvas);
        m_camera.set_canvas(canvas);
    }

    // Expose camera motion for export system to query speed settings
    PathPreviewCameraMotion& camera_motion() { return m_camera; }

private:
    // -- Panel UI --
    void draw_progress_scrubber();
    void draw_mode_toggle();
    void draw_speed_combo();

    // ---------------------------------------------------------------------------
    // Theme-dispatched icon drawing.
    // Each function draws one icon into a pre-sized region.
    //   pos  = top-left corner in screen-space (from GetCursorScreenPos or
    //          GetItemRectMin after InvisibleButton).
    //   size = square side length in pixels.
    //   col  = IM_COL32 RGBA foreground color for the icon shape.
    //   anim_t = time accumulator (seconds) used for animated states; 0 = static.
    // ---------------------------------------------------------------------------
    void draw_btn_background(ImVec2 pos, float size,
                             bool hovered, bool active, bool playing,
                             IconTheme theme, float anim_t) const;

    void draw_icon_prev_layer(ImVec2 pos, float size, unsigned int col) const;
    void draw_icon_step_back (ImVec2 pos, float size, unsigned int col) const;
    void draw_icon_play      (ImVec2 pos, float size, unsigned int col) const;
    void draw_icon_pause     (ImVec2 pos, float size, unsigned int col) const;
    void draw_icon_step_fwd  (ImVec2 pos, float size, unsigned int col) const;
    void draw_icon_next_layer(ImVec2 pos, float size, unsigned int col) const;
    void draw_icon_export    (ImVec2 pos, float size, unsigned int col) const;

    // Returns theme-appropriate foreground color given interaction state.
    unsigned int icon_color(bool hovered, bool active,
                            IconTheme theme, bool is_play_btn = false) const;

    // One complete icon button (background + icon). Returns true if clicked.
    bool transport_btn(const char* id,
                       void (PathPreviewPlayer::*icon_fn)(ImVec2, float, unsigned int) const,
                       float btn_size, bool is_play_btn, bool force_pause_icon = false);

    // -- Playback Logic --
    void toggle_play();
    void toggle_open();
    void step_layer(int delta); // +1 next layer, -1 prev layer
    void step_moves(float delta_pct); // nudge move_progress by delta_pct
    
    // Push the current PlaybackState into GCodeViewer's range APIs.
    void apply_to_viewer();

    // -- Context menu --
    ImVec2 compute_anchor_pos(float canvas_w, float canvas_h,
                               float btn_w, float btn_h) const;
    void   show_context_menu();

    // Draws a small inline preview strip of 3 buttons using the given theme.
    // Used inside the context menu while hovering a theme option.
    void draw_theme_preview(IconTheme theme, float btn_size) const;

    // -- Members --
    GCodeViewer*           m_viewer  = nullptr;
    PlaybackState          m_state;
    bool                   m_open    = false;
    PathPreviewExport      m_export;
    PathPreviewCameraMotion m_camera;

    ButtonAnchor   m_anchor        = ButtonAnchor::BottomRight;

    IconTheme      m_theme         = IconTheme::Hexagonal;
    // When the user hovers a theme option in the menu this holds the
    // candidate theme so the live buttons preview it immediately.
    // Reset to m_theme when the menu closes.
    IconTheme      m_preview_theme = IconTheme::Hexagonal;
    bool           m_in_theme_menu = false;  // true while context menu is open

    // Running time accumulator for animations (seconds).
    float          m_anim_t        = 0.f;

    int  m_speed_idx        = 3;
    bool m_speed_custom     = false;
    float m_custom_speed_val = 1.f;

    // Mode toggle display: true = icon buttons, false = text radio buttons.
    bool m_mode_icons_enabled    = true;
    // Timeline detail rulers above/below the scrubber.
    bool m_show_timeline_details = false;

    static constexpr float k_spl_xy     = 2.0f;
    static constexpr float k_spl_layers = 1.0f;
    static constexpr float k_step_delta = 0.05f;

    static constexpr int   k_num_speeds = 7;
    static constexpr float        k_speeds[k_num_speeds] =
        { 0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f };
    static constexpr const char*  k_speed_labels[k_num_speeds] =
        { "0.125x","0.25x","0.5x","1x","2x","4x","8x" };
};

} // namespace GUI
} // namespace Slic3r
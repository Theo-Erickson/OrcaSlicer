#pragma once
// PathPreviewCameraMotion.hpp  —  src/slic3r/GUI/
//
// Independent per-axis camera motion with preview-and-snap-back functionality.
// Replaces the CameraMotionMode enum with independent checkboxes per axis
// so any combination of orbit, pitch, and rise can run simultaneously.

#include <string>

struct ImVec2;

namespace Slic3r {

struct BoundingBoxf3;

namespace GUI {

class GLCanvas3D;
class PathPreviewPlayer;

// ---------------------------------------------------------------------------
// All motion is controlled by three independent boolean enables.
// Any combination is legal (e.g. orbit + rise simultaneously).
// ---------------------------------------------------------------------------
struct CameraMotionSettings {
    // Per-axis enables
    bool  enable_orbit           = false;
    bool  enable_pitch           = false;
    bool  enable_rise            = false;

    // Speeds
    float orbit_deg_per_sec      = 20.f;  // horizontal rotation °/sec
    float pitch_deg_per_sec      = 5.f;   // vertical tilt °/sec (+ = up)
    float rise_units_per_sec     = 0.f;   // extra Z offset/sec on top of layer tracking

    // Whether any motion is active
    bool any_enabled() const { return enable_orbit || enable_pitch || enable_rise; }

    // Scale camera speed by the player's speed_multiplier
    bool  affect_by_playback_rate = true;
};

// ---------------------------------------------------------------------------
// Saved camera state for the preview-and-snap-back feature.
// ---------------------------------------------------------------------------
struct SavedCameraState {
    bool  valid     = false;
    // We save the full camera by loading/storing via Camera::look_at data.
    // Since Camera has a copy operator (confirmed), we snapshot the whole thing.
};

// ---------------------------------------------------------------------------
class PathPreviewCameraMotion {
public:
    PathPreviewCameraMotion() = default;

    void set_canvas(GLCanvas3D* canvas) { m_canvas = canvas; }
    void set_player(PathPreviewPlayer* player) { m_player = player; }

    // Called every tick during live playback. dt in seconds.
    void tick(float dt, float speed_multiplier);

    // Focus + center on model centroid, zoom to fit.
    void focus_on_model();

    // Open / close the settings panel.
    void open_panel();
    void close_panel() { m_panel_open = false; }
    bool is_panel_open() const { return m_panel_open; }

    // Draw the camera icon button (caller supplies pos/size/colors).
    void render_button(ImVec2 pos, float size,
                       bool hovered, bool active,
                       unsigned int fg_col) const;

    // Draw the floating settings panel.
    void render_panel(float canvas_w, float canvas_h,
                      float anchor_x, float anchor_y);

    // Reset accumulated motion counters (call at playback start).
    void reset_motion_state();

    // Accessors for export system.
    const CameraMotionSettings& settings() const { return m_settings; }
    CameraMotionSettings&       settings()        { return m_settings; }
    float orbit_angle_accumulated() const { return m_orbit_accumulated; }

private:
    // ── Panel sections ────────────────────────────────────────────────────────
    void draw_motion_checkboxes();
    void draw_speed_controls();
    void draw_preview_controls();   // "Preview 3s then snap back"
    void draw_focus_button();
    void draw_overlay_toggles();

    // ── Preview-and-snap-back ─────────────────────────────────────────────────
    void start_preview();
    void stop_preview();       // always restores camera + pauses player

    // Apply one motion tick to the live camera.
    void apply_motion(float dt, float speed_multiplier);

    // ── Members ───────────────────────────────────────────────────────────────
    GLCanvas3D*        m_canvas = nullptr;
    PathPreviewPlayer* m_player = nullptr;

    CameraMotionSettings m_settings;

    bool  m_panel_open          = false;
    bool  m_did_sync            = false;

    // Accumulated counters (display + reset)
    float m_orbit_accumulated   = 0.f;
    float m_pitch_accumulated   = 0.f;
    float m_rise_z_accumulated  = 0.f;
    float m_rise_total_applied  = 0.f;  // total Z delta actually applied

    // Snapshot of accumulators taken at preview start (restored on snap-back)
    float m_saved_orbit_acc     = 0.f;
    float m_saved_pitch_acc     = 0.f;
    float m_saved_rise_acc      = 0.f;
    float m_saved_rise_total    = 0.f;

    // ── Preview-and-snap-back state ───────────────────────────────────────────
    // When preview is active we run motion ticks manually each frame for
    // m_preview_duration seconds, then restore the saved camera.
    bool  m_preview_active      = false;
    float m_preview_elapsed     = 0.f;
    float m_preview_duration    = 3.f;  // seconds, user-configurable

    // Saved camera state: we store position + target + view matrix components
    // by saving the Camera object itself via copy constructor.
    // Use a heap pointer so we don't need Camera.hpp included in the header.
    void* m_saved_camera        = nullptr;  // heap-allocated Camera copy

    // ── Preview timing ────────────────────────────────────────────────────────
    // The preview runs at a fixed 30fps equivalent (dt=1/30) regardless of
    // the actual tick rate so the result is deterministic.
    static constexpr float k_preview_dt = 1.f / 30.f;
    float m_preview_tick_accum  = 0.f;
};

} // namespace GUI
} // namespace Slic3r
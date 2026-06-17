#pragma once
// PathPreviewExport.hpp  —  src/slic3r/GUI/
//
// FBO-based offscreen export: renders into a dedicated framebuffer at any
// resolution without touching or stealing the main viewport.  The user can
// interact with OrcaSlicer normally while export runs.
//
// A live thumbnail preview + floating progress badge are shown in the UI.

#include <string>
#include <vector>
#include <array>

struct ImVec2;

namespace Slic3r {
namespace GUI {

class PathPreviewPlayer;
class GLCanvas3D;

// ---------------------------------------------------------------------------
enum class ExportMode { PNGSequence = 0, AnimatedGIF, MP4Direct, ScreenRecord };
enum class ExportPhase { Idle, Countdown, Recording, Rendering, Done, Cancelled };

// ---------------------------------------------------------------------------
struct ExportOrbitSettings {
    bool  enabled         = false;
    float degrees_per_sec = 20.f;
    int   axis            = 0;   // 0=horizontal 1=vertical
};

struct WatermarkSettings {
    bool         enabled   = false;
    char         text[256] = "OrcaSlicer";
    float        size      = 18.f;
    int          corner    = 2;      // 0=TL 1=TR 2=BL 3=BR
    float        opacity   = 0.75f;
    bool         shadow    = true;
};

struct MetadataSettings {
    bool  printer_name  = false;
    bool  model_name    = false;
    bool  date_time     = false;
    bool  print_time    = false;
    bool  nozzle_size   = false;
    bool  filament_type = false;
    int   corner        = 0;
    float font_size     = 13.f;
    float opacity       = 0.85f;
    bool any_enabled() const {
        return printer_name||model_name||date_time||
               print_time||nozzle_size||filament_type;
    }
};

// ---------------------------------------------------------------------------
struct ExportSettings {
    ExportMode mode         = ExportMode::PNGSequence;
    int  range_mode         = 0;   // 0=full 1=current→end 2=layer A–B
    int  layer_a            = 1;
    int  layer_b            = 999;

    // Unified resolution — used by all modes.
    // res_w=0/res_h=0 means "match viewport" (resolved at export time).
    // res_preset_idx=-1 means custom (user filled in res_w/res_h directly).
    int  res_w              = 1920;
    int  res_h              = 1080;
    int  res_preset_idx     = 3;   // index into k_res_presets (1080p default)

    // Custom resolution text input buffers (for InputInt widgets)
    int  custom_w           = 1920;
    int  custom_h           = 1080;

    int  fps                = 30;
    int  fps_idx            = 3;   // index into k_fps_values

    // GIF-specific
    int  gif_fps            = 15;
    int  gif_fps_idx        = 1;
    int  gif_dither         = 0;
    int  gif_loop           = 0;

    // Screen record
    int  countdown_secs     = 5;
    bool hide_panel         = true;
    bool loop_playback      = false;
    bool reset_to_layer1    = true;

    ExportOrbitSettings orbit;
    WatermarkSettings   watermark;
    MetadataSettings    metadata;

    std::string output_dir;
};

// ---------------------------------------------------------------------------
class PathPreviewExport {
public:
    PathPreviewExport()  = default;
    ~PathPreviewExport();

    void set_player(PathPreviewPlayer* p) { m_player = p; }
    void set_canvas(GLCanvas3D* c)        { m_canvas = c; }

    void open_dialog();
    void render_dialog(float canvas_w, float canvas_h);
    void tick(float dt);

    bool is_busy()        const { return m_phase != ExportPhase::Idle &&
                                         m_phase != ExportPhase::Done; }
    bool is_dialog_open() const { return m_dialog_open; }

private:
    // ── UI ────────────────────────────────────────────────────────────────────
    void draw_tab_bar();
    void draw_pane_png();
    void draw_pane_gif();
    void draw_pane_mp4();
    void draw_pane_rec();

    // Shared widgets
    void draw_resolution_selector(const char* uid);
    void draw_fps_selector(const char* uid, bool is_gif);
    void draw_orbit_toggle(const char* uid, ExportOrbitSettings& o);
    void draw_watermark_section();
    void draw_metadata_section();
    void draw_watermark_preview(float pw, float ph);

    // Live preview thumbnail overlay (shown inside dialog while rendering)
    void draw_live_preview();
    // Floating progress badge drawn on the canvas corner
    void draw_progress_badge(float canvas_w, float canvas_h);

    void draw_footer();
    void draw_countdown_overlay(float canvas_w, float canvas_h);

    // ── Export logic ──────────────────────────────────────────────────────────
    void start_export();
    void cancel_export();
    void finish_export();
    void close_gif();

    // Renders current gcode viewer state into an FBO, writes frame.
    // Never touches the main viewport. Returns false on GL error.
    bool render_and_write_frame();

    // Updates preview thumbnail from m_frame_buf after each frame.
    void snapshot_preview_thumbnail();

    // Advances player state by m_frame_anim_dt, updates progress.
    void advance_animation();

    void write_png_frame(int idx, const std::vector<unsigned char>& rgba);
    void apply_orbit(float dt);
    bool try_invoke_ffmpeg(const std::string& in, const std::string& out);
    std::string resolve_output_dir();
    std::string build_ffmpeg_command() const;
    std::string estimate_file_size() const;

    // Pixel compositing
    void blit_watermark(std::vector<unsigned char>& rgba, int w, int h) const;
    void blit_metadata (std::vector<unsigned char>& rgba, int w, int h) const;
    void blit_text(std::vector<unsigned char>& rgba, int img_w, int img_h,
                   const char* text, int px, int py,
                   float scale, unsigned int col_rgba, bool shadow) const;

    // ── Members ───────────────────────────────────────────────────────────────
    PathPreviewPlayer* m_player = nullptr;
    GLCanvas3D*        m_canvas = nullptr;

    bool           m_dialog_open = false;
    ExportMode     m_active_tab  = ExportMode::PNGSequence;
    ExportSettings m_settings;

    ExportPhase    m_phase           = ExportPhase::Idle;
    float          m_countdown_t     = 0.f;
    float          m_export_progress = 0.f;

    // Fast render loop state
    int   m_export_frame_idx       = 0;
    int   m_total_frames           = 0;
    float m_export_anim_time       = 0.f;
    float m_frame_anim_dt          = 0.f;
    int   m_export_start_layer     = 1;
    int   m_export_end_layer       = 1;
    float m_orbit_angle_accumulated = 0.f;

    std::string m_output_dir;
    std::string m_status_msg;

    // GIF
    void*  m_gif_writer = nullptr;
    bool   m_gif_open   = false;

    // Per-frame pixel buffer (output resolution)
    std::vector<unsigned char> m_frame_buf;

    // Live preview thumbnail (downsampled, shown in dialog during export)
    std::vector<unsigned char> m_preview_buf;
    unsigned int               m_preview_tex   = 0;
    int                        m_preview_w     = 0;
    int                        m_preview_h     = 0;
    bool                       m_preview_dirty = false;

    bool m_panel_was_hidden = false;
};

} // namespace GUI
} // namespace Slic3r
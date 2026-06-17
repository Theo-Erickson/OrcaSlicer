// PathPreviewExport.cpp  —  src/slic3r/GUI/
//
// HOW THE FBO RENDER WORKS (no viewport stealing):
//
//   Each export tick (~16ms, k_frames_per_tick frames written per tick):
//   1. Create a GL framebuffer object at the exact output resolution.
//   2. Copy the current live camera, point it at the FBO viewport.
//   3. Temporarily swap the plater camera so GCodeViewer uses it.
//   4. Call GCodeViewer::render(out_w, out_h, 0) — draws into FBO, not screen.
//   5. Restore the plater camera immediately.
//   6. glReadPixels from the FBO, vertical-flip, scale if needed.
//   7. Composite watermark/metadata, write PNG or GIF frame.
//   8. Destroy FBO.
//
//   The main viewport is NEVER dirtied, NEVER read from, NEVER altered.
//   The user can pan/zoom/interact normally during export.
//   A live thumbnail of the FBO output is shown inside the export dialog.
//   A small floating progress badge shows on the canvas corner.

#define GIF_TEMP_MALLOC malloc
#define GIF_TEMP_FREE   free
// GIF_FLIP_VERT is intentionally NOT defined — we flip rows manually.
#include "gif.h"

#include "PathPreviewExport.hpp"
#include "PathPreviewPlayer.hpp"
#include "PathPreviewCameraMotion.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <glad/gl.h>
#include <wx/dirdlg.h>
#include <wx/image.h>
#include <wx/utils.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <ctime>

namespace Slic3r {
namespace GUI {

// ── Resolution presets ────────────────────────────────────────────────────────
// Unified for all export modes. 16:9 widescreen + square social + custom.
// res_w = -1 means "Custom" — user fills text boxes.
// res_w =  0 means "Match viewport" — resolved at export start.
struct ResPair { const char* label; int w, h; };
static const ResPair k_res_presets[] = {
    { "Match viewport",   0,    0    },
    { "480p   854\xc3\x97""480",   854,  480  },
    { "720p  1280\xc3\x97""720",   1280, 720  },
    { "1080p 1920\xc3\x97""1080",  1920, 1080 },
    { "1440p 2560\xc3\x97""1440",  2560, 1440 },
    { "Square  512\xc3\x97""512",   512,  512  },
    { "Square  720\xc3\x97""720",   720,  720  },
    { "Square 1080\xc3\x97""1080",  1080, 1080 },
    { "Custom...",        -1,   -1   },
};
static constexpr int k_num_res    = 9;
static constexpr int k_custom_idx = 8;

// FPS options — same table for PNG/MP4 and GIF (GIF just defaults lower)
static const int   k_fps_values[] = { 10, 15, 24, 30, 60 };
static const char* k_fps_labels[] = { "10 fps","15 fps","24 fps","30 fps","60 fps" };
static constexpr int k_num_fps    = 5;

static const char* k_dither_labels[]  = { "Floyd-Steinberg","Bayer ordered","None (faster)" };
static const char* k_loop_labels[]    = { "Infinite","Play once","3 times" };
static const char* k_axis_labels[]    = { "Horizontal","Vertical" };
static const char* k_range_labels[]   = { "Full print","Current layer to end","Layer range" };
static const char* k_corner_labels[]  = { "Top-left","Top-right","Bottom-left","Bottom-right" };

// Frames written per UI tick. 4 * 16ms ≈ 120fps effective throughput.
static constexpr int k_frames_per_tick = 4;

// ── Destructor ────────────────────────────────────────────────────────────────
PathPreviewExport::~PathPreviewExport() { close_gif(); }

void PathPreviewExport::close_gif()
{
    if (m_gif_open && m_gif_writer) {
        GifWriter* gw = static_cast<GifWriter*>(m_gif_writer);
        GifEnd(gw);
        delete gw;
        m_gif_writer = nullptr;
        m_gif_open   = false;
    }
}

// ── open_dialog ───────────────────────────────────────────────────────────────
void PathPreviewExport::open_dialog()
{
    m_dialog_open = true;
    if (m_phase == ExportPhase::Done) {
        m_phase = ExportPhase::Idle;
        m_status_msg.clear();
        m_export_progress = 0.f;
    }
}

// ── tick ──────────────────────────────────────────────────────────────────────
void PathPreviewExport::tick(float dt)
{
    if (m_phase == ExportPhase::Countdown) {
        m_countdown_t -= dt;
        if (m_countdown_t <= 0.f) {
            m_countdown_t = 0.f;
            m_phase = ExportPhase::Recording;
            if (m_settings.hide_panel && m_player)
                m_player->set_panel_visible(false);
            if (m_player)
                m_player->force_play_from(m_export_start_layer);
        }
        return;
    }

    if (m_phase == ExportPhase::Recording) {
        if (m_settings.orbit.enabled) apply_orbit(dt);
        if (m_player && !m_player->is_playing())
            finish_export();
        return;
    }

    if (m_phase == ExportPhase::Rendering) {
        for (int i = 0;
             i < k_frames_per_tick && m_phase == ExportPhase::Rendering; ++i)
        {
            if (!render_and_write_frame()) {
                cancel_export();
                return;
            }
            advance_animation();
        }
    }

    // Upload preview thumbnail to GL texture if dirty
    if (m_preview_dirty && !m_preview_buf.empty()) {
        if (m_preview_tex == 0)
            glGenTextures(1, &m_preview_tex);
        glBindTexture(GL_TEXTURE_2D, m_preview_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     m_preview_w, m_preview_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, m_preview_buf.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_preview_dirty = false;
    }
}

// ── start_export ──────────────────────────────────────────────────────────────
void PathPreviewExport::start_export()
{
    if (!m_player || !m_canvas) {
        m_status_msg = "Error: not initialized.";
        return;
    }

    m_status_msg.clear();
    m_export_frame_idx       = 0;
    m_export_progress        = 0.f;
    m_export_anim_time       = 0.f;
    m_orbit_angle_accumulated = 0.f;
    m_preview_w = m_preview_h = 0;
    m_preview_dirty = false;

    // Layer range
    const int total = m_player->total_layers();
    m_export_start_layer = 1;
    m_export_end_layer   = total;
    if (m_settings.range_mode == 1)
        m_export_start_layer = m_player->current_layer();
    else if (m_settings.range_mode == 2) {
        m_export_start_layer = std::clamp(m_settings.layer_a, 1, total);
        m_export_end_layer   = std::clamp(m_settings.layer_b,
                                          m_export_start_layer, total);
    }

    // Screen record: real-time path, no FBO needed
    if (m_active_tab == ExportMode::ScreenRecord) {
        if (m_settings.reset_to_layer1)
            m_player->seek_to_layer(m_export_start_layer);
        m_countdown_t = static_cast<float>(m_settings.countdown_secs);
        if (m_countdown_t <= 0.f) {
            m_phase = ExportPhase::Recording;
            if (m_settings.hide_panel) m_player->set_panel_visible(false);
            m_player->force_play_from(m_export_start_layer);
        } else {
            m_phase = ExportPhase::Countdown;
        }
        return;
    }

    // Resolve resolution
    if (m_settings.res_w <= 0 || m_settings.res_h <= 0) {
        // "Match viewport"
        const Size sz = m_canvas->get_canvas_size();
        m_settings.res_w = sz.get_width();
        m_settings.res_h = sz.get_height();
    }
    // Ensure even dimensions (required by most video codecs)
    m_settings.res_w &= ~1;
    m_settings.res_h &= ~1;
    if (m_settings.res_w < 2 || m_settings.res_h < 2) {
        m_status_msg = "Invalid resolution.";
        return;
    }

    // Output FPS
    const int out_fps = (m_active_tab == ExportMode::AnimatedGIF)
                        ? m_settings.gif_fps : m_settings.fps;

    // Total frame count
    const int   num_layers      = m_export_end_layer - m_export_start_layer + 1;
    const float spl             = 2.0f / m_player->speed_multiplier();
    const float total_anim_secs = float(num_layers) * spl;
    m_total_frames  = std::max(1, int(total_anim_secs * out_fps));
    m_frame_anim_dt = total_anim_secs / float(m_total_frames);

    m_frame_buf.resize(m_settings.res_w * m_settings.res_h * 4);

    // Output directory
    m_output_dir = resolve_output_dir();
    if (m_output_dir.empty()) { m_status_msg = "Export cancelled."; return; }

    // Open GIF writer
    if (m_active_tab == ExportMode::AnimatedGIF) {
        std::string path = m_output_dir + "/path_preview.gif";
        GifWriter* gw    = new GifWriter();
        uint32_t delay   = uint32_t(100 / out_fps);
        if (!GifBegin(gw, path.c_str(),
                      uint32_t(m_settings.res_w),
                      uint32_t(m_settings.res_h), delay)) {
            delete gw;
            m_status_msg = "Could not open GIF for writing.";
            return;
        }
        m_gif_writer = gw;
        m_gif_open   = true;
    }

    m_player->seek_to_layer(m_export_start_layer);
    m_player->set_move_progress(0.f);
    m_phase      = ExportPhase::Rendering;
    m_status_msg = "Rendering...";
}

// ── render_and_write_frame ────────────────────────────────────────────────────
// Renders the gcode viewer into an offscreen FBO at output resolution.
// The main viewport is never touched.
bool PathPreviewExport::render_and_write_frame()
{
    if (!m_canvas) return false;

    const int out_w = m_settings.res_w;
    const int out_h = m_settings.res_h;
    if (out_w <= 0 || out_h <= 0) return false;

    // ── Create FBO ────────────────────────────────────────────────────────────
    GLuint fbo = 0, color_tex = 0, depth_rbo = 0;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, out_w, out_h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);

    glGenRenderbuffers(1, &depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, out_w, out_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rbo);

    const GLenum draw_bufs[] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, draw_bufs);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &color_tex);
        glDeleteRenderbuffers(1, &depth_rbo);
        glDeleteFramebuffers(1, &fbo);
        m_status_msg = "FBO creation failed.";
        return false;
    }

    // ── Set up export camera (copy of live camera) ────────────────────────────
    // Save live camera, swap in export camera for this render call only.
    Camera& live_cam  = wxGetApp().plater()->get_camera();
    Camera  saved_cam = live_cam;          // full copy — saves rotation, zoom, target

    live_cam.set_viewport(0, 0, out_w, out_h);
    live_cam.apply_viewport();
    live_cam.apply_projection(m_canvas->scene_bounding_box());

    // ── Render gcode viewer into FBO ──────────────────────────────────────────
    glViewport(0, 0, out_w, out_h);
    glClearColor(0.122f, 0.122f, 0.141f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // right_margin = 0: no slider UI margin for offscreen render
    m_canvas->get_gcode_viewer().render(out_w, out_h, 0);

    // ── Read pixels ───────────────────────────────────────────────────────────
    m_frame_buf.resize(out_w * out_h * 4);
    glReadPixels(0, 0, out_w, out_h, GL_RGBA, GL_UNSIGNED_BYTE,
                 m_frame_buf.data());

    // ── Restore live camera and default framebuffer ───────────────────────────
    live_cam = saved_cam;
    const Size sz = m_canvas->get_canvas_size();
    glViewport(0, 0, sz.get_width(), sz.get_height());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &color_tex);
    glDeleteRenderbuffers(1, &depth_rbo);
    glDeleteFramebuffers(1, &fbo);

    // ── Vertical flip: GL bottom-up → image top-down ──────────────────────────
    {
        const int stride = out_w * 4;
        std::vector<unsigned char> row(stride);
        for (int top = 0, bot = out_h - 1; top < bot; ++top, --bot) {
            unsigned char* a = m_frame_buf.data() + top * stride;
            unsigned char* b = m_frame_buf.data() + bot * stride;
            memcpy(row.data(), a, stride);
            memcpy(a, b, stride);
            memcpy(b, row.data(), stride);
        }
    }

    // ── Update live preview thumbnail ─────────────────────────────────────────
    snapshot_preview_thumbnail();

    // ── Composite overlays ────────────────────────────────────────────────────
    if (m_settings.watermark.enabled)
        blit_watermark(m_frame_buf, out_w, out_h);
    if (m_settings.metadata.any_enabled())
        blit_metadata(m_frame_buf, out_w, out_h);

    // ── Write frame ───────────────────────────────────────────────────────────
    const int out_fps = (m_active_tab == ExportMode::AnimatedGIF)
                        ? m_settings.gif_fps : m_settings.fps;

    if (m_active_tab == ExportMode::AnimatedGIF) {
        uint32_t gif_delay = uint32_t(100 / out_fps);
        GifWriter* gw = static_cast<GifWriter*>(m_gif_writer);
        GifWriteFrame(gw, m_frame_buf.data(),
                      uint32_t(out_w), uint32_t(out_h),
                      gif_delay, 8,
                      m_settings.gif_dither == 0);
    } else {
        write_png_frame(m_export_frame_idx, m_frame_buf);
    }

    // ── Orbit for next frame ──────────────────────────────────────────────────
    if (m_settings.orbit.enabled) {
        apply_orbit(m_frame_anim_dt);
        m_orbit_angle_accumulated +=
            m_settings.orbit.degrees_per_sec * m_frame_anim_dt;
    }

    return true;
}

// ── snapshot_preview_thumbnail ────────────────────────────────────────────────
void PathPreviewExport::snapshot_preview_thumbnail()
{
    const int out_w = m_settings.res_w;
    const int out_h = m_settings.res_h;
    if (out_w <= 0 || out_h <= 0 || m_frame_buf.empty()) return;

    // Fit within 280×180, preserving aspect ratio
    const float scale = std::min(280.f / out_w, 180.f / out_h);
    const int pw = std::max(1, int(out_w * scale));
    const int ph = std::max(1, int(out_h * scale));

    m_preview_buf.assign(pw * ph * 4, 0);
    for (int dy = 0; dy < ph; ++dy)
        for (int dx = 0; dx < pw; ++dx) {
            const int sx = int(float(dx) / pw * out_w);
            const int sy = int(float(dy) / ph * out_h);
            const unsigned char* src = m_frame_buf.data() + (sy * out_w + sx) * 4;
            unsigned char*       dst = m_preview_buf.data() + (dy * pw + dx) * 4;
            dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=src[3];
        }
    m_preview_w = pw;
    m_preview_h = ph;
    m_preview_dirty = true;
}

// ── advance_animation ─────────────────────────────────────────────────────────
void PathPreviewExport::advance_animation()
{
    ++m_export_frame_idx;
    m_export_anim_time += m_frame_anim_dt;

    const float spd        = m_player->speed_multiplier();
    const float spl        = 2.0f / spd;
    const int   num_layers = m_export_end_layer - m_export_start_layer + 1;

    int   layer_off = int(m_export_anim_time / spl);
    float move_prog = (m_export_anim_time / spl) - layer_off;
    layer_off = std::min(layer_off, num_layers - 1);
    move_prog = std::clamp(move_prog, 0.f, 1.f);

    m_player->seek_to_layer(m_export_start_layer + layer_off);
    m_player->set_move_progress(move_prog);

    m_export_progress = float(m_export_frame_idx) /
                        float(std::max(m_total_frames, 1));

    char buf[128];
    snprintf(buf, sizeof(buf), "Layer %d / %d — frame %d / %d",
             m_export_start_layer + layer_off, m_export_end_layer,
             m_export_frame_idx, m_total_frames);
    m_status_msg = buf;

    if (m_export_frame_idx >= m_total_frames)
        finish_export();
}

// ── write_png_frame ───────────────────────────────────────────────────────────
void PathPreviewExport::write_png_frame(int idx,
    const std::vector<unsigned char>& rgba)
{
    char filename[512];
    snprintf(filename, sizeof(filename),
             "%s/frame_%04d.png", m_output_dir.c_str(), idx + 1);
    const int w = m_settings.res_w, h = m_settings.res_h, n = w * h;
    unsigned char* rgb = new unsigned char[n * 3];
    unsigned char* alp = new unsigned char[n];
    for (int i = 0; i < n; ++i) {
        rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1];
        rgb[i*3+2]=rgba[i*4+2]; alp[i]=rgba[i*4+3];
    }
    wxImage img(w, h, rgb, alp, false);
    img.SaveFile(wxString::FromUTF8(filename), wxBITMAP_TYPE_PNG);
}

// ── finish_export ─────────────────────────────────────────────────────────────
void PathPreviewExport::finish_export()
{
    close_gif();
    if (m_panel_was_hidden && m_player) {
        m_player->set_panel_visible(true);
        m_panel_was_hidden = false;
    }
    m_phase           = ExportPhase::Done;
    m_export_progress = 1.f;

    if (m_active_tab == ExportMode::ScreenRecord) {
        m_status_msg = "Playback finished.";
    } else if (m_active_tab == ExportMode::MP4Direct) {
        std::string pat = m_output_dir + "/frame_%04d.png";
        std::string mp4 = m_output_dir + "/path_preview.mp4";
        if (try_invoke_ffmpeg(pat, mp4))
            m_status_msg = "MP4 saved: path_preview.mp4";
        else {
            m_status_msg = "FFmpeg not found — frames saved. Copy command below.";
            ImGui::SetClipboardText(build_ffmpeg_command().c_str());
        }
        wxLaunchDefaultApplication(wxString::FromUTF8(m_output_dir));
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Done — %d frames saved.", m_export_frame_idx);
        m_status_msg = buf;
        wxLaunchDefaultApplication(wxString::FromUTF8(m_output_dir));
    }
}

// ── cancel_export ─────────────────────────────────────────────────────────────
void PathPreviewExport::cancel_export()
{
    close_gif();
    if (m_panel_was_hidden && m_player) {
        m_player->set_panel_visible(true);
        m_panel_was_hidden = false;
    }
    if (m_player) m_player->pause();
    m_phase            = ExportPhase::Idle;
    m_export_progress  = 0.f;
    m_export_frame_idx = 0;
    m_status_msg       = "Cancelled.";
}

// ── Utilities ─────────────────────────────────────────────────────────────────
void PathPreviewExport::apply_orbit(float dt)
{
    // If the player has camera motion configured, use its orbit settings.
    // This means the export and live preview share the same camera behaviour.
    if (m_player) {
        const auto& cam_settings = m_player->camera_motion().settings();
        if (cam_settings.any_enabled()) {
            m_player->camera_motion().tick(dt, m_player->speed_multiplier());
            return;
        }
    }
    // Fallback: use export-specific orbit settings
    if (!m_settings.orbit.enabled) return;
    Camera& cam = wxGetApp().plater()->get_camera();
    const float rad = m_settings.orbit.degrees_per_sec * dt
                      * float(M_PI) / 180.f;
    if (m_settings.orbit.axis == 0) cam.rotate_on_sphere(rad, 0.f, true);
    else                            cam.rotate_on_sphere(0.f, rad, true);
}

std::string PathPreviewExport::resolve_output_dir()
{
    if (!m_settings.output_dir.empty()) return m_settings.output_dir;
    wxDirDialog dlg(nullptr, "Choose export folder", wxGetHomeDir(),
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return {};
    return dlg.GetPath().ToUTF8().data();
}

std::string PathPreviewExport::build_ffmpeg_command() const
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "ffmpeg -framerate %d -i frame_%%04d.png "
        "-c:v libx264 -pix_fmt yuv420p output.mp4",
        m_settings.fps);
    return buf;
}

bool PathPreviewExport::try_invoke_ffmpeg(const std::string& in,
                                           const std::string& out)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -framerate %d -i \"%s\" "
        "-c:v libx264 -pix_fmt yuv420p \"%s\"",
        m_settings.fps, in.c_str(), out.c_str());
    return system(cmd) == 0;
}

std::string PathPreviewExport::estimate_file_size() const
{
    const int out_fps = (m_active_tab == ExportMode::AnimatedGIF)
                        ? m_settings.gif_fps : m_settings.fps;
    const int   num_layers = m_export_end_layer - m_export_start_layer + 1;
    const float secs = float(num_layers) * 2.f / m_player->speed_multiplier();
    const float frames = secs * out_fps;
    const float px = float(m_settings.res_w * m_settings.res_h);

    float mb;
    if (m_active_tab == ExportMode::AnimatedGIF)
        mb = px * frames * 0.5f / (1024.f * 1024.f);  // GIF rough
    else
        mb = px * frames * 4.f / (1024.f * 1024.f);   // PNG uncompressed

    char buf[128];
    snprintf(buf, sizeof(buf),
             "~%.0f frames  |  est. ~%.0f MB  |  %d\xc3\x97%d @ %d fps",
             frames, mb, m_settings.res_w, m_settings.res_h, out_fps);
    return buf;
}

// ── UI ────────────────────────────────────────────────────────────────────────

void PathPreviewExport::render_dialog(float canvas_w, float canvas_h)
{
    if (m_phase == ExportPhase::Countdown || m_phase == ExportPhase::Recording)
        draw_countdown_overlay(canvas_w, canvas_h);

    // Always draw the progress badge during rendering, even if dialog is closed
    if (m_phase == ExportPhase::Rendering)
        draw_progress_badge(canvas_w, canvas_h);

    if (!m_dialog_open) return;

    const float DLG_W = std::clamp(canvas_w * 0.62f, 680.f, canvas_w * 0.92f);
    const float DLG_H = std::clamp(canvas_h * 0.82f, 520.f, canvas_h * 0.92f);

    ImGui::SetNextWindowPos(
        ImVec2(canvas_w*0.5f - DLG_W*0.5f, canvas_h*0.5f - DLG_H*0.5f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(600.f, 480.f), ImVec2(canvas_w*0.95f, canvas_h*0.95f));
    ImGui::SetNextWindowSize(ImVec2(DLG_W, DLG_H), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.97f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(0.f, 0.f));

    bool open = m_dialog_open;
    if (ImGui::Begin("Export animation##ppp_export", &open, flags)) {
        // Window X button pressed — hide the dialog but do NOT cancel export.
        // Export continues running in the background; badge shows progress.
        if (!open) { m_dialog_open = false; }

        draw_tab_bar();

        // Body: left panel (settings) | right panel (live preview, visible during render)
        const float body_h = ImGui::GetContentRegionAvail().y - 74.f;
        const bool  show_preview = (m_phase == ExportPhase::Rendering ||
                                    m_phase == ExportPhase::Done) &&
                                   m_preview_tex != 0;
        const float preview_col_w = show_preview ? 300.f : 0.f;
        const float settings_col_w = ImGui::GetContentRegionAvail().x
                                     - preview_col_w - (show_preview ? 8.f : 0.f);

        // Settings column
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 12.f));
        ImGui::BeginChild("##export_settings", ImVec2(settings_col_w, body_h),
                          false, ImGuiWindowFlags_None);
        switch (m_active_tab) {
        case ExportMode::PNGSequence:  draw_pane_png(); break;
        case ExportMode::AnimatedGIF:  draw_pane_gif(); break;
        case ExportMode::MP4Direct:    draw_pane_mp4(); break;
        case ExportMode::ScreenRecord: draw_pane_rec(); break;
        }
        ImGui::Dummy(ImVec2(0.f, 10.f));
        ImGui::EndChild();
        ImGui::PopStyleVar();

        // Live preview column
        if (show_preview) {
            ImGui::SameLine(0.f, 8.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
            ImGui::BeginChild("##export_preview", ImVec2(preview_col_w, body_h),
                              false, ImGuiWindowFlags_None);
            draw_live_preview();
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        draw_footer();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ── draw_tab_bar ──────────────────────────────────────────────────────────────
void PathPreviewExport::draw_tab_bar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    struct Tab { const char* label; ExportMode mode; };
    static const Tab tabs[] = {
        { "  PNG sequence  ", ExportMode::PNGSequence  },
        { "  Animated GIF  ", ExportMode::AnimatedGIF  },
        { "  MP4 (FFmpeg)  ", ExportMode::MP4Direct    },
        { "  Screen record ", ExportMode::ScreenRecord },
    };
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    for (const auto& t : tabs) {
        bool active = (m_active_tab == t.mode);
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? ImVec4(0.14f,0.14f,0.18f,1.f) : ImVec4(0.09f,0.09f,0.11f,1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f,0.20f,0.25f,1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.24f,0.24f,0.30f,1.f));
        if (ImGui::Button(t.label, ImVec2(0.f, 36.f))) m_active_tab = t.mode;
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::PopStyleVar();
}

// ── draw_resolution_selector ─────────────────────────────────────────────────
void PathPreviewExport::draw_resolution_selector(const char* uid)
{
    const bool is_custom = (m_settings.res_preset_idx == k_custom_idx);
    const char* preview_label = is_custom
        ? "Custom..."
        : k_res_presets[m_settings.res_preset_idx].label;

    ImGui::Text("Resolution");
    ImGui::SameLine(140.f);
    ImGui::PushItemWidth(200.f);
    char combo_id[32]; snprintf(combo_id, sizeof(combo_id), "##res_%s", uid);
    if (ImGui::BeginCombo(combo_id, preview_label)) {
        for (int i = 0; i < k_num_res; ++i) {
            bool sel = (m_settings.res_preset_idx == i);
            if (ImGui::Selectable(k_res_presets[i].label, sel)) {
                m_settings.res_preset_idx = i;
                if (i != k_custom_idx) {
                    m_settings.res_w = k_res_presets[i].w;
                    m_settings.res_h = k_res_presets[i].h;
                } else {
                    // Keep whatever the user last had in custom boxes
                    m_settings.res_w = m_settings.custom_w;
                    m_settings.res_h = m_settings.custom_h;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    if (is_custom) {
        // Text boxes for custom resolution
        ImGui::Spacing();
        char w_id[32], h_id[32];
        snprintf(w_id, sizeof(w_id), "##cw_%s", uid);
        snprintf(h_id, sizeof(h_id), "##ch_%s", uid);

        ImGui::SetCursorPosX(140.f);
        ImGui::PushItemWidth(80.f);
        if (ImGui::InputInt(w_id, &m_settings.custom_w, 0)) {
            m_settings.custom_w = std::clamp(m_settings.custom_w, 2, 7680);
            m_settings.custom_w &= ~1; // keep even
            m_settings.res_w = m_settings.custom_w;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine(); ImGui::TextDisabled("\xc3\x97");
        ImGui::SameLine();
        ImGui::PushItemWidth(80.f);
        if (ImGui::InputInt(h_id, &m_settings.custom_h, 0)) {
            m_settings.custom_h = std::clamp(m_settings.custom_h, 2, 4320);
            m_settings.custom_h &= ~1;
            m_settings.res_h = m_settings.custom_h;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine(); ImGui::TextDisabled("px");
    }
}

// ── draw_fps_selector ─────────────────────────────────────────────────────────
void PathPreviewExport::draw_fps_selector(const char* uid, bool is_gif)
{
    int& idx = is_gif ? m_settings.gif_fps_idx : m_settings.fps_idx;
    int& val = is_gif ? m_settings.gif_fps     : m_settings.fps;

    ImGui::Text(is_gif ? "GIF frame rate" : "Frame rate");
    ImGui::SameLine(140.f);
    ImGui::PushItemWidth(120.f);
    char id[32]; snprintf(id, sizeof(id), "##fps_%s", uid);
    if (ImGui::BeginCombo(id, k_fps_labels[idx])) {
        for (int i = 0; i < k_num_fps; ++i)
            if (ImGui::Selectable(k_fps_labels[i], idx == i)) {
                idx = i; val = k_fps_values[i];
            }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (is_gif && ImGui::IsItemHovered())
        ImGui::SetTooltip("Controls GIF playback speed only.\n"
                          "Export always renders at maximum speed.");
}

// ── draw_orbit_toggle ─────────────────────────────────────────────────────────
void PathPreviewExport::draw_orbit_toggle(const char* uid,
                                           ExportOrbitSettings& orbit)
{
    char lbl[64];
    snprintf(lbl, sizeof(lbl), "Camera orbit##%s", uid);
    ImGui::Checkbox(lbl, &orbit.enabled);
    if (!orbit.enabled) return;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.f);
    ImGui::Text("Speed"); ImGui::SameLine(160.f);
    snprintf(lbl, sizeof(lbl), "##orb_spd_%s", uid);
    ImGui::PushItemWidth(70.f);
    ImGui::InputFloat(lbl, &orbit.degrees_per_sec, 0.f, 0.f, "%.0f");
    ImGui::PopItemWidth();
    ImGui::SameLine(); ImGui::TextDisabled("deg/sec");

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.f);
    ImGui::Text("Axis"); ImGui::SameLine(160.f);
    snprintf(lbl, sizeof(lbl), "##orb_ax_%s", uid);
    ImGui::PushItemWidth(130.f);
    if (ImGui::BeginCombo(lbl, k_axis_labels[orbit.axis])) {
        for (int i = 0; i < 2; ++i)
            if (ImGui::Selectable(k_axis_labels[i], orbit.axis == i))
                orbit.axis = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
}

// ── Shared range selector (file-local helper) ─────────────────────────────────
static void range_selector(ExportSettings& s)
{
    ImGui::Text("Range");
    ImGui::SameLine(140.f);
    ImGui::PushItemWidth(200.f);
    if (ImGui::BeginCombo("##range", k_range_labels[s.range_mode])) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(k_range_labels[i], s.range_mode == i))
                s.range_mode = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (s.range_mode == 2) {
        ImGui::SetCursorPosX(140.f);
        ImGui::PushItemWidth(60.f);
        ImGui::InputInt("##la", &s.layer_a, 0);
        ImGui::PopItemWidth();
        ImGui::SameLine(); ImGui::TextDisabled("to");
        ImGui::SameLine();
        ImGui::PushItemWidth(60.f);
        ImGui::InputInt("##lb", &s.layer_b, 0);
        ImGui::PopItemWidth();
    }
}

// ── draw_pane_png ─────────────────────────────────────────────────────────────
void PathPreviewExport::draw_pane_png()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.8f,0.5f,1.f));
    ImGui::TextWrapped("Renders into an offscreen framebuffer — "
                       "main viewport is unaffected.");
    ImGui::PopStyleColor();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::TextDisabled("Frame range");
    ImGui::Spacing();
    range_selector(m_settings);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Output");
    ImGui::Spacing();
    draw_resolution_selector("png");
    ImGui::Spacing();
    draw_fps_selector("png", false);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    draw_orbit_toggle("png", m_settings.orbit);
    draw_watermark_section();
    draw_metadata_section();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("FFmpeg command (run after export)");
    ImGui::Spacing();
    std::string cmd = build_ffmpeg_command();
    char cmd_buf[512];
    strncpy(cmd_buf, cmd.c_str(), 511); cmd_buf[511] = 0;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 60.f);
    ImGui::InputText("##ffcmd", cmd_buf, sizeof(cmd_buf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Copy")) ImGui::SetClipboardText(cmd_buf);
}

// ── draw_pane_gif ─────────────────────────────────────────────────────────────
void PathPreviewExport::draw_pane_gif()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.8f,0.5f,1.f));
    ImGui::TextWrapped("Renders offscreen at maximum speed. "
                       "GIF delay is set by fps below.");
    ImGui::PopStyleColor();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::TextDisabled("Frame range");
    ImGui::Spacing();
    range_selector(m_settings);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Output");
    ImGui::Spacing();
    draw_resolution_selector("gif");
    ImGui::Spacing();

    ImGui::TextDisabled("Quality");
    ImGui::Spacing();
    draw_fps_selector("gif", true);
    ImGui::Spacing();

    ImGui::Text("Dithering");
    ImGui::SameLine(140.f);
    ImGui::PushItemWidth(180.f);
    if (ImGui::BeginCombo("##gif_dith", k_dither_labels[m_settings.gif_dither])) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(k_dither_labels[i], m_settings.gif_dither == i))
                m_settings.gif_dither = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Text("Loop");
    ImGui::SameLine(140.f);
    ImGui::PushItemWidth(120.f);
    if (ImGui::BeginCombo("##gif_loop", k_loop_labels[m_settings.gif_loop])) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(k_loop_labels[i], m_settings.gif_loop == i))
                m_settings.gif_loop = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    draw_orbit_toggle("gif", m_settings.orbit);
    draw_watermark_section();
    draw_metadata_section();
}

// ── draw_pane_mp4 ─────────────────────────────────────────────────────────────
void PathPreviewExport::draw_pane_mp4()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.8f,0.5f,1.f));
    ImGui::TextWrapped("Renders PNG frames offscreen, then invokes FFmpeg "
                       "to produce an MP4.");
    ImGui::PopStyleColor();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::TextDisabled("Frame range");
    ImGui::Spacing();
    range_selector(m_settings);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Output");
    ImGui::Spacing();
    draw_resolution_selector("mp4");
    ImGui::Spacing();
    draw_fps_selector("mp4", false);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    draw_orbit_toggle("mp4", m_settings.orbit);
    draw_watermark_section();
    draw_metadata_section();
}

// ── draw_pane_rec ─────────────────────────────────────────────────────────────
void PathPreviewExport::draw_pane_rec()
{
    ImGui::TextDisabled("Countdown before start");
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f,0.10f,0.14f,1.f));
    ImGui::BeginChild("##cd_box", ImVec2(0.f, 62.f), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos({14.f, 10.f});
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("%d", m_settings.countdown_secs);
    ImGui::SetWindowFontScale(1.f);
    ImGui::SameLine(70.f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
    ImGui::BeginGroup();
    ImGui::TextDisabled("seconds before animation begins");
    ImGui::PushItemWidth(200.f);
    float cd_f = float(m_settings.countdown_secs);
    if (ImGui::SliderFloat("##cd", &cd_f, 0.f, 10.f, "%.0f"))
        m_settings.countdown_secs = int(cd_f);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Options");
    ImGui::Spacing();
    ImGui::Checkbox("Hide path preview panel during recording", &m_settings.hide_panel);
    ImGui::Spacing();
    ImGui::Checkbox("Loop playback",             &m_settings.loop_playback);
    ImGui::Spacing();
    ImGui::Checkbox("Reset to layer 1 before starting", &m_settings.reset_to_layer1);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Suggested screen recording tools");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f,0.10f,0.14f,1.f));
    ImGui::BeginChild("##tools_box", ImVec2(0.f, 70.f), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos({10.f,  8.f}); ImGui::TextDisabled("Windows:  OBS Studio, ShareX, Xbox Game Bar (Win+G)");
    ImGui::SetCursorPos({10.f, 28.f}); ImGui::TextDisabled("macOS:    QuickTime Player \xe2\x80\x94 New screen recording");
    ImGui::SetCursorPos({10.f, 48.f}); ImGui::TextDisabled("Linux:    OBS Studio, Kazam, SimpleScreenRecorder");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    draw_orbit_toggle("rec", m_settings.orbit);
}

// ── draw_watermark_section ────────────────────────────────────────────────────
void PathPreviewExport::draw_watermark_section()
{
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Checkbox("Text watermark##wm", &m_settings.watermark.enabled);
    if (!m_settings.watermark.enabled) return;
    ImGui::Spacing();

    ImGui::Text("Text"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(220.f);
    ImGui::InputText("##wm_text", m_settings.watermark.text,
                     sizeof(m_settings.watermark.text));
    ImGui::PopItemWidth();

    ImGui::Text("Size"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(140.f);
    ImGui::SliderFloat("##wm_size", &m_settings.watermark.size, 8.f, 72.f, "%.0f px");
    ImGui::PopItemWidth();

    ImGui::Text("Corner"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(130.f);
    if (ImGui::BeginCombo("##wm_corner", k_corner_labels[m_settings.watermark.corner])) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(k_corner_labels[i], m_settings.watermark.corner == i))
                m_settings.watermark.corner = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Text("Opacity"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(140.f);
    ImGui::SliderFloat("##wm_op", &m_settings.watermark.opacity, 0.1f, 1.f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::Checkbox("Drop shadow##wm_sh", &m_settings.watermark.shadow);
    ImGui::Spacing();
    ImGui::TextDisabled("Preview:");
    draw_watermark_preview(240.f, 76.f);
}

void PathPreviewExport::draw_watermark_preview(float pw, float ph)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x+pw, p.y+ph}, IM_COL32(30,30,35,255), 4.f);
    dl->AddRect(p, {p.x+pw, p.y+ph}, IM_COL32(60,60,70,200), 4.f);
    const char* text = m_settings.watermark.text;
    const float fsz  = m_settings.watermark.size;
    ImVec2 ts = ImGui::CalcTextSize(text);
    const float scale = fsz / ImGui::GetFontSize();
    ts.x *= scale; ts.y *= scale;
    const float margin = 8.f;
    ImVec2 tp;
    switch (m_settings.watermark.corner) {
    case 0: tp={p.x+margin,       p.y+margin};           break;
    case 1: tp={p.x+pw-ts.x-margin, p.y+margin};         break;
    case 2: tp={p.x+margin,       p.y+ph-ts.y-margin};   break;
    default:tp={p.x+pw-ts.x-margin, p.y+ph-ts.y-margin}; break;
    }
    const unsigned int alpha = unsigned(m_settings.watermark.opacity * 255);
    if (m_settings.watermark.shadow)
        dl->AddText(ImGui::GetFont(), fsz, {tp.x+1.f,tp.y+1.f},
                    IM_COL32(0,0,0,alpha/2), text);
    dl->AddText(ImGui::GetFont(), fsz, tp, IM_COL32(255,255,255,alpha), text);
    ImGui::Dummy({pw, ph});
}

// ── draw_metadata_section ─────────────────────────────────────────────────────
void PathPreviewExport::draw_metadata_section()
{
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Info overlay (added to each frame)");
    ImGui::Spacing();
    ImGui::Columns(2, "##meta_cols", false);
    ImGui::Checkbox("Printer name",  &m_settings.metadata.printer_name);
    ImGui::Checkbox("Model name",    &m_settings.metadata.model_name);
    ImGui::Checkbox("Date & time",   &m_settings.metadata.date_time);
    ImGui::NextColumn();
    ImGui::Checkbox("Print time",    &m_settings.metadata.print_time);
    ImGui::Checkbox("Nozzle size",   &m_settings.metadata.nozzle_size);
    ImGui::Checkbox("Filament type", &m_settings.metadata.filament_type);
    ImGui::Columns(1);
    if (!m_settings.metadata.any_enabled()) return;
    ImGui::Spacing();

    ImGui::Text("Corner"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(130.f);
    if (ImGui::BeginCombo("##meta_corner", k_corner_labels[m_settings.metadata.corner])) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(k_corner_labels[i], m_settings.metadata.corner == i))
                m_settings.metadata.corner = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Text("Font size"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(100.f);
    ImGui::SliderFloat("##meta_fs", &m_settings.metadata.font_size,
                       8.f, 32.f, "%.0f px");
    ImGui::PopItemWidth();

    ImGui::Text("Opacity"); ImGui::SameLine(110.f);
    ImGui::PushItemWidth(120.f);
    ImGui::SliderFloat("##meta_op", &m_settings.metadata.opacity,
                       0.1f, 1.f, "%.2f");
    ImGui::PopItemWidth();
}

// ── draw_live_preview ─────────────────────────────────────────────────────────
// Shows a thumbnail of the last rendered frame inside the dialog.
void PathPreviewExport::draw_live_preview()
{
    if (m_preview_tex == 0 || m_preview_w <= 0) return;

    ImGui::TextDisabled("Live preview");
    ImGui::Spacing();

    // Draw thumbnail image
    const float max_w = ImGui::GetContentRegionAvail().x;
    const float max_h = 200.f;
    const float aspect = float(m_preview_w) / float(m_preview_h);
    float dw = max_w, dh = max_w / aspect;
    if (dh > max_h) { dh = max_h; dw = max_h * aspect; }

    ImGui::Image(
        reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(m_preview_tex)),
        ImVec2(dw, dh));

    ImGui::Spacing();
    ImGui::TextDisabled("Frame %d / %d", m_export_frame_idx, m_total_frames);

    // Progress bar
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.10f,0.55f,0.35f,1.f));
    ImGui::ProgressBar(m_export_progress,
                       ImVec2(ImGui::GetContentRegionAvail().x, 6.f), "");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", estimate_file_size().c_str());

    // Cancel button inside preview column
    if (is_busy()) {
        ImGui::Spacing();
        if (ImGui::Button("Cancel export", ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
            cancel_export();
    }
}

// ── draw_progress_badge ───────────────────────────────────────────────────────
// Small floating overlay on the canvas corner during export (even when dialog closed)
void PathPreviewExport::draw_progress_badge(float canvas_w, float canvas_h)
{
    if (m_phase != ExportPhase::Rendering) return;

    constexpr float W = 220.f, H = 52.f;
    ImGui::SetNextWindowPos(
        ImVec2(canvas_w - W - 12.f, canvas_h - H - 68.f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::Begin("##export_badge", nullptr, flags);

    ImGui::SetCursorPos({10.f, 8.f});
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 0.55f, 1.f));
    ImGui::Text("Exporting  %d / %d", m_export_frame_idx, m_total_frames);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos({10.f, 30.f});
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.10f,0.55f,0.35f,1.f));
    ImGui::ProgressBar(m_export_progress, ImVec2(W - 20.f, 8.f), "");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar();
}

// ── draw_footer ───────────────────────────────────────────────────────────────
void PathPreviewExport::draw_footer()
{
    ImGui::Separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 6.f));

    // Status text
    const float win_w    = ImGui::GetWindowWidth();
    const float btn_area = 80.f + 8.f + 140.f + 32.f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + win_w - btn_area);
    ImGui::TextDisabled("%s", m_status_msg.empty() ? " " : m_status_msg.c_str());
    ImGui::PopTextWrapPos();

    // Right-aligned buttons
    const float action_x = win_w - 140.f - 16.f;
    const float cancel_x = action_x - 88.f;
    const float btn_y    = ImGui::GetCursorPosY()
                           - ImGui::GetTextLineHeightWithSpacing() - 4.f;

    bool busy = is_busy();

    ImGui::SetCursorPos(ImVec2(cancel_x, btn_y));
    if (busy) {
        if (ImGui::Button("Cancel##exp", {80.f, 0.f})) cancel_export();
    } else {
        if (ImGui::Button("Close##exp",  {80.f, 0.f})) {
            m_dialog_open = false;
            m_status_msg.clear();
        }
    }

    ImGui::SameLine(0.f, 8.f);

    const char* lbl =
        (m_active_tab == ExportMode::ScreenRecord) ? "Start countdown" :
        (m_active_tab == ExportMode::AnimatedGIF)  ? "Export GIF"      :
        (m_active_tab == ExportMode::MP4Direct)    ? "Export MP4"      :
                                                     "Export frames";

    ImGui::PushStyleColor(ImGuiCol_Button,
        busy ? ImVec4(0.06f,0.35f,0.22f,0.5f) : ImVec4(0.10f,0.55f,0.35f,1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f,0.45f,0.28f,1.f));
    if (!busy && ImGui::Button(lbl, {140.f, 0.f})) start_export();
    ImGui::PopStyleColor(2);

    ImGui::PopStyleVar();
}

// ── draw_countdown_overlay ────────────────────────────────────────────────────
void PathPreviewExport::draw_countdown_overlay(float canvas_w, float canvas_h)
{
    constexpr float OVL_W=240.f, OVL_H=72.f;
    ImGui::SetNextWindowPos(
        ImVec2(canvas_w*0.5f-OVL_W*0.5f, 20.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize({OVL_W, OVL_H});
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::Begin("##cd_overlay", nullptr, flags);
    if (m_phase == ExportPhase::Countdown) {
        ImGui::SetCursorPos({14.f,12.f});
        ImGui::TextDisabled("Recording starts in");
        ImGui::SetCursorPos({14.f,34.f});
        ImGui::SetWindowFontScale(1.8f);
        ImGui::Text("%d", int(std::ceil(m_countdown_t)));
        ImGui::SetWindowFontScale(1.f);
        const float frac = 1.f - (m_countdown_t /
            std::max(1.f, float(m_settings.countdown_secs)));
        ImGui::SetCursorPos({0.f, OVL_H-4.f});
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f,0.30f,0.20f,1.f));
        ImGui::ProgressBar(frac, {OVL_W, 4.f}, "");
        ImGui::PopStyleColor();
    } else {
        ImGui::SetCursorPos({14.f,24.f});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f,0.30f,0.25f,1.f));
        ImGui::Text("Recording \xe2\x80\x94 animation playing");
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ── Pixel compositing ─────────────────────────────────────────────────────────
void PathPreviewExport::blit_text(std::vector<unsigned char>& rgba,
                                   int img_w, int img_h,
                                   const char* text,
                                   int px, int py,
                                   float scale,
                                   unsigned int col_rgba,
                                   bool shadow) const
{
    if (!text || !*text) return;
    const unsigned char cr=(col_rgba>>24)&0xFF, cg=(col_rgba>>16)&0xFF,
                        cb=(col_rgba>> 8)&0xFF, ca=(col_rgba     )&0xFF;
    ImFont* font = ImGui::GetIO().Fonts->Fonts.Size > 0
                 ? ImGui::GetIO().Fonts->Fonts[0] : nullptr;
    if (!font) return;
    int aw=0, ah=0; unsigned char* ap=nullptr;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&ap, &aw, &ah);
    if (!ap) return;

    float cx = float(px);
    for (const char* c = text; *c; ++c) {
        const ImFontGlyph* g = font->FindGlyph((unsigned char)*c);
        if (!g) { cx += scale * font->FallbackAdvanceX; continue; }
        const int gw = int((g->X1-g->X0)*scale);
        const int gh = int((g->Y1-g->Y0)*scale);
        const int ox = int(cx + g->X0*scale);
        const int oy = int(py + g->Y0*scale);
        for (int gy=0; gy<gh; ++gy) for (int gx=0; gx<gw; ++gx) {
            float u = g->U0 + (g->U1-g->U0)*(float(gx)/gw);
            float v = g->V0 + (g->V1-g->V0)*(float(gy)/gh);
            int ax = std::clamp(int(u*aw), 0, aw-1);
            int ay = std::clamp(int(v*ah), 0, ah-1);
            const unsigned char cov = ap[(ay*aw+ax)*4+3];
            if (cov < 8) continue;
            const unsigned char alpha = (unsigned char)((cov*ca)/255);
            auto stamp = [&](int dx, int dy, unsigned char r,
                              unsigned char g2, unsigned char b,
                              unsigned char a) {
                if (dx<0||dx>=img_w||dy<0||dy>=img_h) return;
                unsigned char* d = rgba.data()+(dy*img_w+dx)*4;
                float af = a/255.f;
                d[0]=(unsigned char)(d[0]*(1-af)+r*af);
                d[1]=(unsigned char)(d[1]*(1-af)+g2*af);
                d[2]=(unsigned char)(d[2]*(1-af)+b*af);
                d[3]=255;
            };
            if (shadow) stamp(ox+gx+1, oy+gy+1, 0,0,0, alpha/2);
            stamp(ox+gx, oy+gy, cr, cg, cb, alpha);
        }
        cx += g->AdvanceX * scale;
    }
}

void PathPreviewExport::blit_watermark(std::vector<unsigned char>& rgba,
                                        int w, int h) const
{
    if (!m_settings.watermark.enabled || !m_settings.watermark.text[0]) return;
    const float scale = m_settings.watermark.size / 13.f;
    const int margin  = 10;
    ImFont* font = ImGui::GetIO().Fonts->Fonts.Size > 0
                 ? ImGui::GetIO().Fonts->Fonts[0] : nullptr;
    float tw = font
        ? font->CalcTextSizeA(m_settings.watermark.size, FLT_MAX, 0.f,
                              m_settings.watermark.text).x
        : 80.f;
    float th = m_settings.watermark.size;
    int px, py;
    switch (m_settings.watermark.corner) {
    case 0: px=margin;         py=margin;          break;
    case 1: px=w-int(tw)-margin; py=margin;        break;
    case 2: px=margin;         py=h-int(th)-margin; break;
    default:px=w-int(tw)-margin; py=h-int(th)-margin; break;
    }
    const unsigned int a = unsigned(m_settings.watermark.opacity * 255);
    blit_text(rgba, w, h, m_settings.watermark.text, px, py, scale,
              (0xFF<<24)|(0xFF<<16)|(0xFF<<8)|a, m_settings.watermark.shadow);
}

void PathPreviewExport::blit_metadata(std::vector<unsigned char>& rgba,
                                       int w, int h) const
{
    if (!m_settings.metadata.any_enabled()) return;
    std::vector<std::string> lines;

    if (m_settings.metadata.printer_name)
        lines.push_back("Printer: " +
            wxGetApp().preset_bundle->printers.get_edited_preset().name);
    if (m_settings.metadata.model_name) {
        const auto* model = &wxGetApp().model();
        if (model && !model->objects.empty())
            lines.push_back("Model: " + model->objects.front()->name);
    }
    if (m_settings.metadata.date_time) {
        time_t t = time(nullptr); char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", localtime(&t));
        lines.push_back(std::string("Date: ") + buf);
    }
    if (m_settings.metadata.nozzle_size) {
        auto* nd = wxGetApp().preset_bundle->printers
                     .get_edited_preset().config
                     .option<ConfigOptionFloats>("nozzle_diameter");
        if (nd && !nd->values.empty()) {
            char buf[32];
            snprintf(buf,sizeof(buf),"Nozzle: %.2f mm",nd->values[0]);
            lines.push_back(buf);
        }
    }
    if (m_settings.metadata.filament_type) {
        auto& bundle = *wxGetApp().preset_bundle;
        if (!bundle.filament_presets.empty()) {
            auto* fil = bundle.filaments.find_preset(
                bundle.filament_presets.front());
            if (fil) {
                std::string ft; fil->config.get_filament_type(ft);
                lines.push_back("Filament: " + ft);
            }
        }
    }
    if (lines.empty()) return;

    const float scale   = m_settings.metadata.font_size / 13.f;
    const int   line_h  = int(m_settings.metadata.font_size * 1.35f);
    const int   margin  = 10;
    const int   total_h = int(lines.size()) * line_h;
    const unsigned int alpha = unsigned(m_settings.metadata.opacity * 255);
    const unsigned int col   = (0xFF<<24)|(0xFF<<16)|(0xFF<<8)|alpha;

    for (int i = 0; i < int(lines.size()); ++i) {
        int px, py;
        switch (m_settings.metadata.corner) {
        case 0: px=margin;        py=margin+i*line_h;           break;
        case 1: px=w-200-margin;  py=margin+i*line_h;           break;
        case 2: px=margin;        py=h-total_h-margin+i*line_h; break;
        default:px=w-200-margin;  py=h-total_h-margin+i*line_h; break;
        }
        blit_text(rgba, w, h, lines[i].c_str(), px, py, scale, col, true);
    }
}

} // namespace GUI
} // namespace Slic3r
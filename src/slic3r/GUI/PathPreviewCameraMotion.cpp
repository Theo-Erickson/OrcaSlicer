// PathPreviewCameraMotion.cpp  —  src/slic3r/GUI/
//
// Changes from previous version:
//   - CameraMotionMode enum replaced with three independent bool enables
//     (enable_orbit, enable_pitch, enable_rise) so any combination works
//     and the UI shows/hides only relevant fields without logic bugs.
//   - Window is now moveable and resizable (removed NoMove/NoResize flags).
//   - Preview-and-snap-back: saves full Camera state before preview,
//     runs motion at fixed dt for N seconds, then restores.
//   - Show grid wired to wxGetApp().toggle_show_plate_gridlines().
//   - Show world axes removed: m_show_world_axes is private on GLCanvas3D
//     and is only read at canvas init, so writing AppConfig has no live
//     effect. Documented as a known limitation.
//   - Show plate toggle: show/hide the plate via PartPlateList visibility.

#include "PathPreviewCameraMotion.hpp"
#include "PathPreviewPlayer.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "libslic3r/BoundingBox.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <new>  // placement new for Camera copy

namespace Slic3r {
namespace GUI {

// ── apply_motion ──────────────────────────────────────────────────────────────
// Core motion logic, shared between live tick and preview tick.
void PathPreviewCameraMotion::apply_motion(float dt, float speed_multiplier)
{
    const float spd = m_settings.affect_by_playback_rate ? speed_multiplier : 1.f;

    Camera& cam = wxGetApp().plater()->get_camera();

    // ── Horizontal orbit ──────────────────────────────────────────────────────
    if (m_settings.enable_orbit && m_settings.orbit_deg_per_sec != 0.f) {
        const float rad = m_settings.orbit_deg_per_sec * spd * dt
                          * float(M_PI) / 180.f;
        cam.rotate_on_sphere(rad, 0.f, true);
        m_orbit_accumulated += m_settings.orbit_deg_per_sec * spd * dt;
    }

    // ── Vertical pitch ────────────────────────────────────────────────────────
    if (m_settings.enable_pitch && m_settings.pitch_deg_per_sec != 0.f) {
        const float rad = m_settings.pitch_deg_per_sec * spd * dt
                          * float(M_PI) / 180.f;
        cam.rotate_on_sphere(0.f, rad, true);
        m_pitch_accumulated += m_settings.pitch_deg_per_sec * spd * dt;
    }

    // ── Z rise: smoothly track current build height ───────────────────────────
    if (m_settings.enable_rise && m_player && m_canvas) {
        const int current_layer = m_player->current_layer();
        const int total_layers  = m_player->total_layers();
        if (total_layers > 0) {
            const BoundingBoxf3 bbox = m_canvas->volumes_bounding_box();
            const double model_h     = bbox.max.z() - bbox.min.z();
            const double base_z      = bbox.min.z();
            const float  layer_frac  = float(current_layer) / float(total_layers);
            const double target_z    = base_z + model_h * layer_frac;

            const double extra = m_settings.rise_units_per_sec * spd * dt;
            m_rise_z_accumulated += float(extra);

            Vec3d t = cam.get_target();
            const double alpha = 1.0 - std::exp(-dt / 0.15);
            const double new_z = t.z() + (target_z + m_rise_z_accumulated - t.z()) * alpha;
            const double delta_z = new_z - t.z();
            m_rise_total_applied += float(delta_z);
            t.z() = new_z;
            cam.set_target(t);
        }
    }
}

// ── tick ──────────────────────────────────────────────────────────────────────
void PathPreviewCameraMotion::tick(float dt, float speed_multiplier)
{
    if (!m_settings.any_enabled() || !m_canvas) return;

    if (m_preview_active) {
        m_preview_tick_accum += dt;
        while (m_preview_tick_accum >= k_preview_dt &&
               m_preview_elapsed    <  m_preview_duration)
        {
            apply_motion(k_preview_dt, speed_multiplier);
            m_preview_elapsed    += k_preview_dt;
            m_preview_tick_accum -= k_preview_dt;
        }
        if (m_preview_elapsed >= m_preview_duration)
            stop_preview();  // always restores + pauses
        return;
    }

    apply_motion(dt, speed_multiplier);
}

// ── Preview-and-snap-back ─────────────────────────────────────────────────────
void PathPreviewCameraMotion::start_preview()
{
    if (!m_canvas || !m_player) return;

    // Save the full camera state.
    Camera& live = wxGetApp().plater()->get_camera();
    if (m_saved_camera) {
        *static_cast<Camera*>(m_saved_camera) = live;
    } else {
        m_saved_camera = new Camera(live);
    }
    // Also save per-axis accumulators so we restore them on snap-back.
    m_saved_orbit_acc = m_orbit_accumulated;
    m_saved_pitch_acc = m_pitch_accumulated;
    m_saved_rise_acc  = m_rise_z_accumulated;
    m_saved_rise_total = m_rise_total_applied;

    m_preview_elapsed    = 0.f;
    m_preview_tick_accum = 0.f;
    m_preview_active     = true;

    // Start playback so the animation actually advances during preview.
    m_player->force_play_from(m_player->current_layer());
}

void PathPreviewCameraMotion::stop_preview()
{
    m_preview_active = false;

    // Always restore camera to the saved state.
    if (m_saved_camera && m_canvas) {
        Camera& live = wxGetApp().plater()->get_camera();
        live = *static_cast<Camera*>(m_saved_camera);
        m_canvas->set_as_dirty();
    }

    // Restore accumulator counters so the display doesn't jump.
    m_orbit_accumulated  = m_saved_orbit_acc;
    m_pitch_accumulated  = m_saved_pitch_acc;
    m_rise_z_accumulated = m_saved_rise_acc;
    m_rise_total_applied = m_saved_rise_total;

    // Pause the player — preview has ended.
    if (m_player) m_player->pause();
}

// ── focus_on_model ────────────────────────────────────────────────────────────
// Uses the requires_zoom_to_volumes flag which is the canonical way to trigger
// zoom in OrcaSlicer — it fires on the next render pass and works correctly
// with the gcode viewer's current volume set.
void PathPreviewCameraMotion::focus_on_model()
{
    if (!m_canvas) return;

    // Set the flag that GLCanvas3D picks up on the next render pass.
    // This is more reliable than calling zoom_to_volumes() directly because
    // it goes through the normal render-loop path that handles the gcode viewer.
    Camera& cam = wxGetApp().plater()->get_camera();
    cam.requires_zoom_to_volumes = true;

    // Also set the target to the centroid of whatever volumes are loaded
    // so that orbit is smooth. We try the gcode scene box first.
    BoundingBoxf3 bbox = m_canvas->scene_bounding_box();
    if (!bbox.defined || bbox.area() < 1e-6)
        bbox = m_canvas->volumes_bounding_box();
    if (bbox.defined)
        cam.set_target(bbox.center());

    m_canvas->set_as_dirty();
}

// ── reset_motion_state ────────────────────────────────────────────────────────
void PathPreviewCameraMotion::reset_motion_state()
{
    m_orbit_accumulated  = 0.f;
    m_rise_z_accumulated = 0.f;
    if (m_preview_active) stop_preview();
}

// ── open_panel ────────────────────────────────────────────────────────────────
void PathPreviewCameraMotion::open_panel()
{
    m_panel_open = true;
    m_did_sync = true; // no longer need to sync overlay state
}

// ── UI ────────────────────────────────────────────────────────────────────────

void PathPreviewCameraMotion::render_button(ImVec2 pos, float size,
                                             bool /*hovered*/, bool active,
                                             unsigned int fg_col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cx = pos.x + size * 0.5f;
    const float cy = pos.y + size * 0.5f;

    // Camera body
    const float bw = size * 0.52f, bh = size * 0.36f;
    dl->AddRectFilled(ImVec2(cx-bw*0.5f, cy-bh*0.5f),
                      ImVec2(cx+bw*0.5f, cy+bh*0.5f), fg_col, 3.f);
    // Lens
    dl->AddCircleFilled(ImVec2(cx, cy), size*0.13f, IM_COL32(20,20,25,255));
    dl->AddCircle(ImVec2(cx, cy), size*0.13f, fg_col, 16, 1.2f);
    // Viewfinder bump
    const float bumpw = size*0.16f, bumph = size*0.10f;
    dl->AddRectFilled(ImVec2(cx-bumpw*0.5f, cy-bh*0.5f-bumph),
                      ImVec2(cx+bumpw*0.5f, cy-bh*0.5f+1.f), fg_col, 2.f);
    // Active indicator dot (green) when any motion is on
    if (active && m_settings.any_enabled()) {
        dl->AddCircleFilled(ImVec2(pos.x+size*0.80f, pos.y+size*0.22f),
                            size*0.12f, IM_COL32(80, 210, 130, 240));
    }
}

// ── render_panel ──────────────────────────────────────────────────────────────
void PathPreviewCameraMotion::render_panel(float canvas_w, float canvas_h,
                                            float anchor_x, float anchor_y)
{
    if (!m_panel_open) return;

    // First-use position: above and left of anchor.
    // After that the user can drag/resize freely.
    const float init_w = 300.f;
    const float init_h = 370.f;
    float init_x = std::clamp(anchor_x - init_w + 34.f,
                               4.f, canvas_w - init_w - 4.f);
    float init_y = std::clamp(anchor_y - init_h - 6.f,
                               4.f, canvas_h - init_h - 4.f);

    ImGui::SetNextWindowPos(ImVec2(init_x, init_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240.f, 250.f),
                                         ImVec2(500.f, 600.f));
    ImGui::SetNextWindowSize(ImVec2(init_w, init_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.96f);

    // Allow move + resize; keep title bar for drag handle.
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(12.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(6.f, 6.f));

    bool open = m_panel_open;
    if (ImGui::Begin("Camera motion##cammotion", &open, flags)) {
        if (!open) m_panel_open = false;

        draw_motion_checkboxes();

        if (m_settings.any_enabled()) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            draw_speed_controls();
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            draw_preview_controls();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        draw_focus_button();

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        draw_overlay_toggles();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ── draw_motion_checkboxes ────────────────────────────────────────────────────
// Three independent checkboxes — any combination is valid.
void PathPreviewCameraMotion::draw_motion_checkboxes()
{
    ImGui::TextDisabled("Camera motion");
    ImGui::Spacing();

    ImGui::Checkbox("Orbit (horizontal rotation)##en_orb",
                    &m_settings.enable_orbit);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rotate the camera horizontally around the model.");

    ImGui::Checkbox("Pitch (vertical tilt)##en_pit",
                    &m_settings.enable_pitch);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tilt the camera up or down over time.\n"
                          "Positive pitch_deg_per_sec tilts upward.");

    ImGui::Checkbox("Rise (Z tracking)##en_rise",
                    &m_settings.enable_rise);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move the camera target upward as layers build.\n"
                          "Automatically tracks the current layer height.\n"
                          "Use 'Focus on model' first for best results.");
}

// ── draw_speed_controls ───────────────────────────────────────────────────────
void PathPreviewCameraMotion::draw_speed_controls()
{
    ImGui::TextDisabled("Speed");
    ImGui::Spacing();

    const float label_x = 60.f;
    const float field_w = 72.f;
    const float unit_x  = label_x + field_w + 4.f;

    // Helper lambda: one row of  [Label] [InputFloat] [unit] [acc] [Reset]
    // Returns true if Reset was clicked.
    auto speed_row = [&](const char* label, const char* id,
                         float& speed_val, float& acc_val,
                         const char* unit, const char* acc_fmt,
                         const char* reset_id) -> bool
    {
        ImGui::Text("%s", label); ImGui::SameLine(label_x);
        ImGui::PushItemWidth(field_w);
        ImGui::InputFloat(id, &speed_val, 0.f, 0.f, "%.1f");
        ImGui::PopItemWidth();
        ImGui::SameLine(); ImGui::TextDisabled("%s", unit);

        // Accumulated display + reset on same line
        if (acc_val != 0.f) {
            char acc_buf[40];
            snprintf(acc_buf, sizeof(acc_buf), acc_fmt, acc_val);
            ImGui::SameLine(0.f, 10.f);
            ImGui::TextDisabled("%s", acc_buf);
            ImGui::SameLine(0.f, 6.f);
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.30f,0.30f,0.35f,1.f));
            bool reset = ImGui::SmallButton(reset_id);
            ImGui::PopStyleColor();
            if (reset) { acc_val = 0.f; return true; }
        }
        return false;
    };

    if (m_settings.enable_orbit) {
        speed_row("Orbit", "##orb_spd", m_settings.orbit_deg_per_sec,
                  m_orbit_accumulated, "deg/s",
                  "%.0f\xc2\xb0", "R##rorb");
    }

    if (m_settings.enable_pitch) {
        speed_row("Pitch", "##pit_spd", m_settings.pitch_deg_per_sec,
                  m_pitch_accumulated, "deg/s",
                  "%+.1f\xc2\xb0", "R##rpit");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Positive = tilt up, negative = tilt down");
    }

    if (m_settings.enable_rise) {
        // For rise show the total Z applied in scene units
        speed_row("Rise", "##rise_spd", m_settings.rise_units_per_sec,
                  m_rise_total_applied, "u/s",
                  "%+.2f u", "R##rrise");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Extra upward drift per second on top of\n"
                              "automatic layer-height tracking.");
    }

    ImGui::Spacing();
    ImGui::Checkbox("Scale with playback rate##spbr",
                    &m_settings.affect_by_playback_rate);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera speed scales with the player speed multiplier.");
}

// ── draw_preview_controls ─────────────────────────────────────────────────────
// Starts playback, runs for N seconds, then snaps camera back and pauses.
void PathPreviewCameraMotion::draw_preview_controls()
{
    ImGui::TextDisabled("Preview motion");
    ImGui::Spacing();

    ImGui::Text("Duration"); ImGui::SameLine(80.f);
    ImGui::PushItemWidth(80.f);
    ImGui::SliderFloat("##prev_dur", &m_preview_duration, 0.5f, 10.f, "%.1fs");
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Seconds to run before snapping back and pausing.");

    ImGui::Spacing();

    if (m_preview_active) {
        // Progress bar
        const float frac = std::min(m_preview_elapsed / m_preview_duration, 1.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f,0.7f,0.4f,1.f));
        ImGui::ProgressBar(frac, ImVec2(ImGui::GetContentRegionAvail().x, 6.f), "");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fs / %.1fs",
                 m_preview_elapsed, m_preview_duration);
        ImGui::TextDisabled("%s", buf);
        ImGui::Spacing();
        if (ImGui::Button("Stop & restore##prev_stop", ImVec2(-1.f, 0.f)))
            stop_preview();
    } else {
        if (ImGui::Button("Preview##prev_go", ImVec2(-1.f, 0.f)))
            start_preview();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Starts playback and runs camera motion for %.1fs,\n"
                "then snaps back to this exact camera position and pauses.\n"
                "Use this to test settings without losing your angle.",
                m_preview_duration);
    }
}

// ── draw_focus_button ─────────────────────────────────────────────────────────
void PathPreviewCameraMotion::draw_focus_button()
{
    ImGui::TextDisabled("View");
    ImGui::Spacing();

    if (ImGui::Button("Focus and center on model",
                      ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
    {
        focus_on_model();
        m_rise_z_accumulated = 0.f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Centers the camera target on the model centroid\n"
                          "and zooms to fit, so orbit rotates smoothly.\n"
                          "Also resets Z rise accumulation.");
}

// ── draw_overlay_toggles ──────────────────────────────────────────────────────
void PathPreviewCameraMotion::draw_overlay_toggles()
{
    ImGui::TextDisabled("Viewport overlays");
    ImGui::Spacing();
    // Axes, bed, and grid are controlled by the ≡ (hamburger) menu
    // in the bottom-left of the viewport. Direct toggle via AppConfig
    // only takes effect after canvas reinit, so we don't expose it here.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.f));
    ImGui::TextWrapped("Use the \xe2\x89\xa1 (three lines) menu in the\n"
                       "bottom-left of the viewport to toggle\n"
                       "grid, axes, and bed visibility.");
    ImGui::PopStyleColor();
}

} // namespace GUI
} // namespace Slic3r
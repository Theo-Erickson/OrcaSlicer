// PathPreviewPlayer.cpp
// NEW FILE — add to: src/slic3r/GUI/
//
// See PathPreviewPlayer.hpp for the full integration guide.
//
// Dependencies you already have in OrcaSlicer:
//   imgui/imgui.h             (Dear ImGui, bundled)
//   IconsFontAwesome5.h       (FA icon macros, bundled)
//   GCodeViewer.hpp           (set_toolpaths_move_range, layers API)

#include "PathPreviewPlayer.hpp"
#include "GCodeViewer.hpp"

// ImGui is included transitively through GLCanvas3D headers in OrcaSlicer,
// but include explicitly here for clarity.
#include "imgui/imgui.h"

// render_button():
#define ICON_FA_FILM "##open_player"     →  ">" "##open_player"

// draw_transport_controls():
#define ICON_FA_FAST_BACKWARD "##prev_layer"   →  "|<##prev_layer"
#define ICON_FA_STEP_BACKWARD "##step_back"    →  "<<##step_back"
#define ICON_FA_PAUSE         "##pause"                 →  "||"
#define ICON_FA_PLAY          "##play"                 →  ">"
#define ICON_FA_STEP_FORWARD "##step_fwd"      →  ">>##step_fwd"
#define ICON_FA_FAST_FORWARD "##next_layer"    →  ">|##next_layer"

#include <algorithm>  // std::clamp
#include <cstdio>     // snprintf

namespace Slic3r {
namespace GUI {

// ============================================================
//  init / reset
// ============================================================

void PathPreviewPlayer::init(GCodeViewer* viewer, int total_layers)
{
    m_viewer             = viewer;
    m_state.total_layers = total_layers;
    m_state.current_layer = std::max(1, 1);  // start at layer 1
    m_state.move_progress = 0.f;
    m_state.playing       = false;
    apply_to_viewer();
}

void PathPreviewPlayer::reset()
{
    m_state.reset();
    m_open  = false;
    m_viewer = nullptr;
}

// ============================================================
//  render_button
//  A small icon button pinned to the bottom-right of the viewport.
//  Call this from GLCanvas3D::_render_imgui_layers().
// ============================================================

void PathPreviewPlayer::render_button(float canvas_w, float canvas_h)
{
    if (m_viewer == nullptr || m_state.total_layers == 0)
        return;

    constexpr float BTN  = 52.f;   // was 34.f    
    constexpr float MARG = 10.f;
    constexpr float MARG_BOTTOM = 52.f;

    ImGui::SetNextWindowPos(
        ImVec2(canvas_w - BTN - MARG, canvas_h - BTN - MARG_BOTTOM),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(BTN, BTN));
    ImGui::SetNextWindowBgAlpha(0.80f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(3.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.f);

    // Push color BEFORE Begin()
    int color_push_count = 0;
    if (m_open) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
            ImVec4(0.05f, 0.45f, 0.15f, 0.85f));
        color_push_count = 1;
    }

    ImGui::Begin("##preview_player_btn", nullptr, flags);

    if (ImGui::Button(">##open_player", ImVec2(BTN - 6.f, BTN - 6.f)))
        toggle_open();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Path preview player");

    ImGui::End();

    // Pop AFTER End()
    if (color_push_count > 0)
        ImGui::PopStyleColor(color_push_count);

    ImGui::PopStyleVar(2);
}

// ============================================================
//  render_panel
//  The floating transport panel.  No-ops when m_open == false.
//  Call this immediately after render_button() in the same frame.
// ============================================================

void PathPreviewPlayer::render_panel(float canvas_w, float canvas_h)
{
    if (!m_open || m_viewer == nullptr)
        return;

    constexpr float PANEL_W = 320.f;
    constexpr float PANEL_H = 170.f;

    // Default position: bottom-right, just above the trigger button.
    ImGui::SetNextWindowPos(
        ImVec2(canvas_w - PANEL_W - 10.f, canvas_h - PANEL_H - 60.f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2(PANEL_W, PANEL_H),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(10.f, 8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(6.f, 5.f));

    // &m_open gives the panel its own [X] close button automatically.
    if (ImGui::Begin("Path Preview##player_panel", &m_open, flags)) {
        draw_progress_scrubber();
        ImGui::Spacing();
        draw_transport_controls();
        ImGui::Spacing();
        draw_mode_toggle();
        ImGui::SameLine(0.f, 12.f);
        draw_speed_combo();
        ImGui::SameLine(0.f, 12.f);

        // Layer counter label — right-aligned feel via SameLine padding
        char buf[32];
        snprintf(buf, sizeof(buf), "Layer %d / %d",
                 m_state.current_layer, m_state.total_layers);
        ImGui::TextDisabled("%s", buf);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

// ============================================================
//  tick
//  Called by GLCanvas3D's wxTimer (~16 ms interval).
//  dt: elapsed time in seconds since last call.
// ============================================================

void PathPreviewPlayer::tick(float dt)
{
    if (!m_state.playing || m_viewer == nullptr)
        return;

    const float advance = (dt * m_state.speed_multiplier) / k_seconds_per_layer;

    if (m_state.mode == PlaybackState::Mode::XYPath) 
    {
        // Animate move-by-move within the current layer.
        m_state.move_progress += advance;
        if (m_state.move_progress >= 1.f) 
        {
            m_state.move_progress = 0.f;   // reset to start of next layer
            if (m_state.current_layer < m_state.total_layers) 
            {
                m_state.current_layer++;
            } 
            else 
            {
                m_state.playing = false;
            }
        }
    } else {
        // Layers mode: each layer appears fully formed, one at a time.
        m_state.move_progress += advance;
        if (m_state.move_progress >= 1.f) {
            m_state.move_progress = 0.f;
            if (m_state.current_layer < m_state.total_layers) {
                m_state.current_layer++;
            } else {
                m_state.playing = false;
            }
        }
    }

    apply_to_viewer();
}

// ============================================================
//  Private — UI helpers
// ============================================================

void PathPreviewPlayer::draw_progress_scrubber()
{
    // Overall progress across all layers, expressed as 0–1.
    const float total = static_cast<float>(m_state.total_layers);
    float global = (static_cast<float>(m_state.current_layer - 1) +
                    m_state.move_progress) / total;
    global = std::clamp(global, 0.f, 1.f);

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::SliderFloat("##prog", &global, 0.f, 1.f, "")) {
        // Map global 0–1 back to layer + intra-layer progress.
        const float scaled  = global * total;
        int   new_layer     = static_cast<int>(scaled) + 1;
        float new_progress  = scaled - std::floor(scaled);
        m_state.current_layer  = std::clamp(new_layer, 1, m_state.total_layers);
        m_state.move_progress  = new_progress;
        apply_to_viewer();
    }
    ImGui::PopItemWidth();
}

void PathPreviewPlayer::draw_transport_controls()
{
    if (ImGui::Button("|<##prev_layer"))
        step_layer(-1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous layer");

    ImGui::SameLine();

    if (ImGui::Button("<<##step_back"))
        step_moves(-k_step_delta);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step back");

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button,
        m_state.playing
            ? ImVec4(0.70f, 0.20f, 0.20f, 1.f)
            : ImVec4(0.10f, 0.55f, 0.25f, 1.f));

    const char* play_icon = m_state.playing ? "||" : ">";
    if (ImGui::Button(play_icon, ImVec2(32.f, 24.f)))
        toggle_play();

    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m_state.playing ? "Pause" : "Play");

    ImGui::SameLine();

    if (ImGui::Button(">>##step_fwd"))
        step_moves(k_step_delta);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step forward");

    ImGui::SameLine();

    if (ImGui::Button(">|##next_layer"))
        step_layer(+1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next layer");
}

void PathPreviewPlayer::draw_mode_toggle()
{
    // Simple inline radio buttons for the two animation modes.
    bool xy = (m_state.mode == PlaybackState::Mode::XYPath);

    if (ImGui::RadioButton("XY path", xy)) {
        m_state.mode = PlaybackState::Mode::XYPath;
        m_state.move_progress = 0.f;
        apply_to_viewer();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Layers", !xy)) {
        m_state.mode = PlaybackState::Mode::Layers;
        m_state.move_progress = 0.f;
        apply_to_viewer();
    }
}

void PathPreviewPlayer::draw_speed_combo()
{
    static const char* labels[]  = { "0.5x", "1x", "2x", "4x", "8x" };
    static const float values[]  = { 0.5f,   1.f,  2.f,  4.f,  8.f  };
    constexpr int      N         = 5;

    // Find current index
    int cur = 1;
    for (int i = 0; i < N; i++)
        if (values[i] == m_state.speed_multiplier) { cur = i; break; }

    ImGui::PushItemWidth(60.f);
    if (ImGui::BeginCombo("##speed", labels[cur], ImGuiComboFlags_HeightSmall)) {
        for (int i = 0; i < N; i++) {
            bool selected = (i == cur);
            if (ImGui::Selectable(labels[i], selected))
                m_state.speed_multiplier = values[i];
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed");
}

// ============================================================
//  Private — Playback logic
// ============================================================

void PathPreviewPlayer::toggle_play()
{
    if (m_viewer == nullptr) return;

    // If we're at the very end, restart from the beginning.
    if (!m_state.playing &&
        m_state.current_layer >= m_state.total_layers &&
        m_state.move_progress >= 1.f) {
        m_state.current_layer = 1;
        m_state.move_progress = 0.f;
        apply_to_viewer();
    }
    m_state.playing = !m_state.playing;
}

void PathPreviewPlayer::toggle_open()
{
    m_open = !m_open;
    // If closing while playing, pause — avoids invisible background animation.
    if (!m_open)
        m_state.playing = false;
}

void PathPreviewPlayer::step_layer(int delta)
{
    if (m_viewer == nullptr) return;
    m_state.current_layer = std::clamp(
        m_state.current_layer + delta, 1, m_state.total_layers);
    m_state.move_progress = 0.f;
    apply_to_viewer();
}

void PathPreviewPlayer::step_moves(float delta_pct)
{
    if (m_viewer == nullptr) return;
    m_state.move_progress = std::clamp(
        m_state.move_progress + delta_pct, 0.f, 1.f);
    apply_to_viewer();
}

void PathPreviewPlayer::seek_progress(float pct)
{
    m_state.move_progress = std::clamp(pct, 0.f, 1.f);
    apply_to_viewer();
}

void PathPreviewPlayer::seek_layer(int layer)
{
    m_state.current_layer = std::clamp(layer, 1, m_state.total_layers);
    m_state.move_progress = 0.f;
    apply_to_viewer();
}

// ============================================================
//  apply_to_viewer
//
//  Drives GCodeViewer's existing sequential-view range APIs,
//  which are the same ones the layer-range slider uses.
//
//  GCodeViewer::set_toolpaths_move_range(unsigned min, unsigned max)
//    sets the absolute move-index window that gets rendered.
//
//  GCodeViewer::get_layers_z_range() / set_layers_z_range()
//    control which layers are visible by Z.
//
//  For XYPath mode:
//    - Set the Z range to [current_layer, current_layer] so only
//      the active layer is rendered.
//    - Compute move indices for that layer and pass [0, progress*count]
//      to set_toolpaths_move_range().
//
//  For Layers mode:
//    - Set the Z range to [1, current_layer] so all layers below
//      are fully visible.
//    - No partial move range needed (each layer shown complete).
// ============================================================

void PathPreviewPlayer::apply_to_viewer()
{
    if (m_viewer == nullptr) return;

    IMSlider* layers_slider = m_viewer->get_layers_slider();
    IMSlider* moves_slider  = m_viewer->get_moves_slider();

    if (!layers_slider || !moves_slider) return;

    // layer_idx is 1-based to match the slider's value range
    const int layer_idx = std::clamp(
        m_state.current_layer, 1, m_state.total_layers);

    if (m_state.mode == PlaybackState::Mode::XYPath) {

        // Pin the layer slider to exactly the current layer
        // (do NOT set LowerValue — leave it where the user had it)
        if (layers_slider->GetHigherValue() != layer_idx) {
            layers_slider->SetHigherValue(layer_idx);
            layers_slider->set_as_dirty(true);

            // After changing the layer, reset moves to the beginning
            // so the next tick animates from the start of this layer
            moves_slider->SetHigherValue(moves_slider->GetMinValue());
            moves_slider->set_as_dirty(true);
        }

        // Now animate moves within the current layer.
        // The moves slider min/max now reflects only the current layer
        // because set_as_dirty caused it to recompute.
        const int min_move = moves_slider->GetMinValue();
        const int max_move = moves_slider->GetMaxValue();
        const int target   = min_move + static_cast<int>(
            m_state.move_progress * static_cast<float>(max_move - min_move));

        moves_slider->SetHigherValue(std::clamp(target, min_move, max_move));
        moves_slider->set_as_dirty(true);

    } else {
        // Layers mode: advance one complete layer at a time.
        // Show all moves in the current layer range fully.
        if (layers_slider->GetHigherValue() != layer_idx) {
            layers_slider->SetHigherValue(layer_idx);
            layers_slider->set_as_dirty(true);
        }
        // Show all moves for the visible layers
        moves_slider->SetHigherValue(moves_slider->GetMaxValue());
        moves_slider->set_as_dirty(true);
    }
}

} // namespace GUI
} // namespace Slic3r
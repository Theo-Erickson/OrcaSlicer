// PathPreviewPlayer.cpp
// 
//
// See PathPreviewPlayer.hpp for the full integration guide.
//
// Dependencies you already have in OrcaSlicer:
//   imgui/imgui.h             (Dear ImGui, bundled)
//   IconsFontAwesome5.h       (FA icon macros, bundled)
//   GCodeViewer.hpp           (set_toolpaths_move_range, layers API)

#include "PathPreviewPlayer.hpp"
#include "GCodeViewer.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

// ImGui is included transitively through GLCanvas3D headers in OrcaSlicer,
// but include explicitly here for clarity.
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace Slic3r {
namespace GUI {

// ── constexpr out-of-class definitions ──────────────────────────────────────
constexpr float        PathPreviewPlayer::k_spl_xy;
constexpr float        PathPreviewPlayer::k_spl_layers;
constexpr float        PathPreviewPlayer::k_step_delta;
constexpr int          PathPreviewPlayer::k_num_speeds;
constexpr float        PathPreviewPlayer::k_speeds[];
constexpr const char*  PathPreviewPlayer::k_speed_labels[];

// ── Palette helpers ─────────────────────────────────────────────────────────
// Returns IM_COL32 values for each theme's accent color family.
//   shade: 0 = dark fill, 1 = mid accent, 2 = light/highlight
static unsigned int theme_color(IconTheme t, int shade)
{
    // Hex: purple family  (#26215c / #7f77dd / #afa9ec)
    // Circ: teal family   (#085041 / #1d9e75 / #5dcaa5)
    // Diam: amber family  (#412402 / #ba7517 / #ef9f27)
    static const unsigned int palette[3][3] = {
        { IM_COL32(38,  33, 92, 255), IM_COL32(127,119,221,255), IM_COL32(175,169,236,255) },
        { IM_COL32( 8,  80, 65, 255), IM_COL32( 29,158,117,255), IM_COL32( 93,202,165,255) },
        { IM_COL32(65,  36,  2, 255), IM_COL32(186,117, 23,255), IM_COL32(239,159, 39,255) },
    };
    return palette[static_cast<int>(t)][shade];
}

// ── init / reset ─────────────────────────────────────────────────────────────
void PathPreviewPlayer::init(GCodeViewer* viewer, int total_layers)
{
    m_viewer              = viewer;
    m_state.total_layers  = total_layers;
    m_state.current_layer = 1;
    m_state.move_progress = 0.f;
    m_state.layer_timer   = 0.f;
    m_state.playing       = false;
    m_state.speed_multiplier = k_speeds[m_speed_idx];
    m_anim_t              = 0.f;

    // Wire the export and camera motion systems.
    m_export.set_player(this);
    m_camera.set_player(this);
}

void PathPreviewPlayer::reset()
{
    m_state.reset();
    m_open  = false;
    m_viewer = nullptr;
    m_anim_t = 0.f;
    m_camera.reset_motion_state();
}

// ── Config ───────────────────────────────────────────────────────────────────
void PathPreviewPlayer::load_config(AppConfig* cfg)
{
    if (!cfg) return;

    const std::string anchor_s = cfg->get("path_preview_player", "anchor");
    if      (anchor_s == "bottom_left") m_anchor = ButtonAnchor::BottomLeft;
    else if (anchor_s == "top_right")   m_anchor = ButtonAnchor::TopRight;
    else if (anchor_s == "top_left")    m_anchor = ButtonAnchor::TopLeft;
    else                                m_anchor = ButtonAnchor::BottomRight;

    const std::string theme_s = cfg->get("path_preview_player", "theme");
    if      (theme_s == "circular") m_theme = IconTheme::Circular;
    else if (theme_s == "diamond")  m_theme = IconTheme::Diamond;
    else                            m_theme = IconTheme::Hexagonal;

    m_preview_theme = m_theme;
}

void PathPreviewPlayer::save_config(AppConfig* cfg) const
{
    if (!cfg) return;

    const char* anchor_s = "bottom_right";
    switch (m_anchor) {
    case ButtonAnchor::BottomLeft: anchor_s = "bottom_left"; break;
    case ButtonAnchor::TopRight:   anchor_s = "top_right";   break;
    case ButtonAnchor::TopLeft:    anchor_s = "top_left";    break;
    default: break;
    }
    cfg->set("path_preview_player", "anchor", anchor_s);

    const char* theme_s = "hexagonal";
    switch (m_theme) {
    case IconTheme::Circular: theme_s = "circular"; break;
    case IconTheme::Diamond:  theme_s = "diamond";  break;
    default: break;
    }
    cfg->set("path_preview_player", "theme", theme_s);
}

// ── Icon color ────────────────────────────────────────────────────────────────
unsigned int PathPreviewPlayer::icon_color(bool hovered, bool active,
                                           IconTheme theme, bool is_play) const
{
    if (active || (is_play && m_state.playing))
        return IM_COL32(255, 255, 255, 255);
    if (hovered)
        return theme_color(theme, 2);     // light accent
    if (is_play)
        return theme_color(theme, 1);     // mid accent always on play btn
    return IM_COL32(160, 160, 160, 220); // neutral resting
}

// ── Background drawing ────────────────────────────────────────────────────────
// Draws the shaped button shell for the given theme.
// anim_t is the running time in seconds — used for animated decorations.
void PathPreviewPlayer::draw_btn_background(ImVec2 pos, float s,
                                             bool hovered, bool active,
                                             bool playing,
                                             IconTheme theme,
                                             float anim_t) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cx = pos.x + s * 0.5f;
    const float cy = pos.y + s * 0.5f;

    unsigned int bg, border;
    if (active || playing) {
        bg     = theme_color(theme, 0);  // dark fill
        border = theme_color(theme, 1);  // mid accent border
    } else if (hovered) {
        bg     = IM_COL32(42, 42, 52, 220);
        border = theme_color(theme, 1);
    } else {
        bg     = IM_COL32(28, 28, 34, 200);
        border = IM_COL32(60, 60, 70, 200);
    }

    if (theme == IconTheme::Hexagonal) {
        // Six-point polygon
        const float r = s * 0.48f;
        ImVec2 pts[6];
        for (int i = 0; i < 6; ++i) {
            float a = (float)i / 6.f * 6.2832f - 1.5708f; // start at top
            pts[i] = ImVec2(cx + r * cosf(a), cy + r * sinf(a));
        }
        dl->AddConvexPolyFilled(pts, 6, bg);

        // Animated border pulse when playing
        if (playing) {
            float pulse = 0.5f + 0.5f * sinf(anim_t * 3.14f);
            unsigned int bc = (border & 0x00FFFFFF) |
                              (((unsigned int)(200 + 55 * pulse)) << 24);
            dl->AddPolyline(pts, 6, bc, ImDrawFlags_Closed, 1.5f);
            // Second outer ring, fades in/out
            float r2 = r + 3.f;
            ImVec2 pts2[6];
            for (int i = 0; i < 6; ++i) {
                float a = (float)i / 6.f * 6.2832f - 1.5708f;
                pts2[i] = ImVec2(cx + r2 * cosf(a), cy + r2 * sinf(a));
            }
            unsigned int rc = (border & 0x00FFFFFF) |
                              (((unsigned int)(80 * pulse)) << 24);
            dl->AddPolyline(pts2, 6, rc, ImDrawFlags_Closed, 1.0f);
        } else {
            dl->AddPolyline(pts, 6, border, ImDrawFlags_Closed, 1.0f);
        }

    } else if (theme == IconTheme::Circular) {
        const float r = s * 0.46f;
        dl->AddCircleFilled(ImVec2(cx, cy), r, bg);

        if (playing) {
            // Expanding ring glow
            float pulse = 0.5f + 0.5f * sinf(anim_t * 2.5f);
            float r2    = r + 3.f + 2.f * pulse;
            unsigned int rc = (border & 0x00FFFFFF) |
                              (((unsigned int)(120 * (1.f - pulse))) << 24);
            dl->AddCircle(ImVec2(cx, cy), r2, rc, 32, 1.5f);
            dl->AddCircle(ImVec2(cx, cy), r, border, 32, 1.5f);
        } else {
            dl->AddCircle(ImVec2(cx, cy), r, border, 32, 1.0f);
        }

    } else {
        // Diamond: rotated square drawn as a 4-point polygon
        const float r = s * 0.44f;
        ImVec2 pts[4] = {
            ImVec2(cx,     cy - r),  // top
            ImVec2(cx + r, cy    ),  // right
            ImVec2(cx,     cy + r),  // bottom
            ImVec2(cx - r, cy    ),  // left
        };
        dl->AddConvexPolyFilled(pts, 4, bg);

        if (playing) {
            // Traveling dash around the diamond perimeter using a clip trick:
            // draw segments with alternating alpha driven by anim_t.
            float phase = fmodf(anim_t * 0.8f, 1.0f); // 0..1 cycling
            for (int seg = 0; seg < 4; ++seg) {
                float seg_phase = fmodf(phase + seg * 0.25f, 1.0f);
                float alpha = (seg_phase < 0.5f)
                    ? seg_phase * 2.f
                    : (1.f - seg_phase) * 2.f;
                alpha = 0.2f + 0.8f * alpha;
                unsigned int sc = (border & 0x00FFFFFF) |
                                  (((unsigned int)(alpha * 255)) << 24);
                ImVec2 a = pts[seg];
                ImVec2 b = pts[(seg + 1) % 4];
                dl->AddLine(a, b, sc, 1.5f);
            }
        } else {
            dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, 1.0f);
        }
    }
}

// ── Icon shapes ───────────────────────────────────────────────────────────────
void PathPreviewPlayer::draw_icon_prev_layer(ImVec2 p, float s, unsigned int col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float m  = s * 0.22f;
    const float bw = s * 0.10f;
    // Vertical bar
    dl->AddRectFilled(ImVec2(p.x+m, p.y+m), ImVec2(p.x+m+bw, p.y+s-m), col);
    // Triangle pointing left
    float tx = p.x + m + bw + s*0.04f;
    dl->AddTriangleFilled(
        ImVec2(tx + s*0.38f, p.y + m),
        ImVec2(tx + s*0.38f, p.y + s - m),
        ImVec2(tx,           p.y + s*0.5f), col);
}

void PathPreviewPlayer::draw_icon_step_back(ImVec2 p, float s, unsigned int col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float m  = s * 0.18f;
    const float hw = (s - 2*m) * 0.48f;
    dl->AddTriangleFilled(
        ImVec2(p.x+m+hw, p.y+m), ImVec2(p.x+m+hw, p.y+s-m), ImVec2(p.x+m, p.y+s*0.5f), col);
    dl->AddTriangleFilled(
        ImVec2(p.x+m+hw*2, p.y+m), ImVec2(p.x+m+hw*2, p.y+s-m), ImVec2(p.x+m+hw, p.y+s*0.5f), col);
}

void PathPreviewPlayer::draw_icon_play(ImVec2 p, float s, unsigned int col) const
{
    const float m = s * 0.20f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(p.x+m,     p.y+m),
        ImVec2(p.x+m,     p.y+s-m),
        ImVec2(p.x+s-m,   p.y+s*0.5f), col);
}

void PathPreviewPlayer::draw_icon_pause(ImVec2 p, float s, unsigned int col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float m  = s * 0.20f;
    const float bw = s * 0.18f;
    const float gap = s * 0.10f;
    const float lx = p.x + s*0.5f - bw - gap*0.5f;
    dl->AddRectFilled(ImVec2(lx,        p.y+m), ImVec2(lx+bw,          p.y+s-m), col);
    dl->AddRectFilled(ImVec2(lx+bw+gap, p.y+m), ImVec2(lx+bw*2.f+gap,  p.y+s-m), col);
}

void PathPreviewPlayer::draw_icon_step_fwd(ImVec2 p, float s, unsigned int col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float m  = s * 0.18f;
    const float hw = (s - 2*m) * 0.48f;
    dl->AddTriangleFilled(
        ImVec2(p.x+m,     p.y+m), ImVec2(p.x+m,     p.y+s-m), ImVec2(p.x+m+hw,   p.y+s*0.5f), col);
    dl->AddTriangleFilled(
        ImVec2(p.x+m+hw,  p.y+m), ImVec2(p.x+m+hw,  p.y+s-m), ImVec2(p.x+m+hw*2, p.y+s*0.5f), col);
}

void PathPreviewPlayer::draw_icon_next_layer(ImVec2 p, float s, unsigned int col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float m  = s * 0.22f;
    const float bw = s * 0.10f;
    float tx = p.x + m;
    dl->AddTriangleFilled(
        ImVec2(tx,           p.y+m),
        ImVec2(tx,           p.y+s-m),
        ImVec2(tx+s*0.38f,   p.y+s*0.5f), col);
    float bx = tx + s*0.38f + s*0.04f;
    dl->AddRectFilled(ImVec2(bx, p.y+m), ImVec2(bx+bw, p.y+s-m), col);
}

// ── transport_btn ─────────────────────────────────────────────────────────────
// One full button: invisible hit area → background → icon.
// Uses m_preview_theme so hovering a theme in the menu shows it immediately.
bool PathPreviewPlayer::transport_btn(
    const char* id,
    void (PathPreviewPlayer::*icon_fn)(ImVec2, float, unsigned int) const,
    float btn_size,
    bool is_play_btn,
    bool force_pause_icon)
{
    const ImVec2 sz(btn_size, btn_size);
    ImGui::InvisibleButton(id, sz);
    const bool hov     = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const ImVec2 pos   = ImGui::GetItemRectMin();
    const bool   act   = is_play_btn && m_state.playing;

    IconTheme effective = m_in_theme_menu ? m_preview_theme : m_theme;

    draw_btn_background(pos, btn_size, hov, clicked, act, effective, m_anim_t);

    unsigned int col = icon_color(hov, clicked, effective, is_play_btn);

    if (is_play_btn && (act || force_pause_icon))
        draw_icon_pause(pos, btn_size, col);
    else
        (this->*icon_fn)(pos, btn_size, col);

    return clicked;
}

// ── draw_theme_preview ────────────────────────────────────────────────────────
// Draws a static 3-button preview strip (prev / play / next) in a given theme.
// Called inside the context menu while hovering a theme row.
void PathPreviewPlayer::draw_theme_preview(IconTheme theme, float btn_size) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cursor  = ImGui::GetCursorScreenPos();
    const float gap = 4.f;

    for (int i = 0; i < 3; ++i) {
        ImVec2 p(cursor.x + i * (btn_size + gap), cursor.y);
        bool is_play = (i == 1);
        bool playing  = is_play; // show pause icon on center btn

        draw_btn_background(p, btn_size,
                            /*hovered=*/false, /*active=*/false,
                            playing, theme, m_anim_t);

        unsigned int col = is_play
            ? theme_color(theme, 1)
            : IM_COL32(150, 150, 150, 220);

        if (i == 0)      draw_icon_prev_layer(p, btn_size, col);
        else if (i == 1) draw_icon_pause(p, btn_size, col);
        else             draw_icon_next_layer(p, btn_size, col);
    }

    // Advance cursor past the drawn buttons so ImGui layout continues correctly
    ImGui::Dummy(ImVec2(3 * btn_size + 2 * gap, btn_size));
}

// ── compute_anchor_pos ────────────────────────────────────────────────────────
ImVec2 PathPreviewPlayer::compute_anchor_pos(float cw, float ch,
                                              float bw, float bh) const
{
    const float MARG   = 12.f;
    const float M_BOT  = 58.f;
    switch (m_anchor) {
    case ButtonAnchor::BottomLeft: return ImVec2(MARG,      ch - bh - M_BOT);
    case ButtonAnchor::TopRight:   return ImVec2(cw-bw-MARG, MARG);
    case ButtonAnchor::TopLeft:    return ImVec2(MARG,       MARG);
    default:                       return ImVec2(cw-bw-MARG, ch-bh-M_BOT);
    }
}

// ── show_context_menu ─────────────────────────────────────────────────────────
void PathPreviewPlayer::show_context_menu()
{
    if (!ImGui::BeginPopupContextItem("##ppp_ctx"))
    {
        // Menu just closed — commit preview theme back to actual
        if (m_in_theme_menu) {
            m_in_theme_menu = false;
            m_preview_theme = m_theme;
        }
        return;
    }

    m_in_theme_menu = true;

    // ── Position ────────────────────────────────────────────────────────────
    ImGui::TextDisabled("Position");
    ImGui::Separator();

    struct APos { const char* label; ButtonAnchor val; };
    static const APos apos[] = {
        { "Bottom-right (default)", ButtonAnchor::BottomRight },
        { "Bottom-left",            ButtonAnchor::BottomLeft  },
        { "Top-right",              ButtonAnchor::TopRight    },
        { "Top-left",               ButtonAnchor::TopLeft     },
    };
    for (const auto& a : apos) {
        if (ImGui::MenuItem(a.label, nullptr, m_anchor == a.val)) {
            m_anchor = a.val;
            save_config(wxGetApp().app_config);
        }
    }

    ImGui::Spacing();

    // ── Icon theme ───────────────────────────────────────────────────────────
    ImGui::TextDisabled("Icon theme");
    ImGui::Separator();

    struct ThemeOpt { const char* label; IconTheme val; const char* desc; };
    static const ThemeOpt themes[] = {
        { "Hexagonal", IconTheme::Hexagonal, "Purple, pulse border" },
        { "Circular",  IconTheme::Circular,  "Teal, ring glow"      },
        { "Diamond",   IconTheme::Diamond,   "Amber, dash travel"   },
    };

    const float preview_btn  = 22.f;
    const float preview_w    = 3 * preview_btn + 2 * 4.f; // 3 btns + 2 gaps
    const float row_h        = preview_btn + 6.f;

    for (const auto& th : themes) {
        bool selected = (m_theme == th.val);

        // Custom selectable sized to hold the radio + label + right-side preview
        ImVec2 selectable_sz(ImGui::GetContentRegionAvail().x, row_h);
        ImVec2 row_min = ImGui::GetCursorScreenPos();

        bool hovered_row = false;
        ImGui::PushID(static_cast<int>(th.val));

        if (ImGui::Selectable("##theme_sel", selected,
                              ImGuiSelectableFlags_None, selectable_sz))
        {
            m_theme         = th.val;
            m_preview_theme = th.val;
            save_config(wxGetApp().app_config);
        }

        hovered_row = ImGui::IsItemHovered();

        // While hovered, switch the live preview theme
        if (hovered_row)
            m_preview_theme = th.val;

        // Draw content on top of the selectable
        ImVec2 p = row_min;
        p.x += 4.f;
        p.y += 3.f;

        // Radio dot
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float dot_r = 5.f;
        ImVec2 dot_c(p.x + dot_r, p.y + preview_btn * 0.5f);
        if (selected) {
            dl->AddCircleFilled(dot_c, dot_r,
                theme_color(th.val, 1));
            dl->AddCircle(dot_c, dot_r,
                theme_color(th.val, 1), 16, 1.5f);
        } else {
            dl->AddCircle(dot_c, dot_r,
                IM_COL32(100,100,100,200), 16, 1.f);
        }

        // Label + description
        ImVec2 text_p(dot_c.x + dot_r + 6.f, row_min.y + 2.f);
        dl->AddText(text_p, IM_COL32(220,220,220,255), th.label);
        ImVec2 desc_p(text_p.x, text_p.y + 14.f);
        dl->AddText(ImGui::GetFont(), 10.f, desc_p,
                    IM_COL32(140,140,140,200), th.desc);

        // Mini preview strip on the right
        float px = row_min.x + selectable_sz.x - preview_w - 8.f;
        float py = row_min.y + (row_h - preview_btn) * 0.5f;
        ImVec2 prev_cursor(px, py);
        ImGui::SetCursorScreenPos(prev_cursor);

        // Temporarily set cursor so draw_theme_preview uses correct coords
        draw_theme_preview(th.val, preview_btn);

        ImGui::PopID();
    }

    // Reset preview theme if nothing is hovered
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        m_preview_theme = m_theme;
    }

    ImGui::EndPopup();

    if (!ImGui::IsPopupOpen("##ppp_ctx")) {
        m_in_theme_menu = false;
        m_preview_theme = m_theme;
    }
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

    const float BTN = 44.f;
    ImVec2 pos = compute_anchor_pos(canvas_w, canvas_h, BTN, BTN);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(BTN, BTN));
    ImGui::SetNextWindowBgAlpha(0.0f);  // fully transparent — theme draws its own bg

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::Begin("##ppp_btn", nullptr, flags);

    IconTheme effective = m_in_theme_menu ? m_preview_theme : m_theme;

    // InvisibleButton for the whole window
    ImGui::InvisibleButton("##btn_hit", ImVec2(BTN, BTN));
    const bool hov     = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    if (clicked)
        toggle_open();

    ImVec2 btn_pos = ImGui::GetItemRectMin();
    draw_btn_background(btn_pos, BTN,
                        hov, false, m_state.playing,
                        effective, m_anim_t);

    unsigned int col = icon_color(hov, false, effective, true);
    if (m_state.playing)
        draw_icon_pause(btn_pos, BTN, col);
    else
        draw_icon_play(btn_pos, BTN, col);

    if (hov)
        ImGui::SetTooltip("Path preview player\nRight-click to configure");

    // Right-click context menu
    ImGui::OpenPopupOnItemClick("##ppp_ctx", ImGuiPopupFlags_MouseButtonRight);
    show_context_menu();

    ImGui::End();
    ImGui::PopStyleVar();
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

    // Panel width: at least 580 px, at most 90% of canvas, centered.
    // Panel height: base + extra rows when timeline details are on.
    const float PANEL_H = m_show_timeline_details ? 188.f : 116.f;
    const float PANEL_W = std::min(std::max(canvas_w * 0.60f, 580.f),
                                   canvas_w * 0.90f);

    // Default position: bottom-right, just above the trigger button.
    ImGui::SetNextWindowPos(
        ImVec2(canvas_w * 0.5f - PANEL_W * 0.5f, canvas_h - PANEL_H - 62.f),
        ImGuiCond_Always);   // Always so it repositions when height changes
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(500.f, PANEL_H), ImVec2(canvas_w * 0.90f, PANEL_H));
    ImGui::SetNextWindowSize(ImVec2(PANEL_W, PANEL_H), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(12.f, 8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(6.f,  4.f));

    if (ImGui::Begin("Path Preview##ppp_panel", &m_open, flags)) {
        // ── Row 1: full-width progress scrubber ───────────────────────────────
        draw_progress_scrubber();

        ImGui::Spacing();

        // ── Row 2: [XY path / Layers] [speed]  ···  [transport]  ··· [export] ──
        // Strategy: draw all three groups manually using absolute X positions
        // so they are reliably flush-left, centered, and flush-right regardless
        // of window width.

        const float BTN      = 28.f;
        const float PLAY_BTN = BTN + 10.f;
        const float EXP_BTN  = BTN + 6.f;
        const float GAP      = 6.f;
        const float ROW_H    = PLAY_BTN; // tallest element sets row height

        // Left group width: mode icons (or text) + speed combo
        // Icon mode: 2 × 20px buttons + 4px gap + 4px margin = ~48px
        // Text mode: RadioButton text widths
        const float left_w = m_mode_icons_enabled
            ? (20.f + 4.f + 20.f + 14.f + 92.f)
            : (ImGui::CalcTextSize("XY path").x + 26.f + 4.f +
               ImGui::CalcTextSize("Layers").x  + 26.f + 14.f + 92.f);

        // Transport group total width
        const float transport_w = BTN*4 + PLAY_BTN + GAP*4;

        const float content_x   = ImGui::GetCursorPosX();
        const float content_w   = ImGui::GetContentRegionAvail().x;
        const float row_y       = ImGui::GetCursorPosY();

        // Center X for transport group
        const float transport_x = content_x + (content_w - transport_w) * 0.5f;

        // (export_x is computed inside the right group block below)

        // ── Left group: XY path | Layers | speed ──────────────────────────────
        ImGui::SetCursorPos(ImVec2(content_x,
                                   row_y + (ROW_H - ImGui::GetFrameHeight()) * 0.5f));
        draw_mode_toggle();
        ImGui::SameLine(0.f, 14.f);
        // Nudge speed combo to vertical center of row
        ImGui::SetCursorPosY(row_y + (ROW_H - ImGui::GetFrameHeight()) * 0.5f);
        draw_speed_combo();

        // ── Center group: transport buttons ───────────────────────────────────
        ImGui::SetCursorPos(ImVec2(transport_x, row_y));

        if (transport_btn("|<##pl", &PathPreviewPlayer::draw_icon_prev_layer,
                          BTN, false))
            step_layer(-1);
        ImGui::SameLine(0.f, GAP);

        if (transport_btn("<<##sb", &PathPreviewPlayer::draw_icon_step_back,
                          BTN, false))
            step_moves(-k_step_delta);
        ImGui::SameLine(0.f, GAP);

        if (transport_btn(">||##pp", &PathPreviewPlayer::draw_icon_play,
                          PLAY_BTN, true))
            toggle_play();
        ImGui::SameLine(0.f, GAP);

        if (transport_btn(">>##sf", &PathPreviewPlayer::draw_icon_step_fwd,
                          BTN, false))
            step_moves(k_step_delta);
        ImGui::SameLine(0.f, GAP);

        if (transport_btn(">|##nl", &PathPreviewPlayer::draw_icon_next_layer,
                          BTN, false))
            step_layer(+1);

        // ── Right group: [Details checkbox] [camera] [export] ─────────────────
        const float CAM_BTN  = EXP_BTN;
        // Add a small "Details" toggle checkbox between left group and right buttons.
        // Positioned just right of where the transport group ends.
        const float DET_W    = 14.f;  // small checkbox
        // Right button positions
        const float export_x = content_x + content_w - EXP_BTN;
        const float cam_x    = export_x - GAP - CAM_BTN;

        // Details checkbox — vertically centred in the row
        const float det_x = cam_x - GAP*2.f - DET_W;
        ImGui::SetCursorPos(ImVec2(det_x,
                                   row_y + (ROW_H - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 2.f));
        if (ImGui::Checkbox("##details", &m_show_timeline_details))
            ; // panel height auto-updates via PANEL_H = dynamic above
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show time and layer rulers on the scrubber");

        // Camera motion button
        ImGui::SetCursorPos(ImVec2(cam_x, row_y));
        ImGui::InvisibleButton("##camera_btn", ImVec2(CAM_BTN, CAM_BTN));
        {
            const bool chov     = ImGui::IsItemHovered();
            const bool cclicked = ImGui::IsItemClicked();
            const ImVec2 cpos   = ImGui::GetItemRectMin();
            IconTheme eff = m_in_theme_menu ? m_preview_theme : m_theme;
            const bool cam_active = m_camera.is_panel_open() ||
                                    m_camera.settings().any_enabled();
            draw_btn_background(cpos, CAM_BTN, chov, cclicked,
                                cam_active, eff, m_anim_t);
            unsigned int cam_col = icon_color(chov, cclicked, eff, false);
            m_camera.render_button(cpos, CAM_BTN, chov, cam_active, cam_col);
            if (chov) ImGui::SetTooltip("Camera motion & viewport settings");
            if (cclicked) {
                if (m_camera.is_panel_open()) m_camera.close_panel();
                else                          m_camera.open_panel();
            }
        }

        // Export button
        ImGui::SetCursorPos(ImVec2(export_x, row_y));
        ImGui::InvisibleButton("##export_btn", ImVec2(EXP_BTN, EXP_BTN));
        {
            const bool ehov     = ImGui::IsItemHovered();
            const bool eclicked = ImGui::IsItemClicked();
            const ImVec2 epos   = ImGui::GetItemRectMin();
            IconTheme eff = m_in_theme_menu ? m_preview_theme : m_theme;
            draw_btn_background(epos, EXP_BTN, ehov, eclicked,
                                false, eff, m_anim_t);
            draw_icon_export(epos, EXP_BTN,
                             icon_color(ehov, eclicked, eff, false));
            if (ehov) ImGui::SetTooltip("Export animation");
            if (eclicked) m_export.open_dialog();
        }

        // Advance cursor past the row so the window content height is correct
        ImGui::SetCursorPosY(row_y + ROW_H + 4.f);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);

    // Camera motion panel (floating, positioned above its button in the panel)
    m_camera.render_panel(canvas_w, canvas_h,
                          canvas_w - 38.f,
                          canvas_h - PANEL_H - 62.f);

    // Let the export dialog draw on top (no-op when not open).
    m_export.render_dialog(canvas_w, canvas_h);
}

// ============================================================
//  tick
//  Called by GLCanvas3D's wxTimer (~16 ms interval).
//  dt: elapsed time in seconds since last call.
// ============================================================

void PathPreviewPlayer::tick(float dt)
{
    // Always advance animation time for smooth background animations.
    m_anim_t += dt;

    // Tick the export system (countdown, offscreen render loop, orbit).
    m_export.tick(dt);

    // Tick camera motion (orbit, rise, pitch) — only when playing.
    if (m_state.playing)
        m_camera.tick(dt, m_state.speed_multiplier);

    if (!m_state.playing || m_viewer == nullptr)
        return;

    const float spd = m_state.speed_multiplier;

    if (m_state.mode == PlaybackState::Mode::XYPath) {
        m_state.move_progress += (dt * spd) / k_spl_xy;
        if (m_state.move_progress >= 1.f) {
            m_state.move_progress = 0.f;
            if (m_state.current_layer < m_state.total_layers)
                ++m_state.current_layer;
            else
                m_state.playing = false;
        }
    } else {
        // Layers mode: reveal one whole completed layer at a time at a constant
        // rate of one layer per (1 / speed) seconds (1x = 1 s/layer, 2x = 0.5 s,
        // …). Accumulate elapsed time and step whole layers, subtracting the
        // slice (rather than zeroing) to preserve the remainder so the cadence
        // stays even; the while-loop keeps the rate accurate if a frame is long
        // or the speed is high enough to need more than one layer per frame.
        m_state.layer_timer += dt;
        const float secs_per_layer = k_spl_layers / spd;
        while (m_state.layer_timer >= secs_per_layer) {
            m_state.layer_timer -= secs_per_layer;
            if (m_state.current_layer < m_state.total_layers) {
                ++m_state.current_layer;
            } else {
                m_state.playing     = false;
                m_state.layer_timer = 0.f;
                break;
            }
        }
        m_state.move_progress = 0.f; // whole-layer look: no intra-layer animation
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

    const float avail_w   = ImGui::GetContentRegionAvail().x;
    const float slider_h  = ImGui::GetFrameHeight();

    // ── Top ruler: time markers ───────────────────────────────────────────────
    if (m_show_timeline_details) {
        const float ruler_h = 14.f;
        ImVec2 ruler_pos    = ImGui::GetCursorScreenPos();
        ImDrawList* dl      = ImGui::GetWindowDrawList();

        // Total animation duration estimate
        const float spl = 2.0f / m_state.speed_multiplier;
        const float total_secs = total * spl;

        // Choose a nice tick interval
        const float target_ticks = 8.f;
        float tick_secs = total_secs / target_ticks;
        // Round to a "nice" value
        const float nice[] = {0.5f,1.f,2.f,5.f,10.f,15.f,30.f,60.f,120.f,300.f};
        for (float n : nice) { if (n >= tick_secs) { tick_secs = n; break; } }
        tick_secs = std::max(tick_secs, 0.1f);

        // Draw tick marks and time labels above the slider
        for (float t = 0.f; t <= total_secs + tick_secs * 0.01f; t += tick_secs) {
            const float frac = std::min(t / total_secs, 1.f);
            const float x    = ruler_pos.x + frac * avail_w;
            dl->AddLine(ImVec2(x, ruler_pos.y + ruler_h - 5.f),
                        ImVec2(x, ruler_pos.y + ruler_h), IM_COL32(120,120,130,200), 1.f);
            if (frac < 0.95f || frac >= 1.f) { // skip crowded near-end labels
                char tbuf[16];
                if (t < 60.f) snprintf(tbuf, sizeof(tbuf), "%.0fs", t);
                else          snprintf(tbuf, sizeof(tbuf), "%.0fm", t/60.f);
                dl->AddText(ImVec2(x + 2.f, ruler_pos.y + 1.f),
                            IM_COL32(140,140,150,200), tbuf);
            }
        }
        // Current position marker (vertical line)
        {
            const float x = ruler_pos.x + global * avail_w;
            dl->AddLine(ImVec2(x, ruler_pos.y),
                        ImVec2(x, ruler_pos.y + ruler_h),
                        IM_COL32(100,210,140,220), 1.5f);
        }
        ImGui::Dummy(ImVec2(avail_w, ruler_h));
    }

    // ── Scrubber slider ────────────────────────────────────────────────────────
    ImGui::PushItemWidth(avail_w);
    if (ImGui::SliderFloat("##prog", &global, 0.f, 1.f, "")) {
        // Map global 0–1 back to layer + intra-layer progress.
        const float scaled    = global * total;
        m_state.current_layer = std::clamp(static_cast<int>(scaled) + 1,
                                            1, m_state.total_layers);
        m_state.move_progress = scaled - std::floor(scaled);
        m_state.layer_timer   = 0.f;
        apply_to_viewer();
    }
    ImGui::PopItemWidth();

    // ── Bottom ruler: layer markers ───────────────────────────────────────────
    if (m_show_timeline_details) {
        const float ruler_h = 14.f;
        ImVec2 ruler_pos    = ImGui::GetCursorScreenPos();
        ImDrawList* dl      = ImGui::GetWindowDrawList();

        // Choose a nice layer tick interval
        const int total_layers_i = m_state.total_layers;
        int layer_tick = 1;
        const int layer_nices[] = {1,2,5,10,25,50,100,250,500,1000};
        for (int n : layer_nices) {
            if (float(total_layers_i) / n <= 10.f) { layer_tick = n; break; }
        }

        for (int l = 0; l <= total_layers_i; l += layer_tick) {
            const float frac = float(l) / float(total_layers_i);
            const float x    = ruler_pos.x + frac * avail_w;
            dl->AddLine(ImVec2(x, ruler_pos.y),
                        ImVec2(x, ruler_pos.y + 5.f), IM_COL32(120,120,130,200), 1.f);
            if (l > 0 && (frac < 0.92f || l == total_layers_i)) {
                char lbuf[12];
                snprintf(lbuf, sizeof(lbuf), "%d", l);
                dl->AddText(ImVec2(x + 2.f, ruler_pos.y + 5.f),
                            IM_COL32(140,140,150,200), lbuf);
            }
        }
        // Current layer marker
        {
            const float frac = float(m_state.current_layer - 1) /
                               float(std::max(1, m_state.total_layers - 1));
            const float x = ruler_pos.x + frac * avail_w;
            dl->AddLine(ImVec2(x, ruler_pos.y),
                        ImVec2(x, ruler_pos.y + ruler_h),
                        IM_COL32(100,210,140,220), 1.5f);
        }
        ImGui::Dummy(ImVec2(avail_w, ruler_h));

        // Layer / time readout inline with scrubber
        char info[64];
        const float spl = 2.0f / m_state.speed_multiplier;
        const float elapsed = (m_state.current_layer - 1 + m_state.move_progress) * spl;
        const float total_t = total * spl;
        snprintf(info, sizeof(info), "Layer %d / %d  |  %.1fs / %.1fs",
                 m_state.current_layer, m_state.total_layers, elapsed, total_t);
        ImGui::TextDisabled("%s", info);
    }
}

// Draws a themed export icon: arrow-out-of-box shape, styled per theme.
// Hexagonal: arrow + film-strip notches on the shell sides.
// Circular:  simple upward arrow inside circle.
// Diamond:   arrow with a diamond tip.
void PathPreviewPlayer::draw_icon_export(ImVec2 p, float s, unsigned int col) const
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    IconTheme   th = m_in_theme_menu ? m_preview_theme : m_theme;

    const float cx  = p.x + s * 0.5f;
    const float cy  = p.y + s * 0.5f;
    const float m   = s * 0.18f;

    if (th == IconTheme::Hexagonal) {
        // Upward arrow shaft + head
        const float shaft_w  = s * 0.14f;
        const float shaft_h  = s * 0.28f;
        const float head_w   = s * 0.34f;
        const float head_tip = p.y + m;
        const float head_bot = head_tip + s * 0.28f;
        const float shaft_top = head_bot;
        const float shaft_bot = shaft_top + shaft_h;

        // Arrowhead (triangle)
        dl->AddTriangleFilled(
            ImVec2(cx - head_w, head_bot),
            ImVec2(cx + head_w, head_bot),
            ImVec2(cx,          head_tip), col);

        // Shaft
        dl->AddRectFilled(
            ImVec2(cx - shaft_w, shaft_top),
            ImVec2(cx + shaft_w, shaft_bot), col);

        // Tray / base line (export tray motif)
        dl->AddLine(
            ImVec2(p.x + m,      shaft_bot + s*0.06f),
            ImVec2(p.x + s - m,  shaft_bot + s*0.06f),
            col, 1.8f);

        // Small film-strip notches on the tray (hex theme accent)
        for (int i = 0; i < 3; ++i) {
            float nx = p.x + m + i * (s - 2.f*m) * 0.33f;
            dl->AddLine(
                ImVec2(nx, shaft_bot + s*0.06f),
                ImVec2(nx, shaft_bot + s*0.14f),
                col, 1.2f);
        }

    } else if (th == IconTheme::Circular) {
        // Clean minimal arrow: just the arrow + underline
        const float shaft_w  = s * 0.11f;
        const float head_w   = s * 0.30f;
        const float head_tip = p.y + m;
        const float head_bot = head_tip + s * 0.26f;
        const float shaft_bot = p.y + s - m - s * 0.14f;

        dl->AddTriangleFilled(
            ImVec2(cx - head_w, head_bot),
            ImVec2(cx + head_w, head_bot),
            ImVec2(cx,          head_tip), col);

        dl->AddRectFilled(
            ImVec2(cx - shaft_w, head_bot),
            ImVec2(cx + shaft_w, shaft_bot), col);

        // Underline (platform)
        dl->AddRectFilled(
            ImVec2(p.x + m,     shaft_bot),
            ImVec2(p.x + s - m, shaft_bot + s*0.10f), col);

    } else {
        // Diamond theme: arrow with angled/chamfered base — diamond motif
        const float shaft_w  = s * 0.12f;
        const float head_w   = s * 0.32f;
        const float head_tip = p.y + m;
        const float head_bot = head_tip + s * 0.25f;
        const float shaft_bot = p.y + s*0.72f;

        dl->AddTriangleFilled(
            ImVec2(cx - head_w, head_bot),
            ImVec2(cx + head_w, head_bot),
            ImVec2(cx,          head_tip), col);

        dl->AddRectFilled(
            ImVec2(cx - shaft_w, head_bot),
            ImVec2(cx + shaft_w, shaft_bot), col);

        // Diamond-shaped tray (rotated square)
        const float dr = s * 0.14f;
        const float dy = shaft_bot + dr + s*0.02f;
        ImVec2 dpts[4] = {
            ImVec2(cx,      dy - dr),
            ImVec2(cx + dr, dy     ),
            ImVec2(cx,      dy + dr),
            ImVec2(cx - dr, dy     ),
        };
        dl->AddConvexPolyFilled(dpts, 4, col);
    }
}

void PathPreviewPlayer::draw_mode_toggle()
{
    // Simple inline radio buttons for the two animation modes.
    bool xy = (m_state.mode == PlaybackState::Mode::XYPath);

    auto switch_to_xy = [&]() {
        m_state.mode = PlaybackState::Mode::XYPath;
        m_state.move_progress = 0.f; m_state.layer_timer = 0.f;
        apply_to_viewer();
    };
    auto switch_to_layers = [&]() {
        m_state.mode = PlaybackState::Mode::Layers;
        m_state.move_progress = 0.f; m_state.layer_timer = 0.f;
        apply_to_viewer();
    };

    if (m_mode_icons_enabled) {
        // ── Icon buttons (right-click either to revert to text) ───────────────
        constexpr float SZ = 20.f;
        IconTheme eff = m_in_theme_menu ? m_preview_theme : m_theme;

        // XY button
        ImGui::InvisibleButton("##mode_xy", ImVec2(SZ, SZ));
        {
            bool hov = ImGui::IsItemHovered();
            bool cl  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            bool rc  = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            if (cl) switch_to_xy();
            if (rc) m_mode_icons_enabled = false;
            ImVec2 p = ImGui::GetItemRectMin();
            draw_btn_background(p, SZ, hov, cl, xy, eff, m_anim_t);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            unsigned int col = icon_color(hov, cl, eff, xy);
            const float mg = SZ * 0.22f;
            // Two horizontal path lines with a connecting turn
            dl->AddLine({p.x+mg, p.y+SZ*0.38f}, {p.x+SZ-mg, p.y+SZ*0.38f}, col, 1.5f);
            dl->AddLine({p.x+mg, p.y+SZ*0.62f}, {p.x+SZ*0.60f, p.y+SZ*0.62f}, col, 1.5f);
            dl->AddCircleFilled({p.x+SZ-mg, p.y+SZ*0.50f}, 2.f, col);
            if (hov) ImGui::SetTooltip("XY path\n(right-click for text labels)");
        }

        ImGui::SameLine(0.f, 4.f);

        // Layers button
        ImGui::InvisibleButton("##mode_ly", ImVec2(SZ, SZ));
        {
            bool hov = ImGui::IsItemHovered();
            bool cl  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            bool rc  = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            if (cl) switch_to_layers();
            if (rc) m_mode_icons_enabled = false;
            ImVec2 p = ImGui::GetItemRectMin();
            draw_btn_background(p, SZ, hov, cl, !xy, eff, m_anim_t);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            unsigned int col = icon_color(hov, cl, eff, !xy);
            const float bm = SZ * 0.18f, bh = 3.f;
            const float bw = SZ - 2.f*bm;
            const float gap = (SZ - 2.f*bm - 3.f*bh) / 2.f;
            for (int i = 0; i < 3; ++i) {
                float by = p.y + bm + i * (bh + gap);
                dl->AddRectFilled({p.x+bm, by}, {p.x+bm+bw, by+bh}, col, 1.f);
            }
            if (hov) ImGui::SetTooltip("Layers\n(right-click for text labels)");
        }
    } else {
        // ── Text radio buttons (right-click re-enables icons) ─────────────────
        if (ImGui::RadioButton("XY path", xy)) switch_to_xy();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_mode_icons_enabled = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Layers", !xy)) switch_to_layers();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_mode_icons_enabled = true;
    }
}

void PathPreviewPlayer::draw_speed_combo()
{
    // Build display label
    char combo_label[32];
    if (m_speed_custom)
        snprintf(combo_label, sizeof(combo_label), "%.2fx*", m_state.speed_multiplier);
    else
        snprintf(combo_label, sizeof(combo_label), "%s", k_speed_labels[m_speed_idx]);

    // Deferred flag: set inside the combo, acted on after EndCombo.
    // This is necessary because OpenPopup() called inside BeginCombo/EndCombo
    // is silently swallowed — the combo's popup context has already been closed.
    bool open_custom_popup = false;

    ImGui::PushItemWidth(92.f);
    if (ImGui::BeginCombo("##spd", combo_label,
                          ImGuiComboFlags_HeightRegular))
    {
        if (ImGui::Selectable("Custom...", false))
            open_custom_popup = true;

        ImGui::Separator();

        for (int i = 0; i < k_num_speeds; ++i) {
            bool sel = (!m_speed_custom && i == m_speed_idx);
            if (ImGui::Selectable(k_speed_labels[i], sel)) {
                m_speed_idx              = i;
                m_state.speed_multiplier = k_speeds[i];
                m_speed_custom           = false;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();
        if (ImGui::Selectable("Custom...##bot", false))
            open_custom_popup = true;

        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed (* = custom)");

    // ── Custom speed popup ─────────────────────────────────────────────────────
    // OpenPopup must be called OUTSIDE BeginCombo/EndCombo for it to work.
    if (open_custom_popup) {
        // Seed the static value from current speed before opening
        m_custom_speed_val = m_state.speed_multiplier;
        ImGui::OpenPopup("##custom_speed_popup");
    }

    ImGui::SetNextWindowSize(ImVec2(240.f, 100.f), ImGuiCond_Always);
    if (ImGui::BeginPopup("##custom_speed_popup",
                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::TextDisabled("Custom playback speed");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushItemWidth(110.f);
        if (ImGui::InputFloat("##cspd", &m_custom_speed_val,
                              0.25f, 1.f, "%.2f x",
                              ImGuiInputTextFlags_EnterReturnsTrue))
        {
            // Enter pressed — commit immediately
            m_custom_speed_val       = std::clamp(m_custom_speed_val, 0.01f, 64.f);
            m_state.speed_multiplier = m_custom_speed_val;
            m_speed_custom           = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopItemWidth();
        m_custom_speed_val = std::clamp(m_custom_speed_val, 0.01f, 64.f);

        ImGui::SameLine(0.f, 8.f);
        if (ImGui::Button("Set")) {
            m_state.speed_multiplier = m_custom_speed_val;
            m_speed_custom           = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.f, 6.f);
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
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
        m_state.move_progress >= 1.f)
    {
        m_state.current_layer = 1;
        m_state.move_progress = 0.f;
        m_state.layer_timer   = 0.f;
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
    m_state.layer_timer   = 0.f;
    apply_to_viewer();
}

void PathPreviewPlayer::step_moves(float delta_pct)
{
    if (m_viewer == nullptr) return;
    m_state.move_progress = std::clamp(
        m_state.move_progress + delta_pct, 0.f, 1.f);
    apply_to_viewer();
}

// ── apply_to_viewer ───────────────────────────────────────────────────────────
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
        // Layers mode: reveal each completed layer as a whole unit. Pin the
        // layer slider to the current layer and show all of its moves; the
        // steady vertical motion comes from the constant-rate layer advance in
        // tick(), one whole layer per (1 / speed) seconds.
        if (layers_slider->GetHigherValue() != layer_idx) {
            layers_slider->SetHigherValue(layer_idx);
            layers_slider->set_as_dirty(true);
        }
        moves_slider->SetHigherValue(moves_slider->GetMaxValue());
        moves_slider->set_as_dirty(true);
    }
}

} // namespace GUI
} // namespace Slic3r
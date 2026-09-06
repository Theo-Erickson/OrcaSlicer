#ifndef slic3r_GizmoObjectManipulation_hpp_
#define slic3r_GizmoObjectManipulation_hpp_

#include <memory>
#include <optional>
#include <array>
#include <string>
#include <initializer_list>

#include "libslic3r/Point.hpp"
#include "libslic3r/Geometry.hpp"
#include <float.h>

#include "slic3r/GUI/GUI_Geometry.hpp"

//#include "slic3r/GUI/GLCanvas3D.hpp"

namespace Slic3r {

// GLVolume lives in the Slic3r namespace (not Slic3r::GUI); forward-declare it
// here so a GUI-namespace declaration does not shadow it.
class GLVolume;

namespace GUI {

class Selection;
class GLCanvas3D;

class GizmoObjectManipulation
{
public:
    static const double in_to_mm;
    static const double mm_to_in;
    static const double g_to_oz;
    static const double oz_to_g;

    struct Cache
    {
        Vec3d position;
        Vec3d position_rounded;
        Vec3d rotation;
        Vec3d rotation_rounded;
        Vec3d absolute_rotation;
        Vec3d absolute_rotation_rounded;
        Vec3d scale;
        Vec3d scale_rounded;
        Vec3d size;
        Vec3d size_rounded;

        wxString move_label_string;
        wxString rotate_label_string;
        wxString scale_label_string;

        Cache() { reset(); }
        void reset()
        {
            position = position_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            rotation = rotation_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            scale = scale_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            size = size_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            move_label_string = wxString();
            rotate_label_string = wxString();
            scale_label_string = wxString();
        }
        bool is_valid() const { return position != Vec3d(DBL_MAX, DBL_MAX, DBL_MAX); }
    };

    Cache m_cache;

    // Session clipboard for transferring a transform between objects.
    // One optional slot per manipulation row: a row copy fills exactly one slot,
    // the master copy fills all of them. This is what lets a user hold rotation
    // from object A and scale from object B at the same time. Every slot is stored
    // in world space at copy time; conversion to the panel's current coordinate
    // space happens at paste, not at copy.
    struct TransformClipboard
    {
        std::optional<Vec3d>       position;        // mm, world
        std::optional<Vec3d>       rotation;        // radians, world
        std::optional<Vec3d>       scale_factors;   // unitless, mirror encoded as sign
        std::optional<Vec3d>       size;            // mm, only meaningful for same-mesh pastes
        std::optional<Vec3d>       mirror;          // kept explicit for readout clarity

        // Raw source matrix, stored only to validate the decomposition. After
        // building a paste from the slots above, rebuild a matrix and compare:
        // divergence beyond epsilon means the source had shear.
        std::optional<Transform3d> source_matrix;

        // Per-component, per-axis paste filter driven by the inline value chips
        // (index order X, Y, Z). Independent per row, so a user can, e.g., disable
        // rotation entirely while still pasting position. Applies to paste and the
        // eyedropper apply.
        std::array<bool, 3>        axis_position { true, true, true };
        std::array<bool, 3>        axis_rotation { true, true, true };
        std::array<bool, 3>        axis_scale    { true, true, true };
        std::array<bool, 3>        axis_size     { true, true, true };
        std::array<bool, 3>        axis_mirror   { true, true, true };

        std::array<bool, 3>& axes_for(const std::string &slot) {
            if (slot == "rotation") return axis_rotation;
            if (slot == "scale")    return axis_scale;
            if (slot == "size")     return axis_size;
            if (slot == "mirror")   return axis_mirror;
            return axis_position;
        }
        const std::array<bool, 3>& axes_for(const std::string &slot) const {
            if (slot == "rotation") return axis_rotation;
            if (slot == "scale")    return axis_scale;
            if (slot == "size")     return axis_size;
            if (slot == "mirror")   return axis_mirror;
            return axis_position;
        }

        // What the held data came from, shown in the readout header.
        std::string                source_label;

        bool empty() const { return !position && !rotation && !scale_factors && !size && !mirror; }
    };
    TransformClipboard m_transform_clipboard;
    // "Don't ask again this session" flags for the paste warning dialogs.
    bool            m_tc_skip_shear_warning { false };
    bool            m_tc_skip_uniform_warning { false };
    // Eyedropper mode: when true the next pick only loads the clipboard from the
    // donor (copy from); when false it also applies it to the selection.
    bool            m_tc_eyedropper_copy_only { false };

    bool            m_imperial_units { false };
    bool            m_use_object_cs{false};
    // Mirroring buttons and their current state
    //enum MirrorButtonState {
    //    mbHidden,
    //    mbShown,
    //    mbActive
    //};
    //std::array<std::pair<ScalableButton*, MirrorButtonState>, 3> m_mirror_buttons;

    // Needs to be updated from OnIdle?
    bool            m_dirty = false;
    // Cached labels for the delayed update, not localized!
    std::string     m_new_title_string;
    std::string     m_new_move_label_string;
	std::string     m_new_rotate_label_string;
	std::string     m_new_scale_label_string;
    std::string     m_new_unit_string;
    Vec3d           m_new_position;
    Vec3d           m_new_rotation;
    Vec3d           m_new_absolute_rotation;
    Vec3d           m_new_scale;
    Vec3d           m_new_size;
    Vec3d           m_unscale_size;
    Vec3d           m_buffered_position;
    Vec3d           m_buffered_rotation;
    Vec3d           m_buffered_absolute_rotation;
    Vec3d           m_buffered_scale;
    Vec3d           m_buffered_size;
    Vec3d           cs_center;
    bool            m_new_enabled {true};
    bool            m_uniform_scale {true};
    // Does the object manipulation panel work in World or Local coordinates?
    ECoordinatesType m_coordinates_type{ECoordinatesType::World};

    bool            m_show_reset_0_rotation{false};
    bool            m_show_clear_rotation { false };
    bool            m_show_clear_scale { false };
    bool            m_show_drop_to_bed { false };
    enum class RotateType { None, Relative, Absolute
    };
    RotateType m_last_rotate_type{RotateType::None}; // 0:no input 1:relative 2:absolute

protected:
    float last_move_input_window_width = 0.0f;
    float last_rotate_input_window_width = 0.0f;
    float last_scale_input_window_width = 0.0f;

public:
    GizmoObjectManipulation(GLCanvas3D& glcanvas);
    ~GizmoObjectManipulation() {}

    bool        IsShown();
    void        UpdateAndShow(const bool show);
    void update_ui_from_settings();

    void        set_dirty() { m_dirty = true; }
	// Called from the App to update the UI if dirty.
	void		update_if_dirty();

    void        set_uniform_scaling(const bool uniform_scale);
    bool        get_uniform_scaling() const { return m_uniform_scale; }
    void        set_use_object_cs(bool flag){ if (m_use_object_cs != flag) m_use_object_cs = flag; }
    bool        get_use_object_cs() { return m_use_object_cs; }
    // Does the object manipulation panel work in World or Local coordinates?
    void        set_coordinates_type(ECoordinatesType type);
    ECoordinatesType get_coordinates_type() const { return m_coordinates_type; }
    bool        is_world_coordinates() const { return m_coordinates_type == ECoordinatesType::World; }
    bool        is_instance_coordinates() const { return m_coordinates_type == ECoordinatesType::Instance; }
    bool        is_local_coordinates() const { return m_coordinates_type == ECoordinatesType::Local; }

    const Cache& get_cache() {return m_cache; }
    void reset_cache() { m_cache.reset(); }

    void limit_scaling_ratio(Vec3d &scaling_factor) const;
    void on_change(const std::string& opt_key, int axis, double new_value);
    bool render_combo(ImGuiWrapper *imgui_wrapper, const std::string &label, const std::vector<std::string> &lines, size_t &selection_idx, float label_width, float item_width);
    void do_render_move_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit);
    void do_render_rotate_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit);
    void do_render_scale_input_window(ImGuiWrapper* imgui_wrapper, std::string window_name, float x, float y, float bottom_limit);
    // Transform clipboard UI, rendered inline at the bottom of each manipulation
    // gizmo window. surfaced_slots are the rows shown above the collapsed panel
    // for the active tool (e.g. {"position"} for Move, {"scale","size"} for Scale).
    void do_render_clipboard_window(ImGuiWrapper *imgui_wrapper, std::initializer_list<const char *> surfaced_slots);
    // Eyedropper: copy the donor's transform into the clipboard and (in apply
    // mode) paste it onto the selection. The donor level auto-matches the paste
    // target's kind (whole object vs part); in copy-from mode there is no target,
    // so it defaults to the whole object and alt_pick_part copies the part under
    // the cursor. Called by GLCanvas3D when a donor is clicked.
    void eyedropper_commit(const GLVolume *donor, bool alt_pick_part);
    // Validity of a hovered donor for the current picking mode/selection:
    // 0 = none, 1 = valid (green), 2 = invalid (red). Drives the scene highlight
    // and gates the commit.
    int eyedropper_validity(const GLVolume *donor) const;
    // Human-readable reason a donor can't be picked (empty when it can). Shown in
    // the hover readout so the red state is self-explanatory.
    std::string eyedropper_reason(const GLVolume *donor) const;
    // True while the "Copy from" (load-clipboard-only) pick mode is armed.
    bool is_eyedropper_copy_only() const { return m_tc_eyedropper_copy_only; }
    // Eyedropper commit from an object-list item resolved to model indices (the
    // tree picking surface). is_part selects part vs instance level.
    void eyedropper_commit_from(int obj_idx, int inst_idx, int vol_idx, bool is_part);
    float max_unit_size(int number, Vec3d &vec1, Vec3d &vec2,std::string str);
    bool reset_button(ImGuiWrapper *imgui_wrapper, bool enabled);
    bool reset_zero_button(ImGuiWrapper *imgui_wrapper, bool enabled);
    bool bbl_checkbox(const wxString &label, bool &value);

    void set_init_rotation(const Geometry::Transformation &value);

private:
    void reset_settings_value();
    void update_settings_value(const Selection& selection);
    void update_buffered_value();

    // Show or hide scale/rotation reset buttons if needed
    void update_reset_buttons_visibility();
    //Show or hide mirror buttons
    //void update_mirror_buttons_visibility();

    // change values
    void change_position_value(int axis, double value);
    void change_rotation_value(int axis, double value);
    void change_absolute_rotation_value(int axis, double value);
    void change_scale_value(int axis, double value);
    void change_size_value(int axis, double value);
    void do_scale(int axis, const Vec3d &scale) const;

    // Transform clipboard helpers.
    // Returns the world-space transformation of the current single selection,
    // or std::nullopt when the selection is not a single instance/volume.
    std::optional<Geometry::Transformation> get_selection_world_transformation() const;
    // slot is one of: "position", "rotation", "scale", "size", "mirror", "all".
    void clipboard_copy(const std::string &slot);
    void clipboard_paste(const std::string &slot);
    bool clipboard_slot_filled(const std::string &slot) const;
    // Fill every clipboard slot (position/rotation/scale/mirror + source matrix)
    // from a world-space transform, tagging the readout with the given label.
    void fill_clipboard_from_world(const Geometry::Transformation &world, const std::string &label);
    // Readout label for a source: the model object's name (or the part's name for
    // a part), falling back to the generic kind when unnamed.
    std::string source_label_for(int obj_idx, int vol_idx, bool from_part) const;

    void reset_position_value();
    void reset_rotation_value(bool reset_relative);
    void reset_scale_value();

    GLCanvas3D& m_glcanvas;
    unsigned int m_last_active_item { 0 };

    // Contains all shortcuts in the format of {shortcut, description}, e.g. {alt + _L("Left mouse button"), _L("Part_selection")}
    std::vector<std::pair<wxString, wxString>> m_shortcuts_move;
    // Contains all shortcuts in the format of {shortcut, description}, e.g. {alt + _L("Left mouse button"), _L("Part_selection")}
    std::vector<std::pair<wxString, wxString>> m_shortcuts_rotate;
    // Contains all shortcuts in the format of {shortcut, description}, e.g. {alt + _L("Left mouse button"), _L("Part_selection")}
    std::vector<std::pair<wxString, wxString>> m_shortcuts_scale;

    Vec3d                           m_init_rotation;
    Transform3d                     m_init_rotation_scale_tran;
};

}}

#endif // slic3r_GizmoObjectManipulation_hpp_
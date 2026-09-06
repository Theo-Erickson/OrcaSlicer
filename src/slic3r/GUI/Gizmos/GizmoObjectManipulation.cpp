#include "slic3r/GUI/ImGuiWrapper.hpp"
#include <imgui/imgui_internal.h>

#include "GizmoObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
//#include "I18N.hpp"
#include "GLGizmosManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/AppConfig.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "GLGizmoUtils.hpp"

#include <boost/algorithm/string.hpp>

#define MAX_NUM 9999.99
#define MAX_SIZE std::string_view{"9999.99"}

namespace Slic3r
{
namespace GUI
{

const double GizmoObjectManipulation::in_to_mm = 25.4;
const double GizmoObjectManipulation::mm_to_in = 0.0393700787;
const double GizmoObjectManipulation::oz_to_g = 28.34952;
const double GizmoObjectManipulation::g_to_oz = 0.035274;

// Helper function to be used by drop to bed button. Returns lowest point of this
// volume in world coordinate system.
static double get_volume_min_z(const GLVolume* volume)
{
    const Transform3f& world_matrix = volume->world_matrix().cast<float>();

    // need to get the ModelVolume pointer
    const ModelObject* mo = wxGetApp().model().objects[volume->composite_id.object_id];
    const ModelVolume* mv = mo->volumes[volume->composite_id.volume_id];
    const TriangleMesh& hull = mv->get_convex_hull();

    float min_z = std::numeric_limits<float>::max();
    for (const stl_vertex& vert : hull.its.vertices) {
        min_z = std::min(min_z, Vec3f::UnitZ().dot(world_matrix * vert));
    }
    return min_z;
}

GizmoObjectManipulation::GizmoObjectManipulation(GLCanvas3D& glcanvas)
    : m_glcanvas(glcanvas)
{
    m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    m_new_unit_string = m_imperial_units ? L_CONTEXT("in", "inches") : L("mm");

    const wxString shift                   = GUI::shortkey_shift_prefix();
    const wxString alt                     = GUI::shortkey_alt_prefix();
    const wxString ctrl                    = GUI::shortkey_ctrl_prefix();

    m_shortcuts_move = {
        {alt + _L("Left mouse button"),     _L("Part selection")},
        {shift + _L("Left mouse button"),   _L("Fixed step drag")},
        {_L("Context Menu"),                _L("Toggle Auto-Drop")}
    };

    m_shortcuts_rotate = {
        {alt + _L("Left mouse button"),     _L("Part selection")},
        {_L("Context Menu"),                _L("Toggle Auto-Drop")}
    };

    m_shortcuts_scale = {
        {alt + _L("Left mouse button"),     _L("Part selection")},
        {shift + _L("Left mouse button"),   _L("Fixed step drag")},
        {ctrl + _L("Left mouse button"),    _L("Single sided scaling")},
        {_L("Context Menu"),                _L("Toggle Auto-Drop")}
    };
}

void GizmoObjectManipulation::UpdateAndShow(const bool show)
{
	if (show) {
        this->set_dirty();
		this->update_if_dirty();
	}
}

void GizmoObjectManipulation::update_ui_from_settings()
{
    if (m_imperial_units != (wxGetApp().app_config->get("use_inches") == "1")) {
        m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";

        m_new_unit_string = m_imperial_units ? L_CONTEXT("in", "inches") : L("mm");

        update_buffered_value();
    }
}
void delete_negative_sign(Vec3d& value) {
    for (size_t i = 0; i < value.size(); i++) {
        if (abs(value[i]) < 0.001)
            value[i] = 0.f;
    }
}

void GizmoObjectManipulation::update_settings_value(const Selection &selection)
{
	m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotate (relative)");
    m_new_rotation            = Vec3d::Zero();
    m_new_absolute_rotation   = Vec3d::Zero();
    m_new_scale_label_string  = L("Scale ratios");

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance()) {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_first_volume();
        m_new_position = volume->get_instance_offset();
        auto rotation = volume->get_instance_transformation().get_rotation_by_quaternion();
        m_new_absolute_rotation = rotation * (180. / M_PI);
        delete_negative_sign(m_new_absolute_rotation);
        if (is_world_coordinates()) {//for move and rotate
            m_new_size     = selection.get_bounding_box_in_current_reference_system().first.size();
            m_unscale_size = selection.get_unscaled_instance_bounding_box().size();
            m_new_scale    = m_new_size.cwiseQuotient(m_unscale_size) * 100.0;
		}
        else {//if (is_local_coordinates()) {//for scale
            auto tran      = selection.get_first_volume()->get_instance_transformation();
            m_new_position = tran.get_matrix().inverse() * cs_center;
            if (is_instance_coordinates()) {
                m_new_position = Vec3d::Zero();
            }
            m_new_size  = selection.get_bounding_box_in_current_reference_system().first.size();
            m_unscale_size = selection.get_full_unscaled_instance_local_bounding_box().size();
            m_new_scale    = m_new_size.cwiseQuotient(m_unscale_size) * 100.0;
		}

        m_new_enabled  = true;
        // BBS: change "Instance Operations" to "Object Operations"
        m_new_title_string = L("Object operations");
    }
    else if (selection.is_single_full_object() && obj_list->is_selected(itObject)) {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_scale    = Vec3d(100., 100., 100.);
        m_new_size     = selection.get_bounding_box_in_current_reference_system().first.size();
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
        m_new_title_string = L("Object operations");
    } else if (selection.is_single_volume_or_modifier()) {
        const GLVolume *volume = selection.get_first_volume();
        auto            rotation = volume->get_volume_transformation().get_rotation_by_quaternion();
        m_new_absolute_rotation  = rotation * (180. / M_PI);
        delete_negative_sign(m_new_absolute_rotation);
        if (is_world_coordinates()) {//for move and rotate
            const Geometry::Transformation trafo(volume->world_matrix());
            const Vec3d &offset = trafo.get_offset();
            m_new_position            = offset;
            m_new_scale               = Vec3d(100.0, 100.0, 100.0);
            m_unscale_size            = selection.get_bounding_box_in_current_reference_system().first.size();
            m_new_size                = selection.get_bounding_box_in_current_reference_system().first.size();
        } else if (is_local_coordinates()) {//for scale
            m_new_position            = Vec3d::Zero();
            m_new_scale               = volume->get_volume_scaling_factor() * 100.0;
            m_unscale_size            = selection.get_bounding_box_in_current_reference_system().first.size();
            m_new_size                = selection.get_bounding_box_in_current_reference_system().first.size();
        } else {
            m_new_position            = volume->get_volume_offset();
            m_new_scale_label_string  = L("Scale");
            m_new_scale               = Vec3d(100.0, 100.0, 100.0);
            m_unscale_size            = selection.get_bounding_box_in_current_reference_system().first.size();
            m_new_size                = selection.get_bounding_box_in_current_reference_system().first.size();
        }
        m_new_enabled = true;
        m_new_title_string = L("Volume operations");
    } else if (obj_list->is_connectors_item_selected() || obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot)) {
        reset_settings_value();
		m_new_move_label_string   = L("Translate");
		m_new_scale_label_string  = L("Scale");
        m_unscale_size            = selection.get_bounding_box_in_current_reference_system().first.size();
        m_new_size                = selection.get_bounding_box_in_current_reference_system().first.size();
        m_new_enabled  = true;
        m_new_title_string = L("Group operations");
    } else if (selection.is_wipe_tower()) {
        const BoundingBoxf3 &box = selection.get_bounding_box();
        m_new_position           = box.center();
    }
	else {
        // No selection, reset the cache.
//		assert(selection.is_empty());
		reset_settings_value();
	}
}

void GizmoObjectManipulation::update_buffered_value()
{
    if (this->m_imperial_units)
        m_buffered_position = this->m_new_position * this->mm_to_in;
    else
        m_buffered_position = this->m_new_position;

    m_buffered_rotation = this->m_new_rotation;
    m_buffered_absolute_rotation = this->m_new_absolute_rotation;
    m_buffered_scale = this->m_new_scale;

    if (this->m_imperial_units)
        m_buffered_size = this->m_new_size * this->mm_to_in;
    else
        m_buffered_size = this->m_new_size;
}

void GizmoObjectManipulation::update_if_dirty()
{
    if (! m_dirty)
        return;

    const Selection &selection = m_glcanvas.get_selection();
    this->update_settings_value(selection);
    this->update_buffered_value();

    auto update_label = [](wxString &label_cache, const std::string &new_label) {
        wxString new_label_localized = _(new_label) + ":";
        if (label_cache != new_label_localized) {
            label_cache = new_label_localized;
        }
    };
    update_label(m_cache.move_label_string,   m_new_move_label_string);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string);
    update_label(m_cache.scale_label_string,  m_new_scale_label_string);

    enum ManipulationEditorKey
    {
        mePosition = 0,
        meRotation,
        meScale,
        meSize
    };

    for (int i = 0; i < 3; ++ i) {
        auto update = [this, i](Vec3d &cached, Vec3d &cached_rounded,  const Vec3d &new_value) {
			//wxString new_text = double_to_string(new_value(i), 2);
			double new_rounded = round(new_value(i)*100)/100.0;
			//new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                //const int id = key_id*3+i;
                //if (m_imperial_units && (key_id == mePosition || key_id == meSize))
                //    new_text = double_to_string(new_value(i)*mm_to_in, 2);
                //if (id >= 0) m_editors[id]->set_value(new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded,  m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,     m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,      m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded,  m_new_rotation);
        update(m_cache.absolute_rotation, m_cache.absolute_rotation_rounded, m_new_absolute_rotation);
    }

    update_reset_buttons_visibility();
    //update_mirror_buttons_visibility();

    m_dirty = false;
}

void GizmoObjectManipulation::update_reset_buttons_visibility()
{
    const Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) {
        const GLVolume *               volume = selection.get_first_volume();

        Vec3d rotation;
        Vec3d scale;
        double min_z = 0.;

        if (selection.is_single_full_instance()) {
            rotation = volume->get_instance_rotation();
            scale = volume->get_instance_scaling_factor();
        }
        else {
            rotation = volume->get_volume_rotation();
            scale = volume->get_volume_scaling_factor();
            min_z = get_volume_min_z(volume);
        }
        m_show_clear_rotation = !rotation.isApprox(m_init_rotation);
        m_show_reset_0_rotation = !rotation.isApprox(Vec3d::Zero());
        m_show_clear_scale = (m_cache.scale / 100.0f - Vec3d::Ones()).norm() > 0.001;
        m_show_drop_to_bed = (std::abs(min_z) > EPSILON);
    }
}


void GizmoObjectManipulation::reset_settings_value()
{
    m_new_position = Vec3d::Zero();
    m_new_rotation = Vec3d::Zero();
    m_new_absolute_rotation = Vec3d::Zero();
    m_new_scale = Vec3d::Ones() * 100.;
    m_new_size = Vec3d::Zero();
    m_new_enabled = false;
    // no need to set the dirty flag here as this method is called from update_settings_value(),
    // which is called from update_if_dirty(), which resets the dirty flag anyways.
//    m_dirty = true;
}

void GizmoObjectManipulation::change_position_value(int axis, double value)
{
    if (std::abs(m_cache.position_rounded(axis) - value) < EPSILON)
        return;

    Vec3d position = m_cache.position;
    position(axis) = value;

    Selection& selection = m_glcanvas.get_selection();
    selection.setup_cache();
    TransformationType trafo_type;
    trafo_type.set_relative();
    switch (m_coordinates_type) {
    case ECoordinatesType::Instance: {
        trafo_type.set_instance();
        break;
    }
    case ECoordinatesType::Local: {
        trafo_type.set_local();
        break;
    }
    default: {
        break;
    }
    }
    selection.translate(position - m_cache.position, trafo_type);
    wxGetApp().plater()->take_snapshot("Set Position", UndoRedo::SnapshotType::GizmoAction);
    m_glcanvas.do_move("");

    m_cache.position = position;
	m_cache.position_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_rotation_value(int axis, double value)
{
    if (std::abs(m_cache.rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d rotation = m_cache.rotation;
    rotation(axis) = value;

    Selection& selection = m_glcanvas.get_selection();

    TransformationType transformation_type;
    transformation_type.set_relative();
    if (selection.is_single_full_instance())
        transformation_type.set_independent();
    if (is_local_coordinates())
        transformation_type.set_local();
    if (is_instance_coordinates())
        transformation_type.set_instance();

    selection.setup_cache();
    selection.rotate((M_PI / 180.0) * (transformation_type.absolute() ? rotation : rotation - m_cache.rotation), transformation_type);
    wxGetApp().plater()->take_snapshot(_u8L("Set orientation"), UndoRedo::SnapshotType::GizmoAction);
    m_glcanvas.do_rotate("");

    m_cache.rotation = rotation;
	m_cache.rotation_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_absolute_rotation_value(int axis, double value) {
    if (std::abs(m_cache.absolute_rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d absolute_rotation = m_cache.absolute_rotation;
    absolute_rotation(axis) = value;

    Selection &selection = m_glcanvas.get_selection();
    TransformationType transformation_type;
    transformation_type.set_relative();
    if (selection.is_single_full_instance())
        transformation_type.set_independent();
    if (is_local_coordinates())
        transformation_type.set_local();
    if (is_instance_coordinates())
        transformation_type.set_instance();

    selection.setup_cache();
    auto diff_rotation = transformation_type.absolute() ? absolute_rotation : absolute_rotation - m_cache.absolute_rotation;
    selection.rotate((M_PI / 180.0) * diff_rotation, transformation_type);
    wxGetApp().plater()->take_snapshot("set absolute orientation", UndoRedo::SnapshotType::GizmoAction);
    m_glcanvas.do_rotate("");

    m_cache.absolute_rotation               = absolute_rotation;
    m_cache.absolute_rotation_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_scale_value(int axis, double value)
{
    if (value <= 0.0)
        return;
    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON) {
        m_show_clear_scale = (m_cache.scale / 100.0f - Vec3d::Ones()).norm() > 0.001;
        return;
    }
    Vec3d scale     = m_cache.scale;
    scale(axis)     = value;
    Vec3d ref_scale = m_cache.scale;
    const Selection &selection = m_glcanvas.get_selection();
    if (selection.is_single_volume_or_modifier()) {
        scale     = scale.cwiseQuotient(ref_scale); // scale / ref_scale
        ref_scale =  Vec3d::Ones();
    } else if (selection.is_single_full_instance())
        ref_scale = 100 * Vec3d::Ones();
    this->do_scale(axis, scale.cwiseQuotient(ref_scale));

    m_cache.scale = scale;
	m_cache.scale_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}


void GizmoObjectManipulation::change_size_value(int axis, double value)
{
    if (value <= 0.0)
        return;
    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = m_glcanvas.get_selection();

    Vec3d ref_size = m_cache.size;
    if (selection.is_single_volume_or_modifier()) {
        size     = size.cwiseQuotient(ref_size);
        ref_size = Vec3d::Ones();
    } else if (selection.is_single_full_instance()) {
        if (is_world_coordinates())
            ref_size = selection.get_full_unscaled_instance_bounding_box().size();
        else
            ref_size = selection.get_full_unscaled_instance_local_bounding_box().size();
    }

    this->do_scale(axis, size.cwiseQuotient(ref_size));

    m_cache.size = size;
	m_cache.size_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void GizmoObjectManipulation::do_scale(int axis, const Vec3d &scale) const
{
    Selection& selection = m_glcanvas.get_selection();

    TransformationType transformation_type;
    if (is_local_coordinates())
        transformation_type.set_local();
    else if (is_instance_coordinates())
        transformation_type.set_instance();
    if (selection.is_single_volume_or_modifier() && !is_local_coordinates())
        transformation_type.set_relative();

    Vec3d scaling_factor = m_uniform_scale ? scale(axis) * Vec3d::Ones() : scale;
    limit_scaling_ratio(scaling_factor);

    selection.setup_cache();
    selection.scale(scaling_factor, transformation_type);
    m_glcanvas.do_scale(L("Set scale"));
}


void GizmoObjectManipulation::limit_scaling_ratio(Vec3d &scaling_factor) const{
    for (size_t i = 0; i < scaling_factor.size(); i++) { // range protect //scaling_factor too big has problem
        if (scaling_factor[i] * m_unscale_size[i] > MAX_NUM) {
            scaling_factor[i] = MAX_NUM / m_unscale_size[i];
        }
    }
}

void GizmoObjectManipulation::on_change(const std::string &opt_key, int axis, double new_value)
{
    if (!m_cache.is_valid())
        return;

    if (m_imperial_units && (opt_key == "position" || opt_key == "size"))
        new_value *= in_to_mm;

    if (opt_key == "position")
        change_position_value(axis, new_value);
    else if (opt_key == "rotation")
        change_rotation_value(axis, new_value);
    else if (opt_key == "absolute_rotation")
        change_absolute_rotation_value(axis, new_value);
    else if (opt_key == "scale")
        change_scale_value(axis, new_value);
    else if (opt_key == "size")
        change_size_value(axis, new_value);
}

bool GizmoObjectManipulation::render_combo(
    ImGuiWrapper *imgui_wrapper, const std::string &label, const std::vector<std::string> &lines, size_t &selection_idx, float label_width, float item_width)
{
    if(!label.empty()){
        ImGui::AlignTextToFramePadding();
        imgui_wrapper->text(label);
        ImGui::SameLine(label_width);
    }

    ImGui::PushItemWidth(item_width);

    size_t selection_out = selection_idx;

    const char *selected_str = (selection_idx >= 0 && selection_idx < int(lines.size())) ? lines[selection_idx].c_str() : "";
    if (ImGui::BBLBeginCombo(("##" + label).c_str(), selected_str, 0)) {
        for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
            ImGui::PushID(int(line_idx));
            if (ImGui::Selectable("", line_idx == selection_idx)) selection_out = line_idx;

            ImGui::SameLine();
            ImGui::Text("%s", lines[line_idx].c_str());
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    bool is_changed = selection_idx != selection_out;
    selection_idx   = selection_out;

    return is_changed;
}

void GizmoObjectManipulation::reset_position_value()
{
    Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_volume() || selection.is_single_modifier()) {
        GLVolume* volume = const_cast<GLVolume*>(selection.get_first_volume());
        volume->set_volume_offset(Vec3d::Zero());
    }
    else if (selection.is_single_full_instance()) {
        for (unsigned int idx : selection.get_volume_idxs()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
            volume->set_instance_offset(Vec3d::Zero());
        }
    }
    else
        return;

    // Copy position values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
    wxGetApp().plater()->take_snapshot(_u8L("Reset position"), UndoRedo::SnapshotType::GizmoAction);
    m_glcanvas.do_move("");

    UpdateAndShow(true);
}

void GizmoObjectManipulation::reset_rotation_value(bool reset_relative)
{
    Selection &selection = m_glcanvas.get_selection();
    selection.setup_cache();
    if (selection.is_single_volume_or_modifier()) {
        GLVolume *               vol    = const_cast<GLVolume *>(selection.get_first_volume());
        Geometry::Transformation trafo  = vol->get_volume_transformation();
        if (reset_relative) {
            auto offset = trafo.get_offset();
            trafo.set_matrix(m_init_rotation_scale_tran);
            trafo.set_offset(offset);
        }
        else {
            trafo.reset_rotation();
        }
        vol->set_volume_transformation(trafo);
    } else if (selection.is_single_full_instance()) {
        Geometry::Transformation trafo  = selection.get_first_volume()->get_instance_transformation();
        if (reset_relative) {
            auto offset = trafo.get_offset();
            trafo.set_matrix(m_init_rotation_scale_tran);
            trafo.set_offset(offset);
        } else {
            trafo.reset_rotation();
        }
        for (unsigned int idx : selection.get_volume_idxs()) {
            const_cast<GLVolume *>(selection.get_volume(idx))->set_instance_transformation(trafo);
        }
    } else
        return;
    // Synchronize instances/volumes.

    selection.synchronize_unselected_instances(Selection::SyncRotationType::RESET);
    selection.synchronize_unselected_volumes();
    // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
    m_glcanvas.do_rotate(L("Reset rotation"));

    UpdateAndShow(true);
}

void GizmoObjectManipulation::reset_scale_value()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset scale");

    change_scale_value(0, 100.);
    change_scale_value(1, 100.);
    change_scale_value(2, 100.);
}

void GizmoObjectManipulation::set_uniform_scaling(const bool use_uniform_scale)
{
    if (!use_uniform_scale)
        // Recalculate cached values at this panel, refresh the screen.
        this->UpdateAndShow(true);

    m_uniform_scale = use_uniform_scale;
    set_dirty();
}

void GizmoObjectManipulation::set_coordinates_type(ECoordinatesType type)
{
    /*if (wxGetApp().get_mode() == comSimple)
        type = ECoordinatesType::World;*/

    if (m_coordinates_type == type) return;

    m_coordinates_type = type;
    //m_word_local_combo->SetSelection((int) m_coordinates_type);
    this->UpdateAndShow(true);
    GLCanvas3D *canvas = wxGetApp().plater()->canvas3D();
    canvas->get_gizmos_manager().update_data();
    canvas->set_as_dirty();
    canvas->request_extra_frame();

}

static const char* label_values[3][3] = {
{ "##position_x", "##position_y", "##position_z"},
{ "##rotation_x", "##rotation_y", "##rotation_z"},
{ "##absolute_rotation_x", "##absolute_rotation_y", "##absolute_rotation_z"}
};

static const char* label_scale_values[2][3] = {
{ "##scale_x", "##scale_y", "##scale_z"},
{ "##size_x", "##size_y", "##size_z"}
};

bool GizmoObjectManipulation::reset_button(ImGuiWrapper *imgui_wrapper, bool enabled)
{
    imgui_wrapper->disabled_begin(!enabled);
    bool        pressed   = false;
    ImTextureID normal_id = m_glcanvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_RESET);
    ImTextureID hover_id  = m_glcanvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_RESET_HOVER);

    float  scale       = m_glcanvas.get_scale();
    #ifdef WIN32
        int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
        scale *= (float) dpi / (float) DPI_DEFAULT;
    #endif // WIN32
    ImVec2 button_size = ImVec2(16 * scale, 16 * scale); // ORCA: Use exact resolution will prevent blur on icon

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    pressed = ImGui::ImageButton3(normal_id, hover_id, button_size, {0,0}, {1,1}, -1, {0,0,0,0}, {1,1,1, enabled ? 1.f : 0.f});  // ORCA make icon invisible to prevent changes on layout

    ImGui::PopStyleVar(1);

    imgui_wrapper->disabled_end();
    return pressed;
}

bool GizmoObjectManipulation::reset_zero_button(ImGuiWrapper *imgui_wrapper,  bool enabled)
{
    imgui_wrapper->disabled_begin(!enabled);
    bool        pressed   = false;
    ImTextureID normal_id = m_glcanvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_RESET_ZERO);
    ImTextureID hover_id  = m_glcanvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_RESET_ZERO_HOVER);

    float  scale       = m_glcanvas.get_scale();
    #ifdef WIN32
        int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
        scale *= (float) dpi / (float) DPI_DEFAULT;
    #endif // WIN32
    ImVec2 button_size = ImVec2(16 * scale, 16 * scale); // ORCA: Use exact resolution will prevent blur on icon

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    pressed = ImGui::ImageButton3(normal_id, hover_id, button_size, {0,0}, {1,1}, -1, {0,0,0,0}, {1,1,1, enabled ? 1.f : 0.f});  // ORCA make icon invisible to prevent changes on layout

    ImGui::PopStyleVar(1);

    imgui_wrapper->disabled_end();
    return pressed;
}

 float GizmoObjectManipulation::max_unit_size(int number, Vec3d &vec1, Vec3d &vec2,std::string str)
 {
     if (number <= 1) return -1;
     Vec3d vec[2] = {vec1, vec2};
     float nuit_max[4] = {0};
     float vec_max = 0, unit_size = 0;

     for (int i = 0; i < number; i++)
     {
         char buf[3][64] = {0};
         float buf_size[3] = {0};
         for (int j = 0; j < 3; j++) {
             ImGui::DataTypeFormatString(buf[j], IM_ARRAYSIZE(buf[j]), ImGuiDataType_Double, (void *) &vec[i][j], "%.2f");
             buf_size[j]  = ImGui::CalcTextSize(buf[j]).x;
             vec_max = std::max(buf_size[j], vec_max);
             nuit_max[i]  = vec_max;
         }
         unit_size = std::max(nuit_max[i], unit_size);
     }

     return unit_size + 8.0;
 }

 bool GizmoObjectManipulation::bbl_checkbox(const wxString &label, bool &value)
{
     bool result;
     bool b_value = value;
     if (b_value) {
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
     }
     auto label_utf8 = into_u8(label);
     result          = ImGui::BBLCheckbox(label_utf8.c_str(), &value);

     if (b_value) { ImGui::PopStyleColor(3); }
     return result;
}

void GizmoObjectManipulation::set_init_rotation(const Geometry::Transformation &value) {
    m_init_rotation_scale_tran = value.get_matrix_no_offset();
    m_init_rotation      = value.get_rotation();
}

void GizmoObjectManipulation::do_render_move_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    // BBS: GUI refactor: move gizmo to the right
    if (abs(last_move_input_window_width) > 0.01f) {
        if (x + last_move_input_window_width > m_glcanvas.get_canvas_size().get_width()) {
            if (last_move_input_window_width > m_glcanvas.get_canvas_size().get_width())
                x = 0;
            else
                x = m_glcanvas.get_canvas_size().get_width() - last_move_input_window_width;
        }
    }
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    // BBS
    ImGuiWrapper::push_toolbar_style(m_glcanvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0));

    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    float space_size    = imgui_wrapper->get_style_scaling() * 8;
    //ORCA
    float coord_combo_width = std::max({
        imgui_wrapper->calc_text_size(_L("World")).x,
        imgui_wrapper->calc_text_size(_L("Object")).x,
        imgui_wrapper->calc_text_size(_L("Part")).x
    }) + imgui_wrapper->calc_text_size("xxx"sv).x + imgui_wrapper->scaled(3.5f);
    float label_max = std::max({
        imgui_wrapper->calc_text_size(_L("Position")).x,
        imgui_wrapper->calc_text_size(_L("Relative")).x
    });
    float caption_max = std::max(label_max, coord_combo_width - 3 * space_size);
    float end_text_size = imgui_wrapper->calc_text_size(this->m_new_unit_string).x;

    // position
    Vec3d original_position;
    if (this->m_imperial_units)
        original_position = this->m_new_position * this->mm_to_in;
    else
        original_position = this->m_new_position;
    Vec3d display_position = m_buffered_position;

    // Rotation
    float unit_size = imgui_wrapper->calc_text_size(MAX_SIZE).x + space_size;
    int   index      = 1;
    int   index_unit = 1;

    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();

    Selection &              selection = m_glcanvas.get_selection();
    std::vector<std::string> modes     = {_u8L("World"), _u8L("Object")};//_u8L("Part") // ORCA use shorter terms to make UI more compact
    if (selection.is_multiple_full_object() || selection.is_wipe_tower()) {
        modes.pop_back();
    }
    size_t selection_idx = (int) m_coordinates_type;
    if (selection_idx >= modes.size()) {
        set_coordinates_type(ECoordinatesType::World);
        selection_idx = 0;
    }

    ImGuiWrapper::push_combo_style(m_glcanvas.get_scale());
    bool combox_changed = false;
    if (render_combo(imgui_wrapper, "", modes, selection_idx, 0, coord_combo_width)) {
        combox_changed = true;
    }
    if (ImGui::IsItemHovered()) {
        auto tooltip_str = _L("Coordinate system used for transform actions.");
        imgui_wrapper->tooltip(tooltip_str, imgui_wrapper->calc_text_size(tooltip_str).x + 3 * space_size);
    }
    ImGuiWrapper::pop_combo_style();

    // ORCA use TextColored to match axes color
    float offset_to_center = (unit_size - ImGui::CalcTextSize("O").x) / 2;
    ImGui::SameLine(caption_max + index * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::X()),"X");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::Y()),"Y");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::Z()),"Z");

    index      = 1;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    if (selection.is_single_full_instance() && is_instance_coordinates()) {
        imgui_wrapper->text(_L("Relative")); // ORCA
    }
    else {
        imgui_wrapper->text(_L("Position"));
    }

    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[0][0], &display_position[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[0][1], &display_position[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[0][2], &display_position[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui_wrapper->text(this->m_new_unit_string);
    bool is_avoid_one_update{false};
    if (combox_changed) {
        combox_changed = false;
        set_coordinates_type((ECoordinatesType) selection_idx);
        UpdateAndShow(true);
        is_avoid_one_update = true; // avoid update(current_active_id, "position", original_position
    }

    if (!is_avoid_one_update) {
        for (int i = 0; i < display_position.size(); i++) {
            if (display_position[i] > MAX_NUM) display_position[i] = MAX_NUM;
            if (display_position[i] < -MAX_NUM) display_position[i] = -MAX_NUM;
        }
        m_buffered_position = display_position;
        update(current_active_id, "position", original_position, m_buffered_position);
    }
    // the init position values are not zero, won't add reset button

    // send focus to m_glcanvas
    bool focued_on_text = false;
    for (int j = 0; j < 3; j++) {
        unsigned int id = ImGui::GetID(label_values[0][j]);
        if (current_active_id == id) {
            m_glcanvas.handle_sidebar_focus_event(label_values[0][j] + 2, true);
            focued_on_text = true;
            break;
        }
    }
    if (!focued_on_text) m_glcanvas.handle_sidebar_focus_event("", false);

    do_render_clipboard_window(imgui_wrapper, {"position"});

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    float f_scale = m_glcanvas.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    GLGizmoUtils::render_tooltip_button(imgui_wrapper, m_glcanvas, m_shortcuts_move, x, y);

    ImGui::SameLine();
    GLGizmoUtils::begin_right_aligned_buttons({ _L("Done") });
    if (imgui_wrapper->button(_L("Done"))) {
        m_glcanvas.reset_all_gizmos();
    }

    m_last_active_item = current_active_id;
    last_move_input_window_width = ImGui::GetWindowWidth();
    imgui_wrapper->end();
    ImGui::PopStyleVar(2);
    ImGuiWrapper::pop_toolbar_style();
}

void GizmoObjectManipulation::do_render_rotate_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    // BBS: GUI refactor: move gizmo to the right
    if (abs(last_rotate_input_window_width) > 0.01f) {
        if (x + last_rotate_input_window_width > m_glcanvas.get_canvas_size().get_width()) {
            if (last_rotate_input_window_width > m_glcanvas.get_canvas_size().get_width())
                x = 0;
            else
                x = m_glcanvas.get_canvas_size().get_width() - last_rotate_input_window_width;
        }
    }
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    // BBS
    ImGuiWrapper::push_toolbar_style(m_glcanvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0));

    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    float space_size    = imgui_wrapper->get_style_scaling() * 8;
    // ORCA
    float caption_max = std::max({
        imgui_wrapper->calc_text_size(_L("Relative")).x,
        imgui_wrapper->calc_text_size(_L("Absolute")).x,
        imgui_wrapper->calc_text_size(_L("World")).x
        //imgui_wrapper->calc_text_size(_L("Object")).x,
        //imgui_wrapper->calc_text_size(_L("Part")).x
    }) + 3.f * space_size;
    float end_text_size = ImGui::CalcTextSize("°").x; // ORCA rotate gizmo not uses mm or inch

    // position
    Vec3d original_position;
    if (this->m_imperial_units)
        original_position = this->m_new_position * this->mm_to_in;
    else
        original_position = this->m_new_position;
    Vec3d display_position = m_buffered_position;
    // Rotation
    Vec3d rotation   = this->m_buffered_rotation;
    Vec3d absolute_rotation = this->m_buffered_absolute_rotation;
    float unit_size = imgui_wrapper->calc_text_size(MAX_SIZE).x + space_size;
    int   index      = 1;
    int   index_unit = 1;

    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();
    ImGui::PushItemWidth(caption_max);
    imgui_wrapper->text(_L("World")); // ORCA
    if (ImGui::IsItemHovered()) {
        auto tooltip_str = _L("Coordinate system used for transform actions.");
        imgui_wrapper->tooltip(tooltip_str, imgui_wrapper->calc_text_size(tooltip_str).x + 3 * space_size);
    }
    // ORCA use TextColored to match axes color
    float offset_to_center = (unit_size - ImGui::CalcTextSize("O").x) / 2;
    ImGui::SameLine(caption_max + index * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::X()),"X");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::Y()),"Y");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::Z()),"Z");

    index      = 1;
    index_unit = 1;

    // ImGui::PushItemWidth(unit_size * 2);
    bool is_relative_input = false;
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Relative")); // ORCA
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    if (ImGui::BBLInputDouble(label_values[1][0], &rotation[0], 0.0f, 0.0f, "%.2f")) {
        is_relative_input = true;
    }
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (ImGui::BBLInputDouble(label_values[1][1], &rotation[1], 0.0f, 0.0f, "%.2f")) {
        is_relative_input = true;
    }
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (ImGui::BBLInputDouble(label_values[1][2], &rotation[2], 0.0f, 0.0f, "%.2f")) {
        is_relative_input = true;
    }
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui_wrapper->text("°");
    m_buffered_rotation = rotation;
    if (is_relative_input) {
        m_last_rotate_type = RotateType::Relative;
    }
    if (m_last_rotate_type == RotateType::Relative) {
        bool is_valid = update(current_active_id, "rotation", this->m_new_rotation, m_buffered_rotation) >= 0;
        if (is_valid) {
            m_last_rotate_type = RotateType::None;
        }
    }

    ImGui::SameLine(caption_max + index_unit * unit_size + (++index) * space_size + end_text_size);
    if (reset_button(imgui_wrapper, m_show_clear_rotation)) // ORCA reserve icon space to prevent changes on layout
        reset_rotation_value(true);
    if (m_show_clear_rotation && ImGui::IsItemHovered()) {
        float tooltip_size = imgui_wrapper->calc_text_size(_L("Reset current rotation to the value when open the rotation tool.")).x + 3 * space_size;
        imgui_wrapper->tooltip(_u8L("Reset current rotation to the value when open the rotation tool."), tooltip_size);
    }

    // send focus to m_glcanvas
    bool focued_on_text = false;
    for (int j = 0; j < 3; j++) {
        unsigned int id = ImGui::GetID(label_values[1][j]);
        if (current_active_id == id) {
            m_glcanvas.handle_sidebar_focus_event(label_values[1][j] + 2, true);
            focued_on_text = true;
            break;
        }
    }

    index      = 1;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Absolute"));
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    bool is_absolute_input = false;
    if (ImGui::BBLInputDouble(label_values[2][0], &absolute_rotation[0], 0.0f, 0.0f, "%.2f")) {
        is_absolute_input = true;
    }
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (ImGui::BBLInputDouble(label_values[2][1], &absolute_rotation[1], 0.0f, 0.0f, "%.2f")) {
        is_absolute_input = true;
    }
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (ImGui::BBLInputDouble(label_values[2][2], &absolute_rotation[2], 0.0f, 0.0f, "%.2f")) {
        is_absolute_input = true;
    }
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui_wrapper->text("°");
    m_buffered_absolute_rotation = absolute_rotation;
    if (is_absolute_input) {
        m_last_rotate_type = RotateType::Absolute;
    }
    if (m_last_rotate_type == RotateType::Absolute) {
        bool is_valid = update(current_active_id, "absolute_rotation", this->m_new_absolute_rotation, m_buffered_absolute_rotation) >= 0;
        if (is_valid) {
            m_last_rotate_type = RotateType::None;
        }
    }

    ImGui::SameLine(caption_max + index_unit * unit_size + (++index) * space_size + end_text_size);
    if (reset_zero_button(imgui_wrapper, m_show_reset_0_rotation)) // ORCA reserve icon space to prevent changes on layout
        reset_rotation_value(false);
    if (m_show_reset_0_rotation && ImGui::IsItemHovered()) {
        float tooltip_size = imgui_wrapper->calc_text_size(_L("Reset current rotation to real zeros.")).x + 3 * space_size;
        imgui_wrapper->tooltip(_L("Reset current rotation to real zeros."), tooltip_size);
    }
    // send focus to m_glcanvas
    bool absolute_focued_on_text = false;
    for (int j = 0; j < 3; j++) {
        unsigned int id = ImGui::GetID(label_values[2][j]);
        if (current_active_id == id) {
            m_glcanvas.handle_sidebar_focus_event(label_values[2][j] + 2, true);
            absolute_focued_on_text = true;
            break;
        }
    }
    if (!focued_on_text  && !absolute_focued_on_text)
        m_glcanvas.handle_sidebar_focus_event("", false);

    do_render_clipboard_window(imgui_wrapper, {"rotation"});

    ImGui::Spacing(); // needed after Text
    ImGui::Separator();
    ImGui::Spacing();
    float f_scale = m_glcanvas.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    GLGizmoUtils::render_tooltip_button(imgui_wrapper, m_glcanvas, m_shortcuts_rotate, x, y);
    
    ImGui::SameLine();
    GLGizmoUtils::begin_right_aligned_buttons({ _L("Done") });
    if (imgui_wrapper->button(_L("Done"))) {
        m_glcanvas.reset_all_gizmos();
    }

    m_last_active_item = current_active_id;
    last_rotate_input_window_width = ImGui::GetWindowWidth();
    imgui_wrapper->end();

    // BBS
    ImGui::PopStyleVar(2);
    ImGuiWrapper::pop_toolbar_style();
}

void GizmoObjectManipulation::do_render_scale_input_window(ImGuiWrapper* imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    //BBS: GUI refactor: move gizmo to the right
    if (abs(last_scale_input_window_width) > 0.01f) {
        if (x + last_scale_input_window_width > m_glcanvas.get_canvas_size().get_width()) {
            if (last_scale_input_window_width > m_glcanvas.get_canvas_size().get_width())
                x = 0;
            else
                x = m_glcanvas.get_canvas_size().get_width() - last_scale_input_window_width;
        }
    }
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    //BBS
    ImGuiWrapper::push_toolbar_style(m_glcanvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0));

    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value)->int {
        for (int i = 0; i < 3; i++)
        {
            if (original_value[i] != new_value[i])
            {
                if (active_id != m_last_active_item)
                {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    float space_size = imgui_wrapper->get_style_scaling() * 8;
    // ORCA
    float coord_combo_width = std::max({
        imgui_wrapper->calc_text_size(_L("World")).x,
        imgui_wrapper->calc_text_size(_L("Object")).x,
        imgui_wrapper->calc_text_size(_L("Part")).x
    }) + imgui_wrapper->calc_text_size("xxx"sv).x + imgui_wrapper->scaled(3.5f);
    float label_max = std::max({
        imgui_wrapper->calc_text_size(_L_CONTEXT("Scale", "Noun")).x,
        imgui_wrapper->calc_text_size(_L("Size")).x
    });
    float caption_max = std::max(label_max, coord_combo_width - 3 * space_size);
    float end_text_size = imgui_wrapper->calc_text_size(this->m_new_unit_string).x;
    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();

    Vec3d scale = m_buffered_scale;
    Vec3d display_size = m_buffered_size;

    Vec3d display_position = m_buffered_position;

    float unit_size = imgui_wrapper->calc_text_size(MAX_SIZE).x + space_size;
    bool imperial_units = this->m_imperial_units;

    int index      = 2;
    int index_unit = 1;

    Selection &              selection = m_glcanvas.get_selection();
    std::vector<std::string> modes     = {_u8L("World"), _u8L("Object"), _u8L("Part")}; // ORCA use shorter terms to make UI more compact
    if (selection.is_single_full_object()) { modes.pop_back(); }
    if (selection.is_multiple_full_object()) {
        modes.pop_back();
        modes.pop_back();
    }
    size_t selection_idx = (int) m_coordinates_type;
    if (selection_idx >= modes.size()) {
        set_coordinates_type(ECoordinatesType::World);
        selection_idx = 0;
    }

    ImGuiWrapper::push_combo_style(m_glcanvas.get_scale());
    bool combox_changed = false;
    if (render_combo(imgui_wrapper, "", modes, selection_idx, 0, coord_combo_width)) {
        combox_changed = true;
    }
    if (ImGui::IsItemHovered()) {
        auto tooltip_str = _L("Coordinate system used for transform actions.");
        imgui_wrapper->tooltip(tooltip_str, imgui_wrapper->calc_text_size(tooltip_str).x + 3 * space_size);
    }
    ImGuiWrapper::pop_combo_style();

    // ORCA use TextColored to match axes color
    float offset_to_center = (unit_size - ImGui::CalcTextSize("O").x) / 2;
    ImGui::SameLine(caption_max + space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::X()),"X");
    ImGui::SameLine(caption_max + unit_size + index * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::Y()),"Y");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size + offset_to_center);
    ImGui::TextColored(ImGuiWrapper::to_ImVec4(ColorRGBA::Z()),"Z");

    index      = 2;
    index_unit = 1;

    //ImGui::PushItemWidth(unit_size * 2);
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L_CONTEXT("Scale", "Noun"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[0][0], &scale[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[0][1], &scale[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[0][2], &scale[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    imgui_wrapper->text("%");
    if (scale.x() > 0 && scale.y() > 0 && scale.z() > 0) {
        m_buffered_scale = scale;
    }

    ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size + end_text_size);
    if (reset_button(imgui_wrapper, m_show_clear_scale))
        reset_scale_value();

    //Size
    Vec3d original_size;
    if (this->m_imperial_units)
        original_size = this->m_new_size * this->mm_to_in;
    else
        original_size = this->m_new_size;

    index              = 2;
    index_unit         = 1;
    //ImGui::PushItemWidth(unit_size * 2);
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Size"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[1][0], &display_size[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[1][1], &display_size[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[1][2], &display_size[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    imgui_wrapper->text(this->m_new_unit_string);
    for (int i = 0; i < display_size.size(); i++) {
        if (std::abs(display_size[i]) > MAX_NUM) {
            display_size[i] = MAX_NUM;
        }
    }
    if (display_size.x() > 0 && display_size.y() > 0 && display_size.z() > 0) {
        m_buffered_size = display_size;
    }

    ImGui::AlignTextToFramePadding();
    bool is_avoid_one_update{false};
    if (combox_changed) {
        combox_changed = false;
        set_coordinates_type((ECoordinatesType) selection_idx);
        UpdateAndShow(true);
        is_avoid_one_update = true;
    }

    auto uniform_scale_size =imgui_wrapper->calc_text_size(_L("Uniform scale")).x;
    ImGui::PushItemWidth(uniform_scale_size);
    int size_sel{-1};
    if (!is_avoid_one_update) {
        size_sel    = update(current_active_id, "size", original_size, m_buffered_size);
    }
    ImGui::PopStyleVar(1);
    bool uniform_scale = this->m_uniform_scale;

    // BBS: when select multiple objects, uniform scale can be deselected
    //const Selection &selection = m_glcanvas.get_selection();
    //bool uniform_scale_only    = selection.is_multiple_full_object() || selection.is_multiple_full_instance() || selection.is_mixed() || selection.is_multiple_volume() ||
    //                          selection.is_multiple_modifier();

    //if (uniform_scale_only) {
    //    imgui_wrapper->disabled_begin(true);
    //    imgui_wrapper->bbl_checkbox(_L("Uniform scale"), uniform_scale_only);
    //    imgui_wrapper->disabled_end();
    //} else {
        imgui_wrapper->bbl_checkbox(_L("Uniform scale"), uniform_scale);
    //}
    if (uniform_scale != this->m_uniform_scale) { this->set_uniform_scaling(uniform_scale); }

     // for (int index = 0; index < 3; index++)
    //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",before_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") %
    //    this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
    int scale_sel = update(current_active_id, "scale", this->m_new_scale, m_buffered_scale);
    if ((scale_sel >= 0)) {
        // for (int index = 0; index < 3; index++)
        //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",after_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") %
        //    this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
        for (int i = 0; i < 3; ++i) {
            if (i != scale_sel) ImGui::ClearInputTextInitialData(label_scale_values[0][i], m_buffered_scale[i]);
            ImGui::ClearInputTextInitialData(label_scale_values[1][i], m_buffered_size[i]);
        }
    }

    if ((size_sel >= 0)) {
        // for (int index = 0; index < 3; index++)
        //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",after_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") %
        //    this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
        for (int i = 0; i < 3; ++i) {
            ImGui::ClearInputTextInitialData(label_scale_values[0][i], m_buffered_scale[i]);
            if (i != size_sel) ImGui::ClearInputTextInitialData(label_scale_values[1][i], m_buffered_size[i]);
        }
    }

    //send focus to m_glcanvas
    bool focued_on_text = false;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
        {
            unsigned int id = ImGui::GetID(label_scale_values[i][j]);
            if (current_active_id == id)
            {
                m_glcanvas.handle_sidebar_focus_event(label_scale_values[i][j] + 2, true);
                focued_on_text = true;
                break;
            }
        }
    if (!focued_on_text)
        m_glcanvas.handle_sidebar_focus_event("", false);
    
    do_render_clipboard_window(imgui_wrapper, {"scale", "size"});

    ImGui::Separator();
    float f_scale = m_glcanvas.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    GLGizmoUtils::render_tooltip_button(imgui_wrapper, m_glcanvas, m_shortcuts_scale, x, y);

    ImGui::SameLine();
    GLGizmoUtils::begin_right_aligned_buttons({ _L("Done") });
    if (imgui_wrapper->button(_L("Done"))) {
        m_glcanvas.reset_all_gizmos();
    }

    m_last_active_item = current_active_id;

    last_scale_input_window_width = ImGui::GetWindowWidth();
    imgui_wrapper->end();

    //BBS
    ImGui::PopStyleVar(1);
    ImGuiWrapper::pop_toolbar_style();
}

// ---------------------------------------------------------------------------
// Transform clipboard
// ---------------------------------------------------------------------------

std::optional<Geometry::Transformation> GizmoObjectManipulation::get_selection_world_transformation() const
{
    const Selection &selection = m_glcanvas.get_selection();
    const GLVolume *v = selection.get_first_volume();
    if (v == nullptr)
        return std::nullopt;

    Geometry::Transformation world;
    if (selection.is_single_volume_or_modifier()) {
        // World transform of a part: instance transform composed with the volume transform.
        world.set_matrix(v->get_instance_transformation().get_matrix() * v->get_volume_transformation().get_matrix());
    } else if (selection.is_single_full_instance() || selection.is_single_full_object()) {
        world = v->get_instance_transformation();
    } else {
        return std::nullopt;
    }
    return world;
}

bool GizmoObjectManipulation::clipboard_slot_filled(const std::string &slot) const
{
    const TransformClipboard &c = m_transform_clipboard;
    if (slot == "all")      return !c.empty();
    if (slot == "position") return c.position.has_value();
    if (slot == "rotation") return c.rotation.has_value();
    if (slot == "scale")    return c.scale_factors.has_value();
    if (slot == "size")     return c.size.has_value();
    if (slot == "mirror")   return c.mirror.has_value();
    return false;
}

std::string GizmoObjectManipulation::source_label_for(int obj_idx, int vol_idx, bool from_part) const
{
    const Model &model = wxGetApp().plater()->model();
    if (obj_idx < 0 || obj_idx >= (int) model.objects.size())
        return from_part ? _u8L("Part") : _u8L("Instance");
    const ModelObject *obj = model.objects[obj_idx];
    if (from_part && vol_idx >= 0 && vol_idx < (int) obj->volumes.size() && !obj->volumes[vol_idx]->name.empty())
        return obj->volumes[vol_idx]->name;
    if (!obj->name.empty())
        return obj->name;
    return from_part ? _u8L("Part") : _u8L("Instance");
}

void GizmoObjectManipulation::fill_clipboard_from_world(const Geometry::Transformation &world, const std::string &label)
{
    // Source the components from the decomposed world matrix, never from the
    // displayed Euler values: Orca zeroes the sidebar rotation after Place on
    // Face and similar operations, so the display is not the true orientation.
    // Size is a mesh property, not a transform component, so it is filled only by
    // an explicit Size-row copy, never here.
    TransformClipboard &c = m_transform_clipboard;
    c.position      = world.get_offset();
    c.rotation      = world.get_rotation();
    c.scale_factors = world.get_scaling_factor();
    c.mirror        = world.get_mirror();
    c.source_matrix = world.get_matrix();
    c.source_label  = label;
}

void GizmoObjectManipulation::clipboard_copy(const std::string &slot)
{
    std::optional<Geometry::Transformation> world_opt = get_selection_world_transformation();
    if (!world_opt)
        return;
    const Geometry::Transformation &world     = *world_opt;
    const Selection                &selection = m_glcanvas.get_selection();
    const bool                      is_part   = selection.is_single_volume_or_modifier();
    const GLVolume                 *fv        = selection.get_first_volume();
    const std::string               label     = source_label_for(fv ? fv->object_idx() : -1,
                                                                  fv ? fv->volume_idx() : -1, is_part);

    if (slot == "all") {
        fill_clipboard_from_world(world, label);
        return;
    }

    TransformClipboard &c = m_transform_clipboard;
    if (slot == "position") c.position      = world.get_offset();
    if (slot == "rotation") c.rotation      = world.get_rotation();
    if (slot == "scale")    c.scale_factors = world.get_scaling_factor();
    if (slot == "mirror")   c.mirror        = world.get_mirror();
    if (slot == "size")     c.size          = m_cache.is_valid() ? m_cache.size : Vec3d::Zero();

    // Always refresh the raw source matrix so the shear check has a reference.
    c.source_matrix = world.get_matrix();
    c.source_label  = label;
}

std::string GizmoObjectManipulation::eyedropper_reason(const GLVolume *donor) const
{
    if (donor == nullptr)
        return std::string();
    // "Copy from" only loads the clipboard, so any object is a valid donor.
    if (m_tc_eyedropper_copy_only)
        return std::string();
    // "Apply" needs a single instance/part selected as the paste target...
    const Selection &selection = m_glcanvas.get_selection();
    if (!selection.is_single_full_instance() && !selection.is_single_volume_or_modifier())
        return _u8L("Select one object as the target first");
    // ...and picking the target's own object would be a no-op.
    const GLVolume *tv = selection.get_first_volume();
    if (tv != nullptr && tv->object_idx() == donor->object_idx() && tv->instance_idx() == donor->instance_idx())
        return _u8L("This is the target object");
    return std::string();
}

int GizmoObjectManipulation::eyedropper_validity(const GLVolume *donor) const
{
    if (donor == nullptr)
        return 0; // none
    return eyedropper_reason(donor).empty() ? 1 : 2;
}

void GizmoObjectManipulation::eyedropper_commit(const GLVolume *donor, bool alt_pick_part)
{
    if (donor == nullptr || eyedropper_validity(donor) != 1)
        return;

    // Match the donor level to what the pick should be compatible with. In apply
    // mode that is the paste target: a whole-object (instance) target copies the
    // donor's whole object; a part target copies the part under the cursor. "Copy
    // from" has no target, so it defaults to the whole object, with Alt to grab
    // the part instead. The stored values are world space, so a part transform
    // still applies cleanly to any target.
    bool as_part;
    if (m_tc_eyedropper_copy_only)
        as_part = alt_pick_part;
    else
        as_part = m_glcanvas.get_selection().is_single_volume_or_modifier();

    Geometry::Transformation donor_world;
    if (as_part)
        donor_world.set_matrix(donor->get_instance_transformation().get_matrix() *
                               donor->get_volume_transformation().get_matrix());
    else
        donor_world = donor->get_instance_transformation();

    fill_clipboard_from_world(donor_world, source_label_for(donor->object_idx(), as_part ? donor->volume_idx() : -1, as_part));

    if (!m_tc_eyedropper_copy_only)
        // Apply the whole transform to the current selection, filtered by the axis
        // chips: a fresh eyedropper with all axes enabled reads as "make this like that".
        clipboard_paste("all");
}

void GizmoObjectManipulation::eyedropper_commit_from(int obj_idx, int inst_idx, int vol_idx, bool is_part)
{
    const Model &model = wxGetApp().plater()->model();
    if (obj_idx < 0 || obj_idx >= (int) model.objects.size())
        return;
    const ModelObject *obj = model.objects[obj_idx];
    if (obj->instances.empty())
        return;
    if (inst_idx < 0 || inst_idx >= (int) obj->instances.size())
        inst_idx = 0;

    // Apply mode needs a single instance/part target, and not the donor itself.
    if (!m_tc_eyedropper_copy_only) {
        const Selection &sel = m_glcanvas.get_selection();
        if (!sel.is_single_full_instance() && !sel.is_single_volume_or_modifier())
            return;
        const GLVolume *tv = sel.get_first_volume();
        if (tv != nullptr && tv->object_idx() == obj_idx && tv->instance_idx() == inst_idx)
            return;
    }

    // A part item copies the composed world transform (instance * volume); an
    // instance/object item copies the instance transform.
    const Geometry::Transformation inst_tr = obj->instances[inst_idx]->get_transformation();
    Geometry::Transformation       donor_world;
    if (is_part && vol_idx >= 0 && vol_idx < (int) obj->volumes.size())
        donor_world.set_matrix(inst_tr.get_matrix() * obj->volumes[vol_idx]->get_transformation().get_matrix());
    else
        donor_world = inst_tr;

    fill_clipboard_from_world(donor_world, source_label_for(obj_idx, vol_idx, is_part));
    if (!m_tc_eyedropper_copy_only)
        clipboard_paste("all");
}

void GizmoObjectManipulation::clipboard_paste(const std::string &slot)
{
    if (!m_cache.is_valid())
        return;
    const TransformClipboard &c   = m_transform_clipboard;
    const bool                all = (slot == "all");

    const bool do_scale    = (all || slot == "scale")    && c.scale_factors.has_value();
    const bool do_size     = (slot == "size")            && c.size.has_value();
    const bool do_rotation = (all || slot == "rotation") && c.rotation.has_value();
    const bool do_position = (all || slot == "position") && c.position.has_value();
    const bool do_mirror   = (all || slot == "mirror")   && c.mirror.has_value();
    if (!do_scale && !do_size && !do_rotation && !do_position && !do_mirror)
        return;

    wxWindow *parent = wxGetApp().plater();

    // Shear warning: a rotation-only paste cannot cleanly separate rotation from
    // scale when the source matrix carries shear, so it can distort the target. A
    // whole-transform paste replaces the scale too, so there is nothing to shear:
    // gate this to the rotation-only path.
    if (do_rotation && !do_scale && !all && c.source_matrix && !m_tc_skip_shear_warning &&
        Geometry::Transformation(*c.source_matrix).has_skew()) {
        RichMessageDialog dlg(parent,
            _L("The copied object has a sheared transform, so its rotation and scale cannot be cleanly separated. Pasting rotation on its own may distort this object. Paste anyway?"),
            _L("Paste rotation"), wxICON_WARNING | wxYES_NO | wxNO_DEFAULT);
        dlg.ShowCheckBox(_L("Do not ask again this session"));
        const int res = dlg.ShowModal();
        if (dlg.IsCheckBoxChecked())
            m_tc_skip_shear_warning = true;
        if (res != wxID_YES)
            return;
    }

    // Uniform-lock conflict: the held scale is non-uniform but the uniform scale
    // lock is on, so only one axis can be honoured. Let the user choose which wins.
    bool paste_all_scale_axes = true;
    if (do_scale && m_uniform_scale) {
        const Vec3d s = *c.scale_factors;
        const bool non_uniform = std::abs(s.x() - s.y()) > 1e-6 || std::abs(s.x() - s.z()) > 1e-6;
        if (non_uniform) {
            if (!m_tc_skip_uniform_warning) {
                RichMessageDialog dlg(parent,
                    wxString::Format(_L("The held scale is non-uniform (%.1f%%, %.1f%%, %.1f%%) but the uniform scale lock is on.\n\nYes: turn the lock off and paste all three axes.\nNo: keep the lock and paste uniformly from the X axis."),
                        s.x() * 100.0, s.y() * 100.0, s.z() * 100.0),
                    _L("Paste scale"), wxICON_QUESTION | wxYES_NO | wxCANCEL | wxYES_DEFAULT);
                dlg.ShowCheckBox(_L("Do not ask again this session"));
                const int res = dlg.ShowModal();
                if (dlg.IsCheckBoxChecked())
                    m_tc_skip_uniform_warning = true;
                if (res == wxID_CANCEL)
                    return;
                paste_all_scale_axes = (res == wxID_YES);
            } else {
                // Session default when the prompt is suppressed: keep the lock.
                paste_all_scale_axes = false;
            }
            if (paste_all_scale_axes)
                set_uniform_scaling(false); // lock off -> all three axes below
        }
    }

    // One undo snapshot for the whole paste, named for the action.
    Plater::TakeSnapshot snapshot(wxGetApp().plater(),
        all ? _u8L("Paste Transform") : _u8L("Paste transform component"));

    // Order: scale/mirror, then rotation, then position, so later components are
    // not disturbed by earlier ones. Rotation routes through "absolute_rotation"
    // so it reproduces the source world orientation even after Place on Face
    // zeroed the relative display.

    // Scale (magnitudes) and mirror (signs) are applied directly on the target's
    // transformation, so they are correct in every coordinate view (the scale
    // field is space-dependent, so routing through the setter would misread the
    // stored world magnitudes). set_scaling_factor is shear-free but can drop the
    // existing mirror, so mirror is always re-applied afterwards: with the held
    // signs when pasting mirror, otherwise with the target's own signs to preserve
    // them. Note: for a part whose parent instance is itself scaled/mirrored, this
    // sets the part-local component, not the composed world value.
    if (do_scale || do_mirror) {
        Selection &selection = m_glcanvas.get_selection();
        selection.setup_cache();

        auto edit = [&](Geometry::Transformation &t) {
            const Vec3d preexisting_mirror = t.get_mirror();
            if (do_scale) {
                Vec3d s = t.get_scaling_factor(); // positive magnitudes
                if (m_uniform_scale && !paste_all_scale_axes) {
                    for (int a = 0; a < 3; ++a)
                        if (c.axes_for("scale")[a]) { s = Vec3d::Constant((*c.scale_factors)[a]); break; }
                } else {
                    for (int a = 0; a < 3; ++a)
                        if (c.axes_for("scale")[a]) s[a] = (*c.scale_factors)[a];
                }
                t.set_scaling_factor(s);
            }
            Vec3d m = preexisting_mirror;
            if (do_mirror)
                for (int a = 0; a < 3; ++a)
                    if (c.axes_for("mirror")[a]) m[a] = (*c.mirror)[a] < 0.0 ? -1.0 : 1.0;
            t.set_mirror(m);
        };

        if (selection.is_single_volume_or_modifier()) {
            GLVolume *v = const_cast<GLVolume *>(selection.get_first_volume());
            Geometry::Transformation t = v->get_volume_transformation();
            edit(t);
            v->set_volume_transformation(t);
        } else if (selection.is_single_full_instance()) {
            Geometry::Transformation t = selection.get_first_volume()->get_instance_transformation();
            edit(t);
            for (unsigned int idx : selection.get_volume_idxs())
                const_cast<GLVolume *>(selection.get_volume(idx))->set_instance_transformation(t);
        }
        m_glcanvas.do_scale(""); // empty name: no inner snapshot, coalesced under the outer one
    }
    if (do_size)
        for (int a = 0; a < 3; ++a)
            if (c.axes_for("size")[a]) on_change("size", a, (*c.size)[a]);
    if (do_rotation)
        for (int a = 0; a < 3; ++a)
            if (c.axes_for("rotation")[a]) on_change("absolute_rotation", a, (*c.rotation)[a] * (180.0 / M_PI));

    if (do_position) {
        // Position pastes in world space regardless of the panel's World/Object/
        // Part selector, so "take the source's place" is correct in every view.
        // Done as a direct world-relative translate because the position field is
        // not world in Object/Part space (it reads zero there), so routing it
        // through the setter would misinterpret the stored world value.
        if (const std::optional<Geometry::Transformation> w = get_selection_world_transformation()) {
            const Vec3d cur = w->get_offset();
            Vec3d delta = Vec3d::Zero();
            for (int a = 0; a < 3; ++a)
                if (c.axes_for("position")[a])
                    delta[a] = (*c.position)[a] - cur[a];
            if (delta.cwiseAbs().maxCoeff() > EPSILON) {
                Selection &selection = m_glcanvas.get_selection();
                selection.setup_cache();
                TransformationType trafo_type;
                trafo_type.set_relative(); // world-relative translate
                selection.translate(delta, trafo_type);
                m_glcanvas.do_move("");
            }
        }
    }

    UpdateAndShow(true);
    // Undo coalescing: position/scale/mirror use do_move/do_scale("") (no inner
    // snapshot); rotation routes through the setter whose internal snapshot is
    // absorbed by the outer Plater::TakeSnapshot (same pattern as reset_scale_value),
    // so a paste is a single undo entry. Remaining edge: a part whose parent
    // instance is scaled/mirrored pastes the part-local scale/mirror, not the
    // composed world value.
}

void GizmoObjectManipulation::do_render_clipboard_window(ImGuiWrapper *imgui_wrapper, std::initializer_list<const char *> surfaced_slots)
{
    TransformClipboard &c          = m_transform_clipboard;
    const float         space_size = imgui_wrapper->get_style_scaling() * 8;

    // Small icon button (copy/paste glyphs), matching the reset_button idiom.
    // The unique id disambiguates buttons that share the same texture.
    auto icon_button = [&](GLGizmosManager::MENU_ICON_NAME icon, bool enabled, const char *id) -> bool {
        ImTextureID tex = m_glcanvas.get_gizmos_manager().get_icon_texture_id(icon);
        float scale = m_glcanvas.get_scale();
#ifdef WIN32
        int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
        scale *= (float) dpi / (float) DPI_DEFAULT;
#endif // WIN32
        const ImVec2 button_size(16 * scale, 16 * scale);
        ImGui::PushID(id);
        imgui_wrapper->disabled_begin(!enabled);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        const bool pressed = ImGui::ImageButton3(tex, tex, button_size, {0, 0}, {1, 1}, -1, {0, 0, 0, 0},
                                                 {1, 1, 1, enabled ? 1.0f : 0.35f});
        ImGui::PopStyleVar(1);
        imgui_wrapper->disabled_end();
        ImGui::PopID();
        return pressed && enabled;
    };

    // A toggleable axis chip showing a held value; toggling drives that row's
    // per-axis paste filter (index a = X/Y/Z), independent per component.
    auto value_chip = [&](const char *slot, int a, double value) {
        std::array<bool, 3> &mask = c.axes_for(slot);
        const bool on = mask[a];
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
        }
        ImGui::PushID(a);
        char buf[32];
        ImGui::DataTypeFormatString(buf, IM_ARRAYSIZE(buf), ImGuiDataType_Double, (void *) &value, "%.2f");
        if (ImGui::Button(buf))
            mask[a] = !mask[a];
        ImGui::PopID();
        if (on)
            ImGui::PopStyleColor(3);
    };

    struct Row { wxString label; const char *slot; const std::optional<Vec3d> *val; double factor; };
    const Row rows[] = {
        { _L("Position"), "position", &c.position,      1.0 },
        { _L("Rotation"), "rotation", &c.rotation,      180.0 / M_PI },
        { _L("Scale %"),  "scale",    &c.scale_factors, 100.0 },
        { _L("Size mm"),  "size",     &c.size,          1.0 },
        { _L("Mirror"),   "mirror",   &c.mirror,        1.0 },
    };
    float label_max = 0.f;
    for (const auto &r : rows)
        label_max = std::max(label_max, imgui_wrapper->calc_text_size(r.label).x);

    auto find_row = [&](const std::string &slot) -> const Row * {
        for (const auto &r : rows)
            if (slot == r.slot) return &r;
        return nullptr;
    };

    // One clipboard row: label, copy, paste, then the held value inline as
    // toggleable X/Y/Z chips. scope disambiguates the ImGui ids when a row is
    // shown both surfaced (above) and inside the collapsed panel.
    auto render_row = [&](const Row &r, const char *scope) {
        ImGui::PushID(scope);
        ImGui::PushID(r.slot);
        ImGui::AlignTextToFramePadding();
        imgui_wrapper->text(r.label);
        ImGui::SameLine(label_max + 2 * space_size);
        if (icon_button(GLGizmosManager::IC_TOOLBAR_COPY, true, "tc_copy"))
            clipboard_copy(r.slot);
        ImGui::SameLine();
        if (icon_button(GLGizmosManager::IC_TOOLBAR_PASTE, clipboard_slot_filled(r.slot), "tc_paste"))
            clipboard_paste(r.slot);
        if (r.val->has_value()) {
            Vec3d shown = (**r.val) * r.factor;
            for (int a = 0; a < 3; ++a) {
                ImGui::SameLine();
                value_chip(r.slot, a, shown[a]);
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    };

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Rows relevant to the active tool, surfaced above the collapsed panel.
    for (const char *slot : surfaced_slots)
        if (const Row *r = find_row(slot))
            render_row(*r, "surf");

    // The full transform clipboard, tucked under a collapsed dropdown.
    if (ImGui::CollapsingHeader(into_u8(_L("Transform clipboard")).c_str())) {
        for (const auto &r : rows)
            render_row(r, "full");

        if (c.empty())
            imgui_wrapper->text(_L("Nothing copied yet."));
        else
            imgui_wrapper->text(into_u8(_L("Held from:")) + " " + c.source_label);

        // Whole-transform bulk copy/paste and the two eyedroppers.
        ImGui::AlignTextToFramePadding();
        imgui_wrapper->text(_L("Whole transform"));
        ImGui::SameLine();
        if (icon_button(GLGizmosManager::IC_TOOLBAR_COPY, true, "tc_copy_all"))
            clipboard_copy("all");
        if (ImGui::IsItemHovered())
            imgui_wrapper->tooltip(_L("Copy the whole transform"), ImGui::GetFontSize() * 15);
        ImGui::SameLine();
        if (icon_button(GLGizmosManager::IC_TOOLBAR_PASTE, clipboard_slot_filled("all"), "tc_paste_all"))
            clipboard_paste("all");
        if (ImGui::IsItemHovered() && clipboard_slot_filled("all"))
            imgui_wrapper->tooltip(_L("Paste the whole transform"), ImGui::GetFontSize() * 15);

        // Two eyedroppers: "Pick apply" picks a donor and makes the selection like
        // it; "Copy from" only loads the clipboard from the picked object. Esc, a
        // right-click, or a second press exits picking (handled in GLCanvas3D).
        const bool picking  = m_glcanvas.is_transform_picking();
        const bool apply_on = picking && !m_tc_eyedropper_copy_only;
        const bool copy_on  = picking &&  m_tc_eyedropper_copy_only;
        auto pick_button = [&](bool active, const std::string &text, const char *id, bool copy_only) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
            }
            std::string lbl = text;
            lbl.append("##").append(id);
            if (ImGui::Button(lbl.c_str())) {
                if (active) {
                    m_glcanvas.set_transform_picking(false);
                } else {
                    m_tc_eyedropper_copy_only = copy_only;
                    m_glcanvas.set_transform_picking(true);
                }
            }
            if (active)
                ImGui::PopStyleColor(3);
        };
        ImGui::SameLine();
        pick_button(apply_on, apply_on ? _u8L("Picking...") : _u8L("Pick apply"), "tc_pick_apply", false);
        if (ImGui::IsItemHovered())
            imgui_wrapper->tooltip(_L("Pick a donor object to copy its transform onto the selection"), ImGui::GetFontSize() * 20);
        ImGui::SameLine();
        pick_button(copy_on, copy_on ? _u8L("Picking...") : _u8L("Copy from"), "tc_pick_copy", true);
        if (ImGui::IsItemHovered())
            imgui_wrapper->tooltip(_L("Pick an object to copy its transform from into the clipboard"), ImGui::GetFontSize() * 20);
    }
}


} //namespace GUI
} //namespace Slic3r

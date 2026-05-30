#include "StackObjectsHandler.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "StackObjectsDialog.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace GUI {

void stack_objects(
    Plater*       plater,
    int           copies,
    int           separator_layers,
    int           gap_layers,
    float         separator_to_object_size_ratio,
    float         first_separator_size_ratio,
    bool          support_objects,
    bool          use_base_separator,
    SeparatorType separator_type)
{
    if (!plater->get_selection().is_single_full_object() &&
        !plater->get_selection().is_single_full_instance())
        return;

    int obj_idx = plater->get_selected_object_idx();
    if (obj_idx < 0 || obj_idx >= (int)plater->model().objects.size())
        return;

    ModelObject* src_obj = plater->model().objects[obj_idx];
    if (src_obj->instances.empty())
        return;

    Plater::TakeSnapshot snapshot(plater, std::string("Stack Objects"));

    const int inst_idx = (plater->get_selection().get_instance_idx() >= 0)
                         ? plater->get_selection().get_instance_idx() : 0;
    if (inst_idx >= (int)src_obj->instances.size())
        return;

    // ── Get object-space dimensions before adding any new volumes ─────
    BoundingBoxf3 src_local_bb;
    for (ModelVolume* mv : src_obj->volumes)
        src_local_bb.merge(mv->mesh().bounding_box());

    const double part_height     = src_local_bb.max.z() - src_local_bb.min.z();
    const double src_local_min_z = src_local_bb.min.z();

    const double footprint_x  = src_local_bb.max.x() - src_local_bb.min.x();
    const double footprint_y  = src_local_bb.max.y() - src_local_bb.min.y();
    const double max_footprint = std::max(footprint_x, footprint_y);

    // ── Config ────────────────────────────────────────────────────────
    const double layer_height =
        wxGetApp().preset_bundle->prints
             .get_edited_preset()
             .config.opt_float("layer_height");

    const double sep_thickness = layer_height * separator_layers;
    const double interface_gap = layer_height * gap_layers;

    // If a base separator is requested, the whole stack is lifted by
    // sep_thickness + interface_gap so the base disc fits below the
    // first object at Z=0 on the build plate.
    const double base_lift = use_base_separator
                             ? (sep_thickness + interface_gap)
                             : 0.0;

    // Single consistent step drives all Z placement:
    // [object] [gap] [disc] [gap] [next object] ...
    const double step = part_height + interface_gap + sep_thickness + interface_gap;

    // ── Snapshot original volumes ─────────────────────────────────────
    struct OriginalVolume {
        TriangleMesh    mesh;
        std::string     name;
        ModelVolumeType type;
        int             extruder;
    };

    std::vector<OriginalVolume> orig_vols;
    orig_vols.reserve(src_obj->volumes.size());
    for (ModelVolume* mv : src_obj->volumes) {
        OriginalVolume ov;
        ov.mesh     = mv->mesh();
        ov.name     = mv->name;
        ov.type     = mv->type();
        ov.extruder = mv->config.has("extruder") ? mv->config.extruder() : 0;
        orig_vols.push_back(std::move(ov));
    }

    const std::string base_name = src_obj->name;

    // ── Rename assembly with metadata ─────────────────────────────────
    src_obj->name = base_name + "_StackAssembly" +
                    "_c" + std::to_string(copies) +
                    "_l" + std::to_string(separator_layers) +
                    "_g" + std::to_string(gap_layers) +
                    "_h" + std::to_string((int)(part_height * 100.0)) +
                    "_s" + std::to_string((int)(sep_thickness * 100.0)) +
                    "_v" + std::to_string((int)orig_vols.size()) +
                    "_t" + std::to_string((int)separator_type);

    // ── Rename original volumes and shift them up by base_lift ────────
    for (ModelVolume* mv : src_obj->volumes) {
        mv->name = base_name + "_1_" + mv->name;
        if (base_lift > 0.0)
            mv->translate(0.0, 0.0, base_lift);
    }

    // ── Helper: create separator mesh based on type ───────────────────
    auto create_separator_mesh = [&](double size_ratio, double thickness) -> TriangleMesh {
        switch (separator_type) {
        case SeparatorType::BOUNDING_BOX: {
            const double width = footprint_x * size_ratio;
            const double depth = footprint_y * size_ratio;
            TriangleMesh box = make_cube(width, depth, thickness);
            box.translate(-(float)(width / 2.0),
                          -(float)(depth / 2.0),
                          0.0f);
            return box;
        }
        case SeparatorType::OBJECT_SILHOUETTE: {
            TriangleMesh combined;
            for (const OriginalVolume& ov : orig_vols) {
                if (ov.type == ModelVolumeType::MODEL_PART)
                    combined.merge(ov.mesh);
            }
            combined.scale(Vec3f((float)size_ratio, (float)size_ratio, 1.0f));
            BoundingBoxf3 mesh_bb = combined.bounding_box();
            const double z_scale  = thickness / (mesh_bb.max.z() - mesh_bb.min.z());
            combined.scale(Vec3f(1.0f, 1.0f, (float)z_scale));
            combined.translate(0.0f, 0.0f, (float)(-src_local_min_z));
            return combined;
        }
        default: //default to Disc
        {
            const double radius = 0.5 * size_ratio * max_footprint;
            return make_cylinder(radius, thickness, 2.0 * PI / 72.0);
            
        }
        }
    };

    // ── Helper: place disc + above enforcer at a given disc_bottom Z ──
    // The below-disc gap is left open for tree supports to generate naturally.
    // Only the above-disc gap gets an enforcer to force support interface there.
    auto place_separator = [&](double disc_bottom, double size_ratio,
                               const std::string& label) {
        const double disc_top = disc_bottom + sep_thickness;

        // Disc
        TriangleMesh disc_mesh = create_separator_mesh(size_ratio, sep_thickness);
        disc_mesh.translate(0.0f, 0.0f, (float)disc_bottom);
        ModelVolume* disc_vol = src_obj->add_volume(std::move(disc_mesh));
        disc_vol->name = base_name + "_Separator_" + label;
        disc_vol->set_type(ModelVolumeType::MODEL_PART);

        // Enforcer above disc: forces support interface in the breakaway gap
        // between the disc top and the next object bottom.
        TriangleMesh above_mesh = create_separator_mesh(
            size_ratio * 1.05, interface_gap);
        above_mesh.translate(0.0f, 0.0f, (float)disc_top);
        ModelVolume* above_vol = src_obj->add_volume(std::move(above_mesh));
        above_vol->name = base_name + "_Interface_" + label;
        above_vol->set_type(ModelVolumeType::SUPPORT_ENFORCER);
    };

    // ── Optional base separator on build plate ────────────────────────
    // Sits at Z=0, lifts the base object up by base_lift.
    // Uses first_separator_size_ratio for a wider, more stable footprint.
    if (use_base_separator) {
        // Disc bottom sits at src_local_min_z so it aligns with the
        // natural mesh origin, not at world Z=0.
        const double base_disc_bottom = src_local_min_z;
        const double base_disc_top    = base_disc_bottom + sep_thickness;

        TriangleMesh base_disc = create_separator_mesh(
            first_separator_size_ratio, sep_thickness);
        base_disc.translate(0.0f, 0.0f, (float)base_disc_bottom);
        ModelVolume* base_disc_vol = src_obj->add_volume(std::move(base_disc));
        base_disc_vol->name = base_name + "_Separator_Base";
        base_disc_vol->set_type(ModelVolumeType::MODEL_PART);

        // Enforcer fills the gap between disc top and lifted object bottom
        TriangleMesh base_above = create_separator_mesh(
            first_separator_size_ratio * 1.05, interface_gap);
        base_above.translate(0.0f, 0.0f, (float)base_disc_top);
        ModelVolume* base_above_vol = src_obj->add_volume(std::move(base_above));
        base_above_vol->name = base_name + "_Interface_Base";
        base_above_vol->set_type(ModelVolumeType::SUPPORT_ENFORCER);
    }

    // ── First separator above base object ─────────────────────────────
    // Uses normal separator_to_object_size_ratio, same as all others.
    // The base object bottom is at src_local_min_z + base_lift.
    {
        const double object_top  = src_local_min_z + base_lift + part_height;
        const double disc_bottom = object_top + interface_gap;
        place_separator(disc_bottom, separator_to_object_size_ratio, "0");
    }

    // ── Stacked copies with separators between them ───────────────────
    for (int i = 1; i <= copies; ++i)
    {
        // Copy bottom is base_lift + i * step above src_local_min_z
        const double copy_bottom = src_local_min_z + base_lift + (double)i * step;
        const double copy_top    = copy_bottom + part_height;

        for (const OriginalVolume& ov : orig_vols) {
            TriangleMesh copy_mesh = ov.mesh;
            // Translate relative to original mesh origin so copy lands at copy_bottom
            copy_mesh.translate(0.0f, 0.0f,
                (float)(copy_bottom - src_local_min_z));
            ModelVolume* copy_vol = src_obj->add_volume(std::move(copy_mesh));
            copy_vol->name = base_name + "_" + std::to_string(i + 1)
                             + "_" + ov.name;
            copy_vol->set_type(ov.type);
            if (ov.extruder > 0) {
                copy_vol->config.set_key_value(
                    "extruder", new ConfigOptionInt(ov.extruder));
            }
        }

        // Place separator above this copy if there are more copies above it
        if (i < copies) {
            const double disc_bottom = copy_top + interface_gap;
            place_separator(disc_bottom, separator_to_object_size_ratio,
                            std::to_string(i));
        }
    }

    // ── Support config ────────────────────────────────────────────────
    src_obj->config.set_key_value("enable_support",
        new ConfigOptionBool(true));
    src_obj->config.set_key_value("support_on_build_plate_only",
        new ConfigOptionBool(false));
    src_obj->config.set_key_value("support_remove_small_overhang",
        new ConfigOptionBool(false));
    src_obj->config.set_key_value("support_type",
        new ConfigOptionEnum<SupportType>(stTreeAuto));
    src_obj->config.set_key_value("support_style",
        new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));

    // ── Support blockers to prevent support inside object bodies ──────
    if (!support_objects) {
        const double padding       = 3.0;
        const double blocker_z_start = src_local_min_z
                                       + interface_gap + layer_height;
        const double blocker_z_end   = src_local_min_z
                                       + part_height + padding;
        const double blocker_height  = blocker_z_end - blocker_z_start;

        TriangleMesh blocker_base = make_cube(
            footprint_x + 2.0 * padding,
            footprint_y + 2.0 * padding,
            blocker_height
        );
        blocker_base.translate(
            (float)(src_local_bb.min.x() - padding),
            (float)(src_local_bb.min.y() - padding),
            (float)(blocker_z_start)
        );

        // Base object blocker
        ModelVolume* base_blocker = src_obj->add_volume(blocker_base);
        base_blocker->name = base_name + "_1_SupportBlocker";
        base_blocker->set_type(ModelVolumeType::SUPPORT_BLOCKER);

        // Copy blockers, each shifted up by i * step
        for (int i = 1; i <= copies; ++i) {
            TriangleMesh blocker_mesh = blocker_base;
            blocker_mesh.translate(0.0f, 0.0f, (float)((double)i * step) + interface_gap);
            ModelVolume* blocker_vol = src_obj->add_volume(
                std::move(blocker_mesh));
            blocker_vol->name = base_name + "_" + std::to_string(i + 1)
                                + "_SupportBlocker";
            blocker_vol->set_type(ModelVolumeType::SUPPORT_BLOCKER);
        }
    }

    // ── Refresh UI ────────────────────────────────────────────────────
    const std::vector<size_t> modified = { (size_t)obj_idx };
    plater->get_view3D_canvas3D()
        ->update_instance_printable_state_for_objects(modified);

    wxGetApp().obj_list()->update_name_in_list(obj_idx, 0);
    wxGetApp().obj_list()->add_volumes_to_object_in_list(obj_idx);

    plater->update();
    wxGetApp().obj_list()->update_info_items(obj_idx);
    plater->object_list_changed();
    plater->schedule_background_process();
}

void recalculate_stack_geometry(Plater* plater, int obj_idx, const Vec3d& scale_factors)
{
    if (obj_idx < 0 || obj_idx >= (int)plater->model().objects.size())
        return;

    ModelObject* obj = plater->model().objects[obj_idx];
    
    // Check if this is a stacked assembly
    size_t assembly_pos = obj->name.find("_StackAssembly");
    if (assembly_pos == std::string::npos)
        return;

    // Parse metadata from name
    std::string metadata = obj->name.substr(assembly_pos + 14);
    
    auto extract_int = [&](const std::string& key) -> int {
        size_t pos = metadata.find(key);
        if (pos == std::string::npos) return 0;
        size_t end = metadata.find('_', pos + key.length());
        std::string num_str = (end != std::string::npos)
            ? metadata.substr(pos + key.length(), end - pos - key.length())
            : metadata.substr(pos + key.length());
        return std::stoi(num_str);
    };

    const int copies = extract_int("_c");
    const double z_gap = extract_int("_g") / 100.0;
    const int interface_layers = extract_int("_l");
    const double orig_part_height = extract_int("_h") / 100.0;
    const double sep_thickness = extract_int("_s") / 100.0;
    const int base_vol_count = extract_int("_v");

    if (copies <= 0 || base_vol_count <= 0)
        return;

    // Get the CURRENT (already scaled) base volume dimensions
    BoundingBoxf3 base_bb;
    for (int i = 0; i < base_vol_count && i < (int)obj->volumes.size(); ++i) {
        base_bb.merge(obj->volumes[i]->mesh().bounding_box());
    }
    const double src_local_min_z = base_bb.min.z();
    
    // The part height from the metadata is the ORIGINAL unscaled height
    // We need to calculate what the step should be NOW, after scaling
    // The meshes are already scaled, so we measure the current base height
    const double current_part_height = base_bb.max.z() - base_bb.min.z();
    
    // Calculate the current separator thickness (also already scaled)
    // We can derive it from the scale factor: current = original * scale
    const double current_sep_thickness = sep_thickness * scale_factors.z();
    const double current_z_gap = z_gap * scale_factors.z();
    const double current_step = current_part_height + current_sep_thickness + current_z_gap;

    // Recalculate each separator and object copy position
    int vol_idx = base_vol_count; // Start after base volumes

    for (int i = 1; i <= copies; ++i) {
        // ── Separator disc position ───────────────────────────────────
        if (vol_idx >= (int)obj->volumes.size())
            break;

        ModelVolume* disc_vol = obj->volumes[vol_idx];
        const double new_disc_z = src_local_min_z
                                  + (double)(i - 1) * current_step
                                  + current_part_height;

        // Get current disc position
        BoundingBoxf3 disc_bb = disc_vol->mesh().bounding_box();
        const double current_disc_z = disc_bb.min.z();
        const double disc_offset = new_disc_z - current_disc_z;

        // Translate disc
        disc_vol->translate(0.0, 0.0, disc_offset);
        vol_idx++;

        // ── Object copy volumes position ──────────────────────────────
        const double new_copy_z_offset = (double)i * current_step;

        for (int j = 0; j < base_vol_count && vol_idx < (int)obj->volumes.size(); ++j, ++vol_idx) {
            ModelVolume* copy_vol = obj->volumes[vol_idx];
            
            // Get the corresponding base volume to calculate offset
            BoundingBoxf3 base_vol_bb = obj->volumes[j]->mesh().bounding_box();
            BoundingBoxf3 copy_vol_bb = copy_vol->mesh().bounding_box();
            
            const double target_z = base_vol_bb.min.z() + new_copy_z_offset;
            const double current_z = copy_vol_bb.min.z();
            const double offset = target_z - current_z;

            copy_vol->translate(0.0, 0.0, offset);
        }
    }

    // Mark object as modified
    obj->invalidate_bounding_box();
    
    // Force UI update
    plater->update();
}


} // namespace GUI
} // namespace Slic3r
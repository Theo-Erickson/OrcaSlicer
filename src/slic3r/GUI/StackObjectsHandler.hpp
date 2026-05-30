#pragma once
#include "libslic3r/Model.hpp"
#include "StackObjectsDialog.hpp"

namespace Slic3r {
namespace GUI {

class Plater;

void stack_objects(
    Plater*       plater,
    int copies /*How many copies to make*/, 
    int separator_layers = 6,    // thickness of the disc
    int gap_layers = 8 /*How many layers of interface to make between separator and object*/, 
    float separator_to_object_size_ratio = 1.0f /*How big to make the separator disk compared to the bounding box. Too small and it may not be supported properly */,
    float first_separator_size_ratio = 1.25f /*multiplier bottom-most separator disk only to aid in first layer adhesion*/,
    bool support_objects = false /*Whether to try supporting the object being stacked itself. If false, we will autogenerate a support blocker*/,
    bool use_base_separator = true, /*Whether to make a separator ring for the bottom most object. Attempts to better build adhesion akin to a brim*/ 
    SeparatorType separator_type = SeparatorType::PERIMETER_RING /*What separator to use between objects. Stability in ascending order: Disk->BoundingBox->ObjectSilhoutte */
    );

void recalculate_stack_geometry(Plater* plater, int obj_idx, const Vec3d& scale_factors);

} // namespace GUI
} // namespace Slic3r
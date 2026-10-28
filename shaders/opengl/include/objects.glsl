/*
 * Every object's model matrix and flags, by object index (GLObjectBuffer): read by the draws
 * through their instance attribute, and by the occlusion cull by object, with its world box.
 */

layout(std430, binding = SSBO_INSTANCE_MODELS) readonly buffer InstanceModels { mat4 b_models[]; };
layout(std430, binding = SSBO_INSTANCE_FLAGS) readonly buffer InstanceFlags { uint b_flags[]; };
// Six floats an object, its least corner then its greatest: Math::AABB as the CPU cull wrote it.
layout(std430, binding = SSBO_INSTANCE_BOXES) readonly buffer InstanceBoxes { float b_boxes[]; };

// Object @p object's world box, from @p lo to @p hi.
void objectBox(uint object, out vec3 lo, out vec3 hi) {
    const uint at = object * 6u;
    lo = vec3(b_boxes[at], b_boxes[at + 1u], b_boxes[at + 2u]);
    hi = vec3(b_boxes[at + 3u], b_boxes[at + 4u], b_boxes[at + 5u]);
}

// Whether object @p object is a Viewmodel mesh, drawn through the camera's viewmodel projection.
bool objectIsViewmodel(uint object) {
    return (b_flags[object] & INSTANCE_FLAG_VIEWMODEL) != 0u;
}

/*
 * How the eye draws an object: a Viewmodel mesh (Mesh::seenBy) through the camera's viewmodel
 * projection, squashed into the nearest VIEWMODEL_DEPTH_SHARE of depth (gl_object_buffer.h), so
 * only what lies within about that share of the near plane beyond it can draw over it - by a
 * perspective camera; an orthographic one has no such band (intoViewmodelBand). Needs
 * instancing.glsl and camera.glsl first. The prepass and the forward pass both project by
 * eyeClip, whose answers must agree to the bit under LEQUAL.
 */
layout(std430, binding = SSBO_INSTANCE_FLAGS) readonly buffer InstanceFlags { uint b_flags[]; };

bool instanceIsViewmodel() {
    return (b_flags[aInstance] & INSTANCE_FLAG_VIEWMODEL) != 0u;
}

vec4 eyeClip(vec4 worldPos) {
    if (!instanceIsViewmodel()) return u_camera.viewProjection * worldPos;
    return intoViewmodelBand(u_camera.viewmodelViewProjection * worldPos);
}

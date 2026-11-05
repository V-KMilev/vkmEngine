/*
 * How the eye draws an object: a Viewmodel mesh (Mesh::seenBy) through the camera's viewmodel
 * projection, squashed into the nearest VIEWMODEL_DEPTH_SHARE of depth (GLDepth::VIEWMODEL_SHARE), so
 * only what lies within about that share of the near plane beyond it can draw over it - by a
 * perspective camera; an orthographic one has no such band (intoViewmodelBand). Needs
 * instancing.glsl and camera.glsl first. The prepass and the forward pass both project by
 * eyeClip, whose answers must agree to the bit under GLDepth::EYE_TEST_EQUAL.
 */

bool instanceIsViewmodel() {
    return objectIsViewmodel(instanceObject());
}

vec4 eyeClip(vec4 worldPos) {
    return clipOnLayer(instanceIsViewmodel(), worldPos);
}

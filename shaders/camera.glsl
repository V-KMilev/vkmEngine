/*
 * The per-frame camera UBO. Must match CameraUBO (gl_camera.h); the GPU suite
 * holds the two to the layout the linked program reports.
 */
layout(std140, binding = UBO_CAMERA) uniform CameraBlock {
    mat4  view;
    mat4  projection;
    mat4  viewProjection;
    mat4  invView;
    mat4  invProjection;
    mat4  invViewProjection;
    vec4  cameraPosition;  // xyz = world position
    vec2  viewport;        // render target size in pixels
    float zNear;
    float zFar;
    mat4  viewmodelViewProjection;  // what a Viewmodel mesh draws through (shaders/viewmodel.glsl)
} u_camera;

// The same test CameraData::from makes.
bool cameraIsPerspective() {
    return u_camera.projection[3][3] == 0.0;
}

// Whether a window depth lies in the viewmodel's band (shaders/viewmodel.glsl), where a held
// weapon's squashed depth is, not where it is. A perspective camera's band lies within about
// VIEWMODEL_DEPTH_SHARE of the near distance past the near plane; an orthographic camera's depth
// is linear, so there it would be that share of the whole scene, and it is taken to hold none.
bool inViewmodelBand(float depth01) {
    return cameraIsPerspective() && depth01 < VIEWMODEL_DEPTH_SHARE;
}

// A viewmodel's clip position squashed into that band, which only a perspective camera has: an
// orthographic one draws it at its own depth.
vec4 intoViewmodelBand(vec4 clip) {
    if (cameraIsPerspective()) clip.z = mix(-clip.w, clip.z, VIEWMODEL_DEPTH_SHARE);
    return clip;
}

// Unit vector from a world point toward the viewer: toward the eye in perspective, back along
// the view axis in orthographic, whose rays are parallel.
vec3 toViewer(vec3 worldPos) {
    return cameraIsPerspective() ? normalize(u_camera.cameraPosition.xyz - worldPos)
                                 : normalize(u_camera.invView[2].xyz);
}

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
} u_camera;

// The same test CameraData::from makes.
bool cameraIsPerspective() {
    return u_camera.projection[3][3] == 0.0;
}

// Unit vector from a world point toward the viewer: toward the eye in perspective, back along
// the view axis in orthographic, whose rays are parallel.
vec3 toViewer(vec3 worldPos) {
    return cameraIsPerspective() ? normalize(u_camera.cameraPosition.xyz - worldPos)
                                 : normalize(u_camera.invView[2].xyz);
}

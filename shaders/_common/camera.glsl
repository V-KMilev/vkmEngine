/*
 * The per-frame camera UBO. Must match CameraUBO (gl_camera.h); the binding
 * mirrors GLBindings::UBOBindingPoints::Camera.
 */

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = world position
} u_camera;

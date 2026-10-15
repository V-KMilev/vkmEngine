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

// Whether a window depth lies in the viewmodel's band (shaders/viewmodel.glsl): the nearest
// VIEWMODEL_DEPTH_SHARE of the reversed depth. An orthographic camera has none: its linear depth
// would make the band that share of the whole scene.
bool inViewmodelBand(float depth01) {
    return cameraIsPerspective() && depth01 > NEAR_DEPTH - VIEWMODEL_DEPTH_SHARE;
}

// A viewmodel's clip position squashed into that band, which only a perspective camera has: an
// orthographic one draws it at its own depth.
vec4 intoViewmodelBand(vec4 clip) {
    if (cameraIsPerspective()) clip.z = mix(NEAR_DEPTH * clip.w, clip.z, VIEWMODEL_DEPTH_SHARE);
    return clip;
}

// Unit vector from a world point toward the viewer: toward the eye in perspective, back along
// the view axis in orthographic, whose rays are parallel.
vec3 toViewer(vec3 worldPos) {
    return cameraIsPerspective() ? normalize(u_camera.cameraPosition.xyz - worldPos)
                                 : normalize(u_camera.invView[2].xyz);
}

// The camera's right, up and back in world space: the view matrix's rows.
vec3 cameraRight() {
    return vec3(u_camera.view[0][0], u_camera.view[1][0], u_camera.view[2][0]);
}
vec3 cameraUp() {
    return vec3(u_camera.view[0][1], u_camera.view[1][1], u_camera.view[2][1]);
}
vec3 cameraBack() {
    return vec3(u_camera.view[0][2], u_camera.view[1][2], u_camera.view[2][2]);
}

// The view-space point on the near plane through screen fraction @p uv: in a perspective view, the
// ray from the eye through it.
vec3 nearPlaneAt(vec2 uv) {
    vec4 p = u_camera.invProjection * vec4(uv * 2.0 - 1.0, NEAR_DEPTH, 1.0);
    return p.xyz / p.w;
}

// How far the ray through screen fraction @p uv runs per unit of view depth: 1 in an orthographic
// view, whose rays run straight down the axis.
float rayPerViewDepth(vec2 uv) {
    if (!cameraIsPerspective()) return 1.0;
    vec3 ray = nearPlaneAt(uv);
    return length(ray) / abs(ray.z);
}

// The view-space point at view depth @p viewDepth on @p ray, a nearPlaneAt: an orthographic ray
// runs straight down -Z from its near-plane point.
vec3 viewPointOnRay(vec3 ray, float viewDepth) {
    return cameraIsPerspective() ? ray * (viewDepth / -ray.z) : vec3(ray.xy, -viewDepth);
}

// A world point's linear view depth.
float viewDepthOf(vec3 worldPos) {
    return -(u_camera.view * vec4(worldPos, 1.0)).z;
}

// Where a world point falls on the screen, as a 0..1 fraction.
vec2 screenUvOf(vec3 worldPos) {
    const vec4 clip = u_camera.viewProjection * vec4(worldPos, 1.0);
    return clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5;
}

/**
 * Selection outline vertex shader (non-instanced).
 *
 * Single draw per selected mesh; model matrix comes through u_model so the
 * VAO's per-instance attribute slots (locations 4-7) stay untouched. That
 * matters because GLInstanceBatcher caches per-VAO ownership of those
 * slots: if this pass attached its own buffer, the batcher's next-frame
 * attachToVAO would short-circuit and the forward pass would read from
 * the wrong buffer - the selected mesh would disappear entirely.
 *
 * Two-phase: stencil-write (u_inflate = 0) leaves the geometry alone;
 * outline-draw (u_inflate = 1) pushes each vertex `u_thickness` screen
 * pixels along its silhouette-projected normal.
 */
#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
} u_camera;

uniform mat4  u_model;
uniform float u_inflate;       ///< 0 = stencil write phase, 1 = outline phase
uniform float u_thickness;     ///< Outline thickness, in screen pixels
uniform vec2  u_viewportSize;  ///< Render-target dimensions, in pixels

void main() {
    mat4 mvp = u_camera.viewProjection * u_model;

    vec4 clipPos = mvp * vec4(aPos, 1.0);

    if (u_inflate > 0.5) {
        // Project a sibling vertex one tiny step along the surface normal
        // and read off the resulting screen-space direction. Normalising
        // in NDC makes the offset perpendicular to the silhouette even
        // when the model is non-uniformly scaled or rotated obliquely.
        vec4 clipPosN = mvp * vec4(aPos + aNormal * 0.001, 1.0);
        vec2 ndcPos   = clipPos.xy  / clipPos.w;
        vec2 ndcPosN  = clipPosN.xy / clipPosN.w;
        vec2 dir2     = ndcPosN - ndcPos;
        // Guard against degenerate normals (zero-length or pointing exactly
        // at the camera) by falling back to no offset; those vertices sit
        // inside the silhouette anyway and won't pass the stencil test.
        float len = length(dir2);
        vec2 dir  = (len > 1e-6) ? dir2 / len : vec2(0.0);

        // Convert pixel thickness to NDC units (NDC is 2 wide and 2 tall);
        // multiply by clipPos.w because we're editing pre-divide clip xy.
        vec2 pxToNdc = (2.0 * u_thickness) / u_viewportSize;
        clipPos.xy  += dir * pxToNdc * clipPos.w;
    }

    gl_Position = clipPos;
}

/**
 * Projected decal - box vertex stage.
 *
 * Draws the decal's unit cube ([-0.5, 0.5]); the fragment stage tests the surface under each pixel.
 */
layout(location = ATTR_POSITION) in vec3 aPos;

#include "../camera.glsl"

uniform mat4 u_model;

void main() {
    gl_Position = u_camera.viewProjection * u_model * vec4(aPos, 1.0);
}

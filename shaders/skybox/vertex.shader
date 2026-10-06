/**
 * Skybox vertex shader.
 *
 * The view's translation is stripped so the unit cube centres on the camera, and z = w puts it at
 * the far plane, where LEQUAL passes it only where depth still holds the clear. Projected by
 * u_skyProjection: the camera's own in perspective, a perspective one in orthographic, whose
 * parallel rays would see a single direction of sky.
 */
layout(location = ATTR_POSITION) in vec3 aPos;

#include "../camera.glsl"

uniform mat4 u_skyProjection;

out vec3 vDir;

void main() {
    vDir = aPos;
    mat4 rotView = mat4(mat3(u_camera.view));
    vec4 pos = u_skyProjection * rotView * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}

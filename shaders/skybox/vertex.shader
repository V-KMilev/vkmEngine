/**
 * Skybox vertex shader.
 *
 * The view's translation is stripped so the unit cube centres on the camera, and z = w puts it at
 * the far plane, where LEQUAL passes it only where depth still holds the clear.
 */
layout(location = ATTR_POSITION) in vec3 aPos;

#include "../camera.glsl"

out vec3 vDir;

void main() {
    vDir = aPos;
    mat4 rotView = mat4(mat3(u_camera.view));
    vec4 pos = u_camera.projection * rotView * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}

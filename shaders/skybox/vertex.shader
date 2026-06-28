/**
 * Skybox vertex shader.
 *
 * Unit-cube mesh (position only). The view matrix's translation is stripped
 * so the cube stays centered on the camera, and z is forced to w so the
 * skybox sits at the far plane (drawn after opaque with depth func LEQUAL).
 */

layout (location = 0) in vec3 aPos;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 vDir;

void main() {
    vDir = aPos;
    mat4 rotView = mat4(mat3(u_view));
    vec4 pos = u_projection * rotView * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}

/*
 * IBL bake - shared cube-capture vertex stage.
 *
 * Drawn with the unit-cube mesh (position only), once per cube face with the
 * face's u_view. Emits the local position so the fragment stage can treat it
 * as the capture direction for that texel. Used by every cube bake: equirect
 * load, procedural sky, irradiance convolution, specular prefilter.
 */

layout (location = 0) in vec3 aPos;

uniform mat4 u_projection;
uniform mat4 u_view;

out vec3 vLocalPos;

void main() {
    vLocalPos = aPos;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}

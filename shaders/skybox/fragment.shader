/**
 * Skybox fragment shader.
 *
 * Samples the baked environment cubemap and outputs LINEAR radiance into the
 * HDR target - the composite pass owns exposure + AgX + sRGB, so the sky
 * tone-maps consistently with lit geometry.
 */
#version 420 core

in vec3 vDir;

out vec4 FragColor;

uniform samplerCube u_envCube;
uniform float u_iblIntensity;

void main() {
    vec3 dir = normalize(vDir);
    vec3 color = texture(u_envCube, dir).rgb * u_iblIntensity;
    FragColor = vec4(color, 1.0);
}

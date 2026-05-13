/**
 * Cube shadow depth shader - fragment stage.
 *
 * Writes a linear distance value (|frag - light| / range) instead of the
 * default projected depth. The PBR shader then samples with the same
 * normalised distance so a samplerCubeArrayShadow comparison works
 * regardless of which of the 6 face frustums the fragment lies in.
 */
#version 420 core

in vec3 v_worldPos;

uniform vec3  u_lightPosition;
uniform float u_lightRange;

void main() {
    float linearDepth = length(v_worldPos - u_lightPosition) / u_lightRange;
    gl_FragDepth = clamp(linearDepth, 0.0, 1.0);
}

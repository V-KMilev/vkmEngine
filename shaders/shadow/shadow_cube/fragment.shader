#version 430 core

// Store linear distance from the light, normalised by the light's range, so the
// six faces share one comparison space and the forward pass can sample by
// direction without knowing which face it hit.
in vec3 vWorldPos;

uniform vec3  u_lightPos;
uniform float u_range;

void main() {
    gl_FragDepth = length(vWorldPos - u_lightPos) / u_range;
}

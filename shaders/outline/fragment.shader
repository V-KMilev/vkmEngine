/**
 * Selection outline fragment shader.
 *
 * Phase A (stencil write) uses color-mask=0 so this main() runs but writes
 * nothing - only the stencil buffer is updated. Phase B writes the accent
 * color into the overlay attachment so the composite pass blends it over
 * the tonemapped scene at pixel-exact intensity.
 */
#version 420 core

out vec4 FragColor;

uniform vec3 u_color;

void main() {
    FragColor = vec4(u_color, 1.0);
}

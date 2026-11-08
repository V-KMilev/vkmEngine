/*
 * The upscale's second step: EASU's frame sharpened by FSR 1's RCAS (../fsr1.glsl) into the window,
 * dithered there for its 8 bits.
 */
out vec4 FragColor;

layout(binding = UPSCALE_SLOT_SOURCE) uniform sampler2D u_source;  // EASU's frame, the viewport's size
uniform ivec2 u_origin;                                            // the viewport's corner in the window
uniform float u_sharpness;                                         // RCAS's lobe scale, 1 at the most

#include "../fsr1.glsl"
#include "../../../include/noise.glsl"

void main() {
    vec3 c = fsrRcas(u_source, ivec2(gl_FragCoord.xy) - u_origin, u_sharpness);
    FragColor = vec4(displayDither(c, gl_FragCoord.xy), 1.0);
}

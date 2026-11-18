/*
 * The upscale's first step: the composite's frame, at the scene's size, widened to the viewport's
 * by FSR 1's EASU (../fsr1.glsl).
 */
out vec4 FragColor;

layout(binding = UPSCALE_SLOT_SOURCE) uniform sampler2D u_source;  // display-encoded, in 0..1
uniform vec2 u_outputSize;                                         // the viewport's, in pixels

#include "../fsr1.glsl"

void main() {
    vec3 c = fsrEasu(u_source, floor(gl_FragCoord.xy), vec2(textureSize(u_source, 0)), u_outputSize);
    FragColor = vec4(c, 1.0);
}

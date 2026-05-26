/**
 * Auto-exposure - scene log-luminance.
 *
 * Writes log2(luminance) of the resolved HDR scene into an R16F target.
 * glGenerateMipmap then reduces it: the top 1x1 mip is the average of the
 * logs, i.e. the geometric mean luminance (robust to bright outliers).
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_hdr;

void main() {
    vec3 c = texture(u_hdr, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    FragColor = vec4(log2(max(lum, 1e-4)), 0.0, 0.0, 1.0);
}

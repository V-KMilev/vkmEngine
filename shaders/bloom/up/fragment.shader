/**
 * Bloom upsample - 3x3 tent filter.
 *
 * Reads one source mip (the smaller one) and is drawn additively (GL_ONE,
 * GL_ONE) into the next larger mip, so the chain accumulates a wide, smooth
 * energy-conserving bloom. u_filterRadius is in UV space.
 */

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_src;
uniform float u_srcLod;
uniform float u_filterRadius;

void main() {
    float x = u_filterRadius;
    float y = u_filterRadius;

    vec3 a = textureLod(u_src, vUV + vec2(-x,  y), u_srcLod).rgb;
    vec3 b = textureLod(u_src, vUV + vec2( 0,  y), u_srcLod).rgb;
    vec3 c = textureLod(u_src, vUV + vec2( x,  y), u_srcLod).rgb;

    vec3 d = textureLod(u_src, vUV + vec2(-x,  0), u_srcLod).rgb;
    vec3 e = textureLod(u_src, vUV,                u_srcLod).rgb;
    vec3 f = textureLod(u_src, vUV + vec2( x,  0), u_srcLod).rgb;

    vec3 g = textureLod(u_src, vUV + vec2(-x, -y), u_srcLod).rgb;
    vec3 h = textureLod(u_src, vUV + vec2( 0, -y), u_srcLod).rgb;
    vec3 i = textureLod(u_src, vUV + vec2( x, -y), u_srcLod).rgb;

    vec3 result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i);
    result *= (1.0 / 16.0);

    FragColor = vec4(result, 1.0);
}

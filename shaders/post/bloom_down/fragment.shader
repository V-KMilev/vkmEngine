/**
 * Bloom downsample - Call of Duty 13-tap filter.
 *
 * On the first downsample (u_karis = 1) the five 2x2 groups are combined with
 * a Karis luma average so a single firefly cannot bloom the whole screen.
 * Sampling is always from one explicit source mip (textureLod), so the same
 * shader handles "resolved HDR -> mip0" and "mip i-1 -> mip i".
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_src;
uniform float u_srcLod;
uniform int   u_karis;

float karisWeight(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(u_src, int(u_srcLod)));
    float x = texel.x;
    float y = texel.y;

    vec3 a = textureLod(u_src, vUV + vec2(-2.0*x,  2.0*y), u_srcLod).rgb;
    vec3 b = textureLod(u_src, vUV + vec2( 0.0,    2.0*y), u_srcLod).rgb;
    vec3 c = textureLod(u_src, vUV + vec2( 2.0*x,  2.0*y), u_srcLod).rgb;

    vec3 d = textureLod(u_src, vUV + vec2(-2.0*x,  0.0),   u_srcLod).rgb;
    vec3 e = textureLod(u_src, vUV,                        u_srcLod).rgb;
    vec3 f = textureLod(u_src, vUV + vec2( 2.0*x,  0.0),   u_srcLod).rgb;

    vec3 g = textureLod(u_src, vUV + vec2(-2.0*x, -2.0*y), u_srcLod).rgb;
    vec3 h = textureLod(u_src, vUV + vec2( 0.0,   -2.0*y), u_srcLod).rgb;
    vec3 i = textureLod(u_src, vUV + vec2( 2.0*x, -2.0*y), u_srcLod).rgb;

    vec3 j = textureLod(u_src, vUV + vec2(-x,  y), u_srcLod).rgb;
    vec3 k = textureLod(u_src, vUV + vec2( x,  y), u_srcLod).rgb;
    vec3 l = textureLod(u_src, vUV + vec2(-x, -y), u_srcLod).rgb;
    vec3 m = textureLod(u_src, vUV + vec2( x, -y), u_srcLod).rgb;

    vec3 result;
    if (u_karis == 1) {
        vec3 g0 = (a + b + d + e) * 0.25;
        vec3 g1 = (b + c + e + f) * 0.25;
        vec3 g2 = (d + e + g + h) * 0.25;
        vec3 g3 = (e + f + h + i) * 0.25;
        vec3 g4 = (j + k + l + m) * 0.25;

        float w0 = karisWeight(g0);
        float w1 = karisWeight(g1);
        float w2 = karisWeight(g2);
        float w3 = karisWeight(g3);
        float w4 = karisWeight(g4);

        float wSum = w0 + w1 + w2 + w3 + w4;
        result = (g0*w0 + g1*w1 + g2*w2 + g3*w3 + g4*w4) / max(wSum, 1e-4);
    } else {
        result  = e * 0.125;
        result += (a + c + g + i) * 0.03125;
        result += (b + d + f + h) * 0.0625;
        result += (j + k + l + m) * 0.125;
    }

    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}

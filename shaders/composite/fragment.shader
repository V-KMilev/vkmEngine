in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D u_hdr;    // linear HDR scene
layout(binding = 1) uniform sampler2D u_bloom;  // bloom mip 0 (energy-conserving chain)
uniform float u_bloomStrength;                  // 0 when bloom is unavailable

// Debug-view inputs. Only sampled when u_renderMode != MODE_DEFAULT, so the
// backend binds them only then; in the default path these stay untouched.
layout(binding = 19) uniform sampler2D u_sceneDepth;     // scene depth
layout(binding = 20) uniform sampler2D u_sceneGBuffer;   // oct view-normal.xy, roughness.z, metalness.w
layout(binding = 21) uniform sampler2D u_ao;             // GTAO factor
layout(binding = 11) uniform sampler2D u_shadowAtlas;    // tiled 2D shadow depth
uniform int  u_renderMode;   // 0 = final image, else a debug buffer (see MODE_* below)
uniform mat4 u_projection;   // camera projection, for depth linearization (debug only)
uniform bool u_fxaa;         // false = straight tonemapped resolve, no edge blend

// Must match RenderMode in src/engine/system/render/render_settings.h.
const int MODE_DEFAULT   = 0;
const int MODE_DEPTH     = 1;
const int MODE_NORMALS   = 2;
const int MODE_ROUGHNESS = 3;
const int MODE_METALNESS = 4;
const int MODE_AO        = 5;
const int MODE_BLOOM     = 6;
const int MODE_SHADOW    = 7;

#include "../_common/normal_codec.glsl"  // signNotZero, octDecode

// Visualize one intermediate render target, raw (no tonemap / FXAA).
vec3 debugColor(vec2 uv) {
    if (u_renderMode == MODE_NORMALS)   return octDecode(texture(u_sceneGBuffer, uv).rg) * 0.5 + 0.5;
    if (u_renderMode == MODE_ROUGHNESS) return vec3(texture(u_sceneGBuffer, uv).b);
    if (u_renderMode == MODE_METALNESS) return vec3(texture(u_sceneGBuffer, uv).a);
    if (u_renderMode == MODE_AO)        return vec3(texture(u_ao, uv).r);
    if (u_renderMode == MODE_BLOOM)     return texture(u_bloom, uv).rgb;
    if (u_renderMode == MODE_SHADOW)    return vec3(texture(u_shadowAtlas, uv).r);
    if (u_renderMode == MODE_DEPTH) {
        // Positive linear view depth (same two-coefficient form SSR uses), then
        // log-mapped between near and far: a plain lin/far divide crushes all
        // geometry to black when the far plane is large. Near = bright, far = dark.
        float ndc  = texture(u_sceneDepth, uv).r * 2.0 - 1.0;
        float lin  = u_projection[3][2] / (ndc + u_projection[2][2]);
        float near = u_projection[3][2] / (u_projection[2][2] - 1.0);
        float far  = u_projection[3][2] / (u_projection[2][2] + 1.0);
        float t    = log2(lin / near) / log2(far / near);
        return vec3(clamp(1.0 - t, 0.0, 1.0));
    }
    return texture(u_hdr, uv).rgb;
}

// Bloom-blend + tonemap + gamma a linear HDR sample to perceptual LDR. FXAA
// runs on this (edge detection wants perceptual luma, not linear radiance), so
// every tap goes through here.
vec3 resolve(vec2 uv) {
    vec3 c = texture(u_hdr, uv).rgb;
    c = mix(c, texture(u_bloom, uv).rgb, u_bloomStrength);  // bloom in linear HDR
    c = c / (c + vec3(1.0));        // Reinhard tonemap
    return pow(c, vec3(1.0 / 2.2)); // gamma to the LDR backbuffer
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// FXAA (Timothy Lottes, the widely-used simplified form): detect a luma edge
// from the 4 diagonal neighbours, find its direction, and blend a short span
// across it. Cheap first-pass AA until TAA/MSAA lands.
const float FXAA_SPAN_MAX   = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

void main() {
    // Debug views bypass tonemap + FXAA and show the raw buffer.
    if (u_renderMode != MODE_DEFAULT) {
        FragColor = vec4(debugColor(vUV), 1.0);
        return;
    }

    // FXAA disabled: tonemapped resolve of this texel only, no edge blend.
    if (!u_fxaa) {
        FragColor = vec4(resolve(vUV), 1.0);
        return;
    }

    vec2 texel = 1.0 / vec2(textureSize(u_hdr, 0));

    vec3 rgbM  = resolve(vUV);
    vec3 rgbNW = resolve(vUV + vec2(-1.0, -1.0) * texel);
    vec3 rgbNE = resolve(vUV + vec2( 1.0, -1.0) * texel);
    vec3 rgbSW = resolve(vUV + vec2(-1.0,  1.0) * texel);
    vec3 rgbSE = resolve(vUV + vec2( 1.0,  1.0) * texel);

    float lumaM  = luma(rgbM);
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Blend direction is perpendicular to the luma gradient.
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL,
                          FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * texel;

    vec3 rgbA = 0.5 * (resolve(vUV + dir * (1.0 / 3.0 - 0.5))
                     + resolve(vUV + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (resolve(vUV + dir * -0.5)
                                   + resolve(vUV + dir *  0.5));

    // The wider B span overshoots on thin features; fall back to A then.
    float lumaB = luma(rgbB);
    vec3 color = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

    FragColor = vec4(color, 1.0);
}

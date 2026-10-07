in vec2 vUV;
out vec4 FragColor;

layout(binding = COMPOSITE_SLOT_SCENE) uniform sampler2D u_hdr;    // linear HDR scene
layout(binding = COMPOSITE_SLOT_BLOOM) uniform sampler2D u_bloom;  // bloom mip 0, every level summed into it
uniform float u_bloomStrength;  // 0 when bloom is unavailable

// Debug-view inputs, read only when u_renderMode != MODE_DEFAULT; GLCompositePass binds them.
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2D u_sceneGBuffer;  // oct view-normal
layout(binding = POST_SLOT_AO)            uniform sampler2D u_ao;  // GTAO factor
// A plain sampler2D: this view shows stored depth, not a compare result. Its own binding, as a
// comparing and a non-comparing sampler cannot share a unit.
layout(binding = SHADOW_SLOT_ATLAS_2D_RAW) uniform sampler2D u_shadowAtlas;  // tiled 2D shadow depth
uniform int   u_hasAO;       // 0 when GTAO is off and nothing wrote the AO target
uniform int   u_renderMode;  // a RenderMode, 0 = final image; MODE_* come from the prelude
uniform int   u_tonemap;     // which display transform ends the frame (see TONEMAP_*)
uniform float u_exposure;    // 2^EV the frame is scaled by before the tonemap; 1 = as lit

#include "../normal_codec.glsl"
#include "../depth.glsl"
#include "../camera.glsl"
#include "../noise.glsl"  // effectNoise, for the dither
#include "../fog.glsl"    // fogAt, for the fog view
#include "../color.glsl"

// Visualize one intermediate render target, raw (no tonemap).
vec3 debugColor(vec2 uv) {
    if (u_renderMode == MODE_NORMALS) return octDecode(texture(u_sceneGBuffer, uv).rg) * 0.5 + 0.5;
    if (u_renderMode == MODE_AMBIENT_OCCLUSION) return vec3(u_hasAO != 0 ? texture(u_ao, uv).r : 1.0);
    if (u_renderMode == MODE_BLOOM) return texture(u_bloom, uv).rgb;
    if (u_renderMode == MODE_SHADOW_ATLAS) return vec3(texture(u_shadowAtlas, uv).r);
    if (u_renderMode == MODE_FOG) {
        // In-scattered fog in front of each pixel's opaque surface.
        return fogAt(uv, linearizeViewDepth(texture(u_sceneDepth, uv).r, u_camera.invProjection)).rgb;
    }
    if (u_renderMode == MODE_DEPTH) {
        // Log-mapped between near (bright) and far (dark): a plain lin/far divide crushes
        // all geometry to black when the far plane is large.
        float lin = linearizeViewDepth(texture(u_sceneDepth, uv).r, u_camera.invProjection);
        float t   = log2(lin / u_camera.zNear) / log2(u_camera.zFar / u_camera.zNear);
        return vec3(clamp(1.0 - t, 0.0, 1.0));
    }
    // The material views, which the forward pass wrote in place of the colour; the sky
    // is no surface.
    if (texture(u_sceneDepth, uv).r >= 1.0) return vec3(0.0);
    return texture(u_hdr, uv).rgb;
}

// c / (c + 1); what each curve is for is Tonemap's (render_settings.h).
vec3 tonemapReinhard(vec3 c) {
    return c / (c + vec3(1.0));
}

// Hill's fit of the ACES RRT and ODT (MJP's BakingLab), the ACES of three.js, Godot and Bevy:
// into the ACES working space, the curve, and back, with no pre-scale.
vec3 tonemapACES(vec3 c) {
    const mat3 IN = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    );
    const mat3 OUT = mat3(
        1.60475, -0.10208, -0.00327,
        -0.53108, 1.10813, -0.07276,
        -0.07367, -0.00605, 1.07602
    );
    vec3 v = IN * c;
    v = (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081);
    return clamp(OUT * v, 0.0, 1.0);
}

// AgX (Sobotka), Wrensch's minimal fit: into a wider gamut, a log2 encoding over 16.5 stops,
// a sigmoid, and out. Every channel runs to white together, so a bright saturated light whitens
// rather than turning yellow, and a primary reaches white at all. The sigmoid's output is
// display-encoded; it is linearised here, since linearToSrgb encodes after every curve.
vec3 tonemapAgX(vec3 c) {
    const mat3 IN = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    const mat3 OUT = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );
    const float MIN_EV = -12.47393;
    const float MAX_EV = 4.026069;
    vec3 x = clamp(log2(max(IN * max(c, 0.0), 1e-10)), MIN_EV, MAX_EV);
    x = (x - MIN_EV) / (MAX_EV - MIN_EV);
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    vec3 s = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2
        + 0.1191 * x - 0.00232;
    return pow(clamp(OUT * s, 0.0, 1.0), vec3(2.2));
}

// glTF's Khronos PBR Neutral.
vec3 tonemapKhronosNeutral(vec3 c) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation     = 0.15;

    float low    = min(c.r, min(c.g, c.b));
    float offset = low < 0.08 ? low - 6.25 * low * low : 0.04;
    c -= offset;

    float peak = max(c.r, max(c.g, c.b));
    if (peak < startCompression) return c;

    float span    = 1.0 - startCompression;
    float newPeak = 1.0 - span * span / (peak + span - startCompression);
    c *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(c, vec3(newPeak), g);
}

// Bloom-add, expose, tonemap and sRGB-encode a linear HDR sample for the 8-bit backbuffer.
vec3 resolve(vec2 uv) {
    vec3 c = texture(u_hdr, uv).rgb;
    // Added in linear HDR, not blended: the bloom holds only what passed the
    // threshold, so a blend would darken every pixel away from a bright source.
    if (u_bloomStrength > 0.0) c += texture(u_bloom, uv).rgb * u_bloomStrength;
    c *= u_exposure;

    if      (u_tonemap == TONEMAP_AGX)             c = tonemapAgX(c);
    else if (u_tonemap == TONEMAP_ACES)            c = tonemapACES(c);
    else if (u_tonemap == TONEMAP_KHRONOS_NEUTRAL) c = tonemapKhronosNeutral(c);
    else                                           c = tonemapReinhard(c);

    return linearToSrgb(c);
}

void main() {
    // Debug views show the raw buffer, except those the forward pass shades as radiance.
    if (((MODE_SHADED_MASK >> u_renderMode) & 1) == 0) {
        FragColor = vec4(debugColor(vUV), 1.0);
        return;
    }

    // Triangular noise of one step either way turns the 8-bit backbuffer's banding into grain,
    // equally strong at every level (Gjol, INSIDE).
    float n = effectNoise(gl_FragCoord.xy, NOISE_DITHER) * 2.0 - 1.0;
    n = sign(n) * (1.0 - sqrt(1.0 - abs(n)));
    FragColor = vec4(clamp(resolve(vUV) + n / 255.0, 0.0, 1.0), 1.0);
}

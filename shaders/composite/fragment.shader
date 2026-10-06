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

// Narkowicz's fit of the ACES film curve: more contrast than Reinhard, and
// highlights roll off warm instead of toward white.
vec3 tonemapACES(vec3 c) {
    const float a = 2.51;
    const float b = 0.03;
    const float k = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((c * (a * c + b)) / (c * (k * c + d) + e), 0.0, 1.0);
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

    if      (u_tonemap == TONEMAP_ACES)            c = tonemapACES(c);
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

    // Half a step of noise either way on the encoded value turns the 8-bit backbuffer's
    // banding of smooth gradients into grain no eye resolves.
    float dither = (effectNoise(gl_FragCoord.xy, NOISE_DITHER) - 0.5) / 255.0;
    FragColor = vec4(resolve(vUV) + dither, 1.0);
}

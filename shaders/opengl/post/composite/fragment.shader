in vec2 vUV;
out vec4 FragColor;

layout(binding = COMPOSITE_SLOT_SCENE) uniform sampler2D u_hdr;    // linear HDR scene
layout(binding = COMPOSITE_SLOT_BLOOM) uniform sampler2D u_bloom;  // bloom mip 0, every level summed into it
uniform float u_bloomStrength;  // 0 when bloom is unavailable

// Debug-view inputs, read only when u_renderMode != MODE_LIT; GLCompositePass binds them.
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
// A plain sampler2D: this view shows stored depth, not a compare result. Its own binding, as a
// comparing and a non-comparing sampler cannot share a unit.
layout(binding = SHADOW_SLOT_ATLAS_2D_RAW) uniform sampler2D u_shadowAtlasRaw;  // tiled 2D shadow depth
uniform int   u_renderMode;  // a RenderMode, 0 = final image; MODE_* come from the prelude
uniform int   u_tonemap;     // which display transform ends the frame (see TONEMAP_*)
uniform float u_exposure;    // 2^EV the frame is scaled by before the tonemap; 1 = as lit
uniform int   u_dither;      // 0 when the upscale takes the frame and dithers it in the window

// The color grading table, display values in and out; read only while u_gradingStrength is above 0.
layout(binding = COMPOSITE_SLOT_GRADING) uniform sampler3D u_grading;
uniform float u_gradingStrength;  // 0..1, the graded color's share; 0 with no table

#include "../../include/scene_gbuffer.glsl"
#include "../../include/scene_ao.glsl"
#include "../../include/depth.glsl"
#include "../../include/camera.glsl"
#include "../../include/noise.glsl"  // displayDither
#include "../../include/fog.glsl"    // fogAt, for the fog view
#include "../../include/color.glsl"

// Visualize one intermediate render target, raw (no tonemap).
vec3 debugColor(vec2 uv) {
    if (u_renderMode == MODE_NORMALS) return sceneNormalAt(uv) * 0.5 + 0.5;
    if (u_renderMode == MODE_AMBIENT_OCCLUSION) return vec3(aoAt(uv));
    if (u_renderMode == MODE_BLOOM) return texture(u_bloom, uv).rgb;
    if (u_renderMode == MODE_SHADOW_ATLAS) return vec3(texture(u_shadowAtlasRaw, uv).r);
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
    // The material views, which the forward pass wrote in place of the color; the sky
    // is no surface.
    if (isFarDepth(texture(u_sceneDepth, uv).r)) return vec3(0.0);
    return texture(u_hdr, uv).rgb;
}

// What each curve is for is Tonemap's (render_settings.h).

// Hill's fit of the ACES RRT and ODT (MJP's BakingLab): into the ACES working space, the curve,
// and back, with no pre-scale.
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

// Hable's filmic curve, with his shoulder, linear and toe strengths.
vec3 hableCurve(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return (x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F) - E / F;
}

// Hable's curve over the frame pre-scaled by 2.8 and held below the white point, 11.2, which
// lands on 1.
vec3 tonemapHable(vec3 c) {
    const float WHITE = 11.2;
    return hableCurve(min(max(c, 0.0) * 2.8, vec3(WHITE))) / hableCurve(vec3(WHITE));
}

// Bloom-add, expose, tonemap and sRGB-encode a linear HDR sample for the 8-bit backbuffer.
vec3 resolve(vec2 uv) {
    vec3 c = texture(u_hdr, uv).rgb;
    // Added in linear HDR, not blended: the bloom holds only what passed the
    // threshold, so a blend would darken every pixel away from a bright source.
    if (u_bloomStrength > 0.0) c += texture(u_bloom, uv).rgb * u_bloomStrength;
    c *= u_exposure;

    return linearToSrgb(u_tonemap == TONEMAP_HABLE ? tonemapHable(c) : tonemapACES(c));
}

// A display value through the grading table. Its points lie on the texel centers, so 0 and 1 are
// read at the first and last centers, not at the table's outer edges.
vec3 grade(vec3 display) {
    float size  = float(textureSize(u_grading, 0).x);
    vec3 graded = textureLod(u_grading, display * ((size - 1.0) / size) + 0.5 / size, 0.0).rgb;
    return mix(display, graded, u_gradingStrength);
}

void main() {
    // Debug views show the raw buffer, except those the forward pass shades as radiance; at a
    // lowered render scale the upscale widens them as it does the frame.
    if (((MODE_SHADED_MASK >> u_renderMode) & 1) == 0) {
        FragColor = vec4(debugColor(vUV), 1.0);
        return;
    }

    // Graded as a screenshot holds the frame, which is what the table was made on.
    vec3 display = resolve(vUV);
    if (u_gradingStrength > 0.0) display = grade(display);
    display = clamp(display, 0.0, 1.0);
    FragColor = vec4(u_dither == 1 ? displayDither(display, gl_FragCoord.xy) : display, 1.0);
}

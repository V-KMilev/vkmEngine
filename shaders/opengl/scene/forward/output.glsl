/*
 * What a forward fragment writes: its color, fogged at its own depth, and the reflection pass's
 * inputs (GLTarget, Layout::Scene); or, in a debug view, a part of it.
 */

#include "../../include/color.glsl"
#include "../../include/constants.glsl"
#include "../../include/fog.glsl"
#include "../../include/surface.glsl"

layout(location = OUT_COLOR) out vec4 FragColor;

// The reflection pass's inputs. The product is stored, not the radiance, so the pass subtracts
// exactly what was added whatever the weight's quantizing. A pixel reflecting nothing writes zero;
// alpha is what a transparent surface blends by.
// rgb: the split-sum weight a trace replaces, zero where none may (traceFade); a: the lobe's
// roughness, its widest axis.
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV) out vec4 ReflectEnv;  // rgb: that weight x the probe / sky radiance

uniform int u_renderMode;       // MODE_* debug view; 0 everywhere but the main view
uniform int u_wirePass;         // 1 on the Wireframe view's second draw, of its lines alone
uniform int u_alphaToCoverage;  // 1 while alpha-to-coverage is on for a cutout

const vec3  WIRE_COLOR = vec3(0.0);
const float WIRE_ALPHA = 0.55;

// The share of a lobe of @p roughness the one reflection trace, at @p traceLobe, stands for: all of
// the sharper lobe, and none of one TRACE_LOBE_GAP rougher, which keeps its environment's blur.
const float TRACE_LOBE_GAP = 0.25;
float traceShare(float roughness, float traceLobe) {
    return clamp(1.0 - (roughness - traceLobe) / TRACE_LOBE_GAP, 0.0, 1.0);
}

// The share of its environment's reflection a trace may replace on a material of @p roughness as
// authored: all of a near-mirror's, fading to none at SSR_MAX_ROUGHNESS (the roughest a trace
// serves), so that a roughness map crossing it draws no contour.
float traceFade(float roughness) {
    return 1.0 - smoothstep(0.75 * SSR_MAX_ROUGHNESS, SSR_MAX_ROUGHNESS, roughness);
}

// How much of the fragment a cutout at @p uv covers, before lighting: the ~1px edge for
// alpha-to-coverage, else whole or cut at half. Discards what it cuts away.
float cutoutCoverage(vec2 uv) {
    if (u_material.type != MAT_ALPHA_MASK) return 1.0;
    float a = sampledOpacity(uv);
    float coverage = clamp((a - u_material.alphaCutoff) / max(fwidth(a), 1e-5) + 0.5, 0.0, 1.0);
    if (u_alphaToCoverage == 0) coverage = step(0.5, coverage);
    if (coverage <= 0.0) discard;
    return coverage;
}

// The Wireframe view's lines, blended over what the first draw shaded; the reflection inputs write
// a zero alpha, which leaves them as they were. Whether it wrote them.
bool writeWire() {
    if (u_wirePass == 0) return false;
    FragColor = vec4(WIRE_COLOR, WIRE_ALPHA);
    return true;
}

// The material views: the surface as sampled, before any light, shown raw. Whether it wrote one;
// the lighting-only view instead turns @p s into a white, rough-as-authored dielectric.
bool writeSurfaceView(inout Surface s) {
    if (u_renderMode == MODE_ALBEDO)    { FragColor = vec4(linearToSrgb(s.albedo), 1.0); return true; }
    if (u_renderMode == MODE_ROUGHNESS) { FragColor = vec4(vec3(s.roughness), 1.0); return true; }
    if (u_renderMode == MODE_METALNESS) { FragColor = vec4(vec3(s.metallic), 1.0); return true; }
    if (u_renderMode == MODE_LIGHTING_ONLY) {
        s.albedo   = vec3(1.0);
        s.metallic = 0.0;
        s.emission = vec3(0.0);
    }
    return false;
}

// An unlit material's color, fogged. Whether it is one.
bool writeUnlit(Surface s) {
    if (u_material.type != MAT_UNLIT) return false;
    vec4 fog = fragmentFog();
    FragColor = vec4((s.albedo + s.emission) * fog.a + fog.rgb, s.opacity);
    return true;
}

// The Light Clusters view: green -> yellow -> red over the cluster's light count, magenta where
// the list is full, as lights past it are dropped and show as a cut.
vec3 clusterHeat(uint count) {
    float t    = clamp(float(count) / float(MAX_LIGHTS_PER_CLUSTER), 0.0, 1.0) * 3.0;
    vec3  heat = mix(vec3(0.02, 0.10, 0.02), vec3(0.15, 0.85, 0.15), clamp(t, 0.0, 1.0));
    heat       = mix(heat, vec3(0.95, 0.85, 0.10), clamp(t - 1.0, 0.0, 1.0));
    heat       = mix(heat, vec3(0.95, 0.10, 0.10), clamp(t - 2.0, 0.0, 1.0));
    if (count >= uint(MAX_LIGHTS_PER_CLUSTER)) heat = vec3(0.95, 0.10, 0.95);
    return heat;
}

// The lighting views: the environment's light @p ambient, the direct @p direct, or the @p count of
// the cluster's lights. Whether it wrote one.
bool writeLightView(vec3 ambient, vec3 direct, uint count) {
    if (u_renderMode == MODE_GI_ONLY)     { FragColor = vec4(ambient, 1.0); return true; }
    if (u_renderMode == MODE_DIRECT_ONLY) { FragColor = vec4(direct, 1.0); return true; }
    if (u_renderMode == MODE_CLUSTERS)    { FragColor = vec4(clusterHeat(count), 1.0); return true; }
    return false;
}

// @p lit fogged by @p fog (fragmentFog). The reflection inputs lose what the color loses, and keep
// only what a trace of a lobe as rough as @p tracedAs replaces.
vec3 fogged(vec3 lit, vec4 fog, float tracedAs) {
    float replaced = fog.a * traceFade(tracedAs);
    ReflectWeight.rgb *= replaced;
    ReflectEnv.rgb    *= replaced;
    return lit * fog.a + fog.rgb;
}

// Write @p color, a transparent surface's at @p opacity and a cutout's at its @p coverage.
void writeColor(vec3 color, float opacity, float coverage) {
    // Clamped to what RGBA16F holds: past it a channel becomes infinity.
    float outAlpha = (u_material.type == MAT_TRANSPARENT) ? opacity : coverage;
    FragColor = vec4(min(color, vec3(HALF_MAX)), outAlpha);

    // Blends zero at its opacity over the inputs behind, roughness masked off
    // (GLForwardPass), dimming them as it dims everything behind.
    if (u_material.type == MAT_TRANSPARENT) {
        ReflectWeight = vec4(0.0, 0.0, 0.0, outAlpha);
        ReflectEnv    = vec4(0.0, 0.0, 0.0, outAlpha);
    }

    // Never traced: the prepass skips cutouts, so the G-buffer under one holds
    // the surface behind.
    if (u_material.type == MAT_ALPHA_MASK) {
        ReflectWeight = vec4(0.0);
        ReflectEnv    = vec4(0.0);
    }
}

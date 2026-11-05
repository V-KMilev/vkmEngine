/*
 * Forward PBR ubershader for the full MaterialAsset spec: metal-rough GGX base,
 * clearcoat, sheen, subsurface, thin transmission, alpha mask and unlit; probes
 * over the IBL. Its frame, lobes, lights and output are the forward modules
 * (shading.glsl, lobes.glsl, direct_light.glsl, environment_light.glsl,
 * output.glsl). Each fragment, transparent ones too, is fogged here at its
 * own depth and never again; shading-split debug views are not.
 */

#include "../../../include/constants.glsl"

in vec3 vWorldPos;
flat in uint vViewmodel;
flat in uint vFade;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in float vHandedness;
#ifdef BAKED
in vec2      vLightmapUV;
flat in int  vLightmapPage;
#endif

// Whether the optional features - subsurface, transmission, clearcoat and sheen - are compiled in.
// The plain variant (PLAIN) leaves them out, for a material that uses none
// (GLMaterial::plain): a feature gated here is one GLMaterial::update tests.
#ifdef PLAIN
const bool FEATURES = false;
#else
const bool FEATURES = true;
#endif

#include "../../../include/camera.glsl"
#include "../fade.glsl"
#include "../direct_light.glsl"
#include "../environment_light.glsl"

// Opaque + sky copy for transmission refraction, transparent bucket only.
layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_sceneColor;
uniform int u_hasSceneColor;

vec3 lightReflected(
    Shading sh,
    Surface s,
    vec3 L,
    vec3 radiance,
    float sourceTan,
    vec3 Ls,
    float sphereNorm
) {
    return evaluateLight(sh, s, L, radiance, sourceTan, Ls, sphereNorm);
}

vec3 environmentDiffuse(Surface s, vec3 irradiance, vec3 kD) {
    return irradiance * s.albedo * kD;
}

// Screen-space refraction of @p color. What the surface reflects, @p reflected, stays whole and
// only the rest blends toward the scene behind, itself dimmed by the reflected share @p FssEss, so
// glass keeps its reflections and highlights.
vec3 refraction(Shading sh, Surface s, vec3 color, vec3 reflected, vec3 FssEss) {
    vec3 rdir = refract(-sh.V, sh.N, 1.0 / max(u_material.ior, 1.0));
    if (dot(rdir, rdir) <= 0.0) return color;

    float thickness = (u_material.thicknessFactor > 0.0) ? u_material.thicknessFactor : 0.5;
    vec2 uv0 = screenUvOf(vWorldPos);
    vec2 uv1 = screenUvOf(vWorldPos + rdir * thickness);
    vec2 ruv = clamp(gl_FragCoord.xy / u_camera.viewport + (uv1 - uv0), vec2(0.0), vec2(1.0));

    vec3 transmitted = texture(u_sceneColor, ruv).rgb * s.albedo;
    if (u_material.thicknessFactor > 0.0 && u_material.attenuationColor.a > 0.0) {
        float dist = u_material.thicknessFactor / max(abs(dot(sh.N, rdir)), 0.1);
        transmitted *= pow(
            max(u_material.attenuationColor.rgb, vec3(1e-4)),
            vec3(dist / u_material.attenuationColor.a)
        );
    }
    transmitted *= 1.0 - FssEss;
    return mix(color - reflected, transmitted, s.transmission * (1.0 - s.metallic)) + reflected;
}

void main() {
    // The samples it covers, as the prepass laid their depth.
    fadeCover(vFade);
    ReflectWeight = vec4(0.0);
    ReflectEnv    = vec4(0.0);

    Shading sh;
    sh.P         = vWorldPos;
    sh.V         = toViewer(vWorldPos);
    sh.viewmodel = vViewmodel != 0u;
#ifdef BAKED
    sh.lightmapUV   = vLightmapUV;
    sh.lightmapPage = vLightmapPage;
#endif

    vec3       Ng  = normalize(vNormal);
    const mat3 TBN = shadingFrame(Ng, vTangent, vHandedness);
    sh.Ng = Ng;
    sh.uv = vUV;

    const float maskCoverage = cutoutCoverage(sh.uv);
    if (writeWire()) return;

    Surface s = sampleSurface(sh.uv);
    // Whether a trace stands in is the material's: the specular AA below widens the lobe it is
    // traced at, but leaves a mirror a mirror.
    const float authored = s.roughness;

    if (writeSurfaceView(s)) return;
    if (writeUnlit(s)) return;

    sh.N = getNormal(sh.uv, Ng, TBN);

    // The trace's lobe as authored: the coat's where it is the sharper (environmentLight).
    const float tracedAs = coated(s) ? min(authored, s.clearcoatRoughness) : authored;

    // The coat too, on its own smooth normal: it is smoother and shimmers first.
    s.roughness          = specularAA(sh.N, s.roughness);
    s.clearcoatRoughness = specularAA(Ng, s.clearcoatRoughness);

    const vec2 dfg = shadingResponse(sh, s);

    const bool litFromBehind = hasSubsurface() || (FEATURES && s.transmission > 0.001);
    uint count;
    vec3 Lo = directLight(sh, s, litFromBehind, count);

    vec3 ambientSpecular;
    vec3 FssEss;
    vec3 ambient = environmentLight(sh, s, dfg, ambientSpecular, FssEss);
    if (writeLightView(ambient, Lo, count)) return;

    // Before refraction: its scene copy is already fogged.
    const vec4 fog   = fragmentFog();
    vec3       color = fogged(ambient + Lo + s.emission, fog, tracedAs);
    if (FEATURES && u_hasSceneColor == 1 && s.transmission > 0.0) {
        color = refraction(sh, s, color, (ambientSpecular + g_directSpecular) * fog.a, FssEss);
    }
    writeColor(color, s.opacity, maskCoverage);
}

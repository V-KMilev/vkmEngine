/*
 * Water (Water): its surface moved by its share of the scene's waves and rippled by its share of
 * their ripples, running at its flow, foamed where the waves squeeze it and where something stands in
 * it, and lit through the forward modules with the light it takes in scattered back out of it
 * (water.glsl). A material's optional lobes are compiled out: water has none.
 */

#include "../../../include/constants.glsl"

in vec3 vWorldPos;
flat in uint vViewmodel;
flat in uint vFade;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in float vHandedness;
in vec2  vRest;
in float vHeight;
in vec3  vAlongX;
in vec3  vAlongZ;
in float vLingering;
flat in uint vBody;

const bool FEATURES = false;

#include "../../../include/camera.glsl"
#include "water.glsl"
#include "../fade.glsl"
#include "../direct_light.glsl"
#include "../environment_light.glsl"

// The water's scattering color, its strength in it; foam over it scatters none.
vec3 g_scatter = vec3(0.0);

vec3 lightReflected(
    Shading sh,
    Surface s,
    vec3 L,
    vec3 radiance,
    float sourceTan,
    vec3 Ls,
    float sphereNorm
) {
    vec3 reflected = evaluateLight(sh, s, L, radiance, sourceTan, Ls, sphereNorm);
    const vec3 into = 1.0 - fresnelSchlick(sh.NdotV, sh.f0);
    reflected += into * waterScatter(g_scatter, sh.N, sh.V, L, radiance, crestOf(vHeight));
    return reflected;
}

// Its own color too, lit by the sky it takes in.
vec3 environmentDiffuse(Surface s, vec3 irradiance, vec3 kD) {
    vec3 diffuse = irradiance * s.albedo * kD;
    diffuse += irradiance * g_scatter * (SCATTER_AMBIENT * kD);
    return diffuse;
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

    WaterPoint at;
    at.rest      = vRest;
    at.position  = vWorldPos;
    at.height    = vHeight;
    at.normal    = normalize(vNormal);
    at.tangent   = vTangent;
    at.alongX    = vAlongX;
    at.alongZ    = vAlongZ;
    at.lingering = vLingering;
    float            foam;
    const WaterShade water = waterShade(b_waterBodies[vBody], at, foam);
    vec3       Ng  = water.normal;
    const mat3 TBN = shadingFrame(Ng, water.tangent, vHandedness);
    sh.Ng = Ng;
    sh.uv = vUV;

    const float maskCoverage = cutoutCoverage(sh.uv);
    if (writeWire()) return;

    Surface s = sampleSurface(sh.uv);
    // Whether a trace stands in is the material's: the ripples and the specular AA below widen the
    // lobe it is traced at, blurring a far sea's reflection, but leave a mirror a mirror.
    float authored = s.roughness;

    g_scatter = u_material.subsurfaceColor.rgb * (u_material.subsurface * (1.0 - foam));
    // Ripples too fine for the pixel roughen it.
    s.roughness = sqrt(waterAlpha(s.roughness, water.variance));
    // Whitecaps and waterlines: foam over the water.
    s.albedo    = mix(s.albedo, FOAM_COLOR, foam);
    s.roughness = mix(s.roughness, FOAM_ROUGHNESS, foam);
    authored    = mix(authored, FOAM_ROUGHNESS, foam);
    s.metallic  = mix(s.metallic, 0.0, foam);

    if (writeSurfaceView(s)) return;
    if (writeUnlit(s)) return;

    sh.N        = getNormal(sh.uv, Ng, TBN);
    s.roughness = specularAA(sh.N, s.roughness);
    const vec2 dfg = shadingResponse(sh, s);

    // Lit from behind too: through a crest.
    uint count;
    vec3 Lo = directLight(sh, s, true, count);

    vec3 ambientSpecular;
    vec3 FssEss;
    vec3 ambient = environmentLight(sh, s, dfg, ambientSpecular, FssEss);
    if (writeLightView(ambient, Lo, count)) return;

    writeColor(fogged(ambient + Lo + s.emission, fragmentFog(), authored), s.opacity, maskCoverage);
}

/*
 * A forward fragment's frame - where it is, its normals, the viewer - and its specular response:
 * what every light (direct_light.glsl) and the environment (environment_light.glsl) read.
 */

#include "../../include/brdf.glsl"
#include "../../include/surface.glsl"

// Split-sum BRDF/DFG LUT, integrated at start, so valid with no sky.
layout(binding = IBL_SLOT_BRDF_LUT) uniform sampler2D u_brdfLUT;

// The fragment's frame and what every light it takes reads.
struct Shading {
    vec3  P;                   // world position
    vec3  V;                   // toward the viewer
    vec3  N;                   // the shading normal, the normal map's
    vec3  Ng;                  // the geometric normal; a back face's turned toward it
    vec2  uv;
    vec3  f0;
    float NdotV;
    vec3  energyCompensation;  // multi-bounce energy, 1 + f0 (1 / Ess - 1)
    bool  viewmodel;           // drawn through the viewmodel projection: its lights are found by P
#ifdef BAKED
    vec2  lightmapUV;          // where in its lightmap page
    int   lightmapPage;        // which page, -1 for none
#endif
};

// The tangent frame of geometric normal @p Ng and interpolated @p tangent, of @p handedness. A back
// face's frame turns whole, @p Ng with it, so its normal map mirrors too (the glTF sample viewer's);
// a single-sided material never shows one.
mat3 shadingFrame(inout vec3 Ng, vec3 tangent, float handedness) {
    // Interpolation skews the frame; re-orthogonalize, or normal maps read a tilted basis.
    vec3 tanFlat = tangent - Ng * dot(Ng, tangent);
    // A tangent interpolated onto the normal keeps the frame it was given.
    vec3 T = (dot(tanFlat, tanFlat) > 1e-8) ? normalize(tanFlat) : normalize(tangent);
    // The sign cross cannot recompute: a mirrored UV shell is left-handed.
    vec3 B = (handedness < 0.0) ? -cross(Ng, T) : cross(Ng, T);
    if (!gl_FrontFacing) {
        T  = -T;
        B  = -B;
        Ng = -Ng;
    }
    return mat3(T, B, Ng);
}

// The geometric normal, or the normal map through the TBN. Maps hold x and y
// alone (BC5 or RG8), so z is rebuilt.
vec3 getNormal(vec2 uv, vec3 Ng, mat3 tbn) {
#ifdef LAYERED
    vec3 n;
    n.xy = layeredNormalXY(uv);
    n.z  = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));
    return normalize(tbn * normalize(n));
#else
    if (!hasTex(MATERIAL_SLOT_NORMAL)) {
        return Ng;
    }
    vec3 n;
    n.xy = texture(u_normalTexture, uv).rg * 2.0 - 1.0;
    n.z  = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));
    n.xy *= u_material.normalScale;
    return normalize(tbn * normalize(n));
#endif
}

// Geometric specular antialiasing (Karis): roughness rises with
// screen-space normal variance so normal detail does not shimmer.
float specularAA(vec3 N, float roughness) {
    const float SAA_VARIANCE  = 0.25;
    const float SAA_THRESHOLD = 0.18;
    vec3  dndu     = dFdx(N);
    vec3  dndv     = dFdy(N);
    float variance = SAA_VARIANCE * (dot(dndu, dndu) + dot(dndv, dndv));
    float alpha2   = (roughness * roughness) * (roughness * roughness);
    float kernel   = min(2.0 * variance, SAA_THRESHOLD);
    float a2       = clamp(alpha2 + kernel, 0.0, 1.0);
    return sqrt(sqrt(a2));   // back to perceptual roughness
}

// Fill @p sh's specular response to @p s, its shading normal set; returns the DFG pair, read once
// for the environment, the reflection weight and the direct energy compensation.
vec2 shadingResponse(inout Shading sh, Surface s) {
    sh.f0    = surfaceF0(u_material.ior, s.albedo, s.metallic);
    sh.NdotV = max(dot(sh.N, sh.V), 1e-4);
    vec2 dfg = texture(u_brdfLUT, vec2(sh.NdotV, s.roughness)).rg;
    sh.energyCompensation = 1.0 + sh.f0 * (1.0 / max(dfg.x + dfg.y, 1e-4) - 1.0);
    return dfg;
}

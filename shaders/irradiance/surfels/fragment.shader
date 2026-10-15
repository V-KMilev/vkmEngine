/*
 * Irradiance volume - what a probe sees, kept as surfaces to light (shaders/irradiance/surfel.glsl).
 *
 * Drawn a cube face at a time into the probe's tile of the surfel atlas, culling off and the
 * depth test on, so each texel is the surface nearest the probe in its direction. Culling off is
 * how a probe inside a solid finds out: it sees the back of whatever encloses it. A double-sided
 * material's back is a surface, not an inside. The geometric normal alone: a normal map's detail
 * averages away over a texel this coarse, and in the SH-L1 the probe keeps.
 */

#include "../../surface.glsl"
#include "../../camera.glsl"
#include "../surfel.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform int u_doubleSided;

layout(location = OUT_COLOR) out uvec4 SurfelOut;

void main() {
    if (u_material.type == MAT_ALPHA_MASK && sampledOpacity(vUV) < u_material.alphaCutoff) discard;

    Surfel surfel;
    surfel.distance = length(vWorldPos - u_camera.cameraPosition.xyz);
    if (!gl_FrontFacing && u_doubleSided == 0) {
        surfel.bounce   = vec3(0.0);
        surfel.ao       = 0.0;
        surfel.normal   = vec3(0.0, 0.0, 1.0);
        surfel.distance = -surfel.distance;
        surfel.emission = vec3(0.0);
        SurfelOut = packSurfel(surfel);
        return;
    }

    Surface s = sampleSurface(vUV);
    surfel.normal = normalize(gl_FrontFacing ? vNormal : -vNormal);
    surfel.ao     = s.ao;
    if (u_material.type == MAT_UNLIT) {
        surfel.bounce   = vec3(0.0);
        surfel.emission = s.albedo + s.emission;
    } else {
        vec3 f0 = surfaceF0(u_material.ior, s.albedo, s.metallic);
        surfel.bounce   = s.albedo * (1.0 - s.metallic) * (1.0 - f0) + f0;
        surfel.emission = s.emission;
    }
    SurfelOut = packSurfel(surfel);
}

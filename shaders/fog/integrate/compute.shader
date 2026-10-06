/**
 * Froxel fog - scattering integration.
 *
 * One invocation per froxel column (x,y): march front-to-back accumulating
 * in-scattered light weighted by transmittance, and the transmittance itself.
 * Writes (accumulated scattering rgb, transmittance a) per froxel, read through
 * shaders/fog.glsl. Energy-conserving slice integration (Frostbite).
 */

#include "../../depth.glsl"
#include "../../camera.glsl"

layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE, local_size_z = 1) in;

layout(binding = 0, rgba16f) uniform readonly  image3D u_scatter;     // rgb scatter, a = extinction
layout(binding = 1, rgba16f) uniform writeonly image3D u_integrated;  // rgb accum, a = transmittance

uniform ivec3 u_froxelDims;  // froxel grid resolution (runtime; fog quality)
uniform float u_fogDepth;    // view depth the slices reach

void main() {
    ivec2 col = ivec2(gl_GlobalInvocationID.xy);
    if (col.x >= u_froxelDims.x || col.y >= u_froxelDims.y) return;

    // The slices are steps in view depth, which the column's ray crosses at an
    // angle: the light travels this much further through each, from the
    // column's centre. An orthographic ray runs straight down the axis.
    float rayPerDepth = 1.0;
    if (cameraIsPerspective()) {
        vec2 uv    = (vec2(col) + 0.5) / vec2(u_froxelDims.xy);
        vec4 nearH = u_camera.invProjection * vec4(uv * 2.0 - 1.0, -1.0, 1.0);
        vec3 ray   = nearH.xyz / nearH.w;
        rayPerDepth = length(ray) / abs(ray.z);
    }

    vec3  accum         = vec3(0.0);
    float transmittance = 1.0;
    float prevDepth     = u_camera.zNear;

    for (int z = 0; z < u_froxelDims.z; ++z) {
        vec4  s         = imageLoad(u_scatter, ivec3(col, z));
        float thisDepth = sliceToViewDepth(float(z) + 1.0, u_camera.zNear, u_fogDepth, float(u_froxelDims.z));
        float stepLen   = (thisDepth - prevDepth) * rayPerDepth;
        prevDepth       = thisDepth;

        float extinction = max(s.a, 1e-6);
        float sliceT     = exp(-extinction * stepLen);
        // Integrate in-scattering over the slice: (S - S*T) / extinction.
        vec3  sliceScat  = (s.rgb - s.rgb * sliceT) / extinction;

        accum         += transmittance * sliceScat;
        transmittance *= sliceT;

        imageStore(u_integrated, ivec3(col, z), vec4(accum, transmittance));
    }
}

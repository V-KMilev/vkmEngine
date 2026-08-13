/**
 * Froxel fog - scattering integration.
 *
 * One invocation per froxel column (x,y): march front-to-back accumulating
 * in-scattered light weighted by transmittance, and the transmittance itself.
 * Writes (accumulated scattering rgb, transmittance a) per froxel for the apply
 * pass. Energy-conserving slice integration (Frostbite).
 */

#include "../../_common/depth.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba16f) uniform readonly  image3D u_scatter;     // rgb scatter, a = extinction
layout(binding = 1, rgba16f) uniform writeonly image3D u_integrated;  // rgb accum, a = transmittance

uniform float u_zNear;
uniform float u_zFar;
uniform ivec3 u_froxelDims;  // froxel grid resolution (runtime; fog quality)

void main() {
    ivec2 col = ivec2(gl_GlobalInvocationID.xy);
    if (col.x >= u_froxelDims.x || col.y >= u_froxelDims.y) return;

    vec3  accum         = vec3(0.0);
    float transmittance = 1.0;
    float prevDepth     = u_zNear;

    for (int z = 0; z < u_froxelDims.z; ++z) {
        vec4  s          = imageLoad(u_scatter, ivec3(col, z));
        float thisDepth  = sliceToViewDepth(float(z) + 1.0, u_zNear, u_zFar, float(u_froxelDims.z));
        float stepLen    = thisDepth - prevDepth;
        prevDepth        = thisDepth;

        float extinction = max(s.a, 1e-6);
        float sliceT     = exp(-extinction * stepLen);
        // Integrate in-scattering over the slice: (S - S*T) / extinction.
        vec3  sliceScat  = (s.rgb - s.rgb * sliceT) / extinction;

        accum         += transmittance * sliceScat;
        transmittance *= sliceT;

        imageStore(u_integrated, ivec3(col, z), vec4(accum, transmittance));
    }
}

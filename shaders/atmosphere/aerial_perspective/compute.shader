/**
 * The aerial-perspective volume (Hillaire 2020, section 5.4): for each froxel of the view, the
 * sky's air between the eye and it - the light it scatters toward the eye (rgb) and the mean of
 * its transmittance (a) - which shaders/fog.glsl lays over every surface.
 *
 * Froxels are the screen's tiles by aerialSliceToViewDepth's squared slices. Each marches from
 * the eye to its own depth, scaled by u_distanceScale, in more steps the deeper it lies.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE, local_size_z = 1) in;

layout(binding = BAKE_SLOT_TRANSMITTANCE)   uniform sampler2D u_transmittance;
layout(binding = BAKE_SLOT_MULTISCATTERING) uniform sampler2D u_multiScattering;
layout(binding = 0, rgba16f) uniform writeonly image3D u_aerialPerspective;

uniform vec3  u_sunDir;          // direction TO the sun, normalized
uniform vec3  u_sunIlluminance;  // above the air, times the sky's intensity as the skybox draws it
uniform float u_airDepth;        // view depth the slices reach
uniform float u_distanceScale;   // how much air a metre of the scene holds; 1 is the planet's

#include "../../depth.glsl"
#include "../../camera.glsl"
#include "../../atmosphere.glsl"

void main() {
    ivec3 froxel = ivec3(gl_GlobalInvocationID);
    ivec3 size   = imageSize(u_aerialPerspective);
    if (any(greaterThanEqual(froxel, size))) return;

    // The froxel's centre ray, and how far along it the slice's depth lies. An orthographic ray
    // runs straight down the axis.
    vec2  uv         = (vec2(froxel.xy) + 0.5) / vec2(size.xy);
    float viewDepth  = aerialSliceToViewDepth(float(froxel.z) + 0.5, float(size.z), u_airDepth);
    vec3  viewDir    = vec3(0.0, 0.0, -1.0);
    float pathLength = viewDepth;
    if (cameraIsPerspective()) {
        vec4 nearH = u_camera.invProjection * vec4(uv * 2.0 - 1.0, -1.0, 1.0);
        vec3 ray   = nearH.xyz / nearH.w;
        viewDir    = normalize(ray);
        pathLength = viewDepth * length(ray) / abs(ray.z);
    }
    vec3 dir = normalize(mat3(u_camera.invView) * viewDir);

    vec3 throughput;
    vec3 light = integrateAir(
        u_transmittance,
        u_multiScattering,
        ATMOSPHERE_EYE,
        dir,
        pathLength * u_distanceScale,
        2 * (froxel.z + 1),
        false,
        u_sunDir,
        throughput
    );
    imageStore(u_aerialPerspective, froxel, vec4(u_sunIlluminance * light, dot(throughput, vec3(1.0 / 3.0))));
}

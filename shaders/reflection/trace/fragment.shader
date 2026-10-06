/**
 * Reflection trace: where each glossy pixel's ray meets the scene, marched in
 * screen space against this frame's depth and bisected to the texel. The ray
 * reflects the interpolated G-buffer normal: one ray per pixel turns a normal
 * map into a speckle of hits and misses.
 *
 * Zero trust where the ray found nothing.
 */

#include "../../depth.glsl"
#include "../../camera.glsl"
#include "../../normal_codec.glsl"

out vec4 Hit;  // xy: hit uv; z: trust; w: chain level over the last (the target is unorm)

layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2D u_sceneGBuffer;
layout(binding = REFLECT_SLOT_WEIGHT)     uniform sampler2D u_weight;

uniform float u_maxRoughness;  // rougher pixels reflect only probes and sky
uniform float u_maxDistance;   // world length a ray is traced
uniform float u_maxLod;        // the colour chain's last level

#include "../reflection.glsl"

// A mirror takes the most steps; the roughness limit the fewest, since its
// blurred level cannot show a thin object a coarse march skips.
const int MAX_STEPS = 48;
const int MIN_STEPS = 16;
const int REFINE    = 6;

// View depth at screen fraction t, interpolated through 1/w so perspective is exact.
float rayDepth(float z0, float z1, float k0, float k1, float t) {
    return mix(z0, z1, t) / mix(k0, k1, t);
}

void main() {
    Hit = vec4(0.0);

    const ivec2 texel     = ivec2(gl_FragCoord.xy);
    const vec4  weight    = texelFetch(u_weight, texel, 0);
    const float roughness = weight.a;
    if (!tracesReflection(weight, u_maxRoughness)) return;

    const float depth = texelFetch(u_sceneDepth, texel, 0).r;
    if (depth >= 1.0 || !sceneHasNormal(texel)) return;

    const vec2 size  = vec2(textureSize(u_sceneDepth, 0));
    const vec2 uv    = (vec2(texel) + 0.5) / size;
    const vec4 clip  = u_camera.invProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    const vec3 n     = sceneNormalAt(texel);
    const vec3 toEye = cameraIsPerspective() ? normalize(-clip.xyz / clip.w) : vec3(0.0, 0.0, 1.0);
    const vec3 dir   = reflect(-toEye, n);

    // Lifted off its own surface by what the depth can resolve there.
    vec3 p0 = clip.xyz / clip.w;
    p0 += n * (0.01 + 0.002 * -p0.z);

    // Cut short of the near plane: a point behind the eye projects onto the
    // wrong side of the screen.
    const float near = u_camera.zNear;
    if (p0.z > -near) return;
    float len = u_maxDistance;
    if (dir.z > 0.0) len = min(len, (-near - p0.z) / dir.z * 0.99);
    const vec3 p1 = p0 + dir * len;

    const vec4  h0 = u_camera.projection * vec4(p0, 1.0);
    const vec4  h1 = u_camera.projection * vec4(p1, 1.0);
    const float k0 = 1.0 / h0.w;
    const float k1 = 1.0 / h1.w;
    const vec2  s0 = h0.xy * k0 * 0.5 + 0.5;
    const vec2  s1 = h1.xy * k1 * 0.5 + 0.5;
    const float z0 = -p0.z * k0;
    const float z1 = -p1.z * k1;

    // Stop where the ray leaves the picture.
    const vec2 delta = s1 - s0;
    float tEnd = 1.0;
    if (delta.x > 0.0) tEnd = min(tEnd, (1.0 - s0.x) / delta.x);
    if (delta.x < 0.0) tEnd = min(tEnd, -s0.x / delta.x);
    if (delta.y > 0.0) tEnd = min(tEnd, (1.0 - s0.y) / delta.y);
    if (delta.y < 0.0) tEnd = min(tEnd, -s0.y / delta.y);

    const ivec2 last   = ivec2(size) - 1;
    const float texels = length(delta * tEnd * size);
    const float budget = mix(float(MAX_STEPS), float(MIN_STEPS), roughness / u_maxRoughness);
    const int   steps  = int(clamp(texels, 1.0, budget));
    const float stepT  = tEnd / float(steps);

    float tPrev = 0.0;
    for (int i = 1; i <= MAX_STEPS; ++i) {
        if (i > steps) break;
        const float t = stepT * float(i);

        // In front of what the frame shows: keep going.
        if (rayDepth(z0, z1, k0, k1, t) <= sceneDepthAt(min(ivec2((s0 + delta * t) * size), last))) {
            tPrev = t;
            continue;
        }

        // Crossed since the last step: bisect to a texel. At a real surface the
        // gap closes; behind an edge (depth jumping past an object) it stays open.
        float lo = tPrev;
        float hi = t;
        for (int r = 0; r < REFINE; ++r) {
            const float mid      = 0.5 * (lo + hi);
            const ivec2 midTexel = min(ivec2((s0 + delta * mid) * size), last);
            if (rayDepth(z0, z1, k0, k1, mid) > sceneDepthAt(midTexel)) hi = mid;
            else                                                        lo = mid;
        }
        tPrev = t;

        const vec2  hitUV    = s0 + delta * hi;
        const ivec2 hitTexel = min(ivec2(hitUV * size), last);
        const float hitZ     = rayDepth(z0, z1, k0, k1, hi);
        const float gap      = hitZ - sceneDepthAt(hitTexel);
        const float step     = hitZ - rayDepth(z0, z1, k0, k1, lo);
        if (gap > 2.0 * abs(step) + hitZ * 0.004) continue;

        // A surface facing away is a back the frame does not show. One with no
        // normal is taken as it is.
        if (sceneHasNormal(hitTexel) && dot(sceneNormalAt(hitTexel), dir) > 0.0) continue;

        const vec3  hitP      = mix(p0 * k0, p1 * k1, hi) / mix(k0, k1, hi);
        const float travelled = length(hitP - p0);

        // The lobe widens with the GGX alpha and the distance travelled, in texels.
        const float perTexel = u_camera.projection[1][1] * 0.5 * size.y
            / (cameraIsPerspective() ? hitZ : 1.0);
        const float cone     = 2.0 * travelled * roughness * roughness * perTexel;
        const float lod      = clamp(log2(max(cone, 1.0)), 0.0, u_maxLod);

        // Faded at the screen edge and the ray's end so neither draws a line.
        const vec2 edge = min(hitUV, 1.0 - hitUV);
        float trust = smoothstep(0.0, 0.06, min(edge.x, edge.y));
        trust *= 1.0 - smoothstep(0.8, 1.0, travelled / u_maxDistance);
        Hit = vec4(hitUV, trust, lod / max(u_maxLod, 1.0));
        return;
    }
}

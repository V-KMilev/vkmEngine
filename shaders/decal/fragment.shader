/**
 * Projected decal - screen-space projection.
 *
 * Each covered pixel's world position, rebuilt from depth, is discarded outside the decal's unit
 * box, whose XY is the UV. It fades where the G-buffer normal turns from the projector and over the
 * last fifth of each half of the box's depth, so one crossing a corner ends softly, and is lit
 * diffusely as the surface under it: every light of its cluster through its shadow, the
 * environment - probes and volume - through GTAO.
 */

#include "../constants.glsl"
#include "../scene_gbuffer.glsl"
#include "../scene_ao.glsl"
#include "../depth.glsl"
#include "../camera.glsl"
#include "../area_light.glsl"    // the cluster's lights (lights.glsl)
#include "../shadows.glsl"       // each light's shadow
#include "../fog.glsl"           // fogAt
#include "../ambient.glsl"       // environmentIrradiance
#include "../material.glsl"      // the decal's albedo factor and maps

out vec4 FragColor;

// The decal's albedo map (rgb + alpha), if any.
layout(binding = MATERIAL_SLOT_ALBEDO)    uniform sampler2D u_decalAlbedo;
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;

uniform mat4 u_invModel;  // world -> decal local

uniform vec3  u_projDir;      // direction the decal projects along (world): its forward
uniform float u_angleFade;
uniform float u_opacity;

void main() {
    vec2  uv = gl_FragCoord.xy / u_camera.viewport;
    float d  = texture(u_sceneDepth, uv).r;
    if (isFarDepth(d)) discard;  // background: nothing to project onto

    vec3 worldPos = worldPosFromDepth(uv, d, u_camera.invViewProjection);

    vec3 local = (u_invModel * vec4(worldPos, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    vec3 viewN  = sceneNormalAt(uv);
    vec3 worldN = normalize(mat3(u_camera.invView) * viewN);

    float facing = dot(worldN, -u_projDir);
    float ends   = 1.0 - smoothstep(0.4, 0.5, abs(local.z));
    float fade   = smoothstep(0.0, max(u_angleFade, 1e-3), facing) * ends;
    if (fade <= 0.0) discard;

    // Each light of its cluster, diffusely, as the forward pass lights the surface beneath: shadowed
    // by the geometric normal the G-buffer holds, an area light by its form factor.
    float viewDepth    = linearizeViewDepth(d, u_camera.invProjection);
    float screenShadow = screenShadowAt(uv);
    int   ci           = clusterIndex(uv, viewDepth, u_camera.zNear, u_camera.zFar);
    vec3  direct       = vec3(0.0);
    uint  count        = u_clusters.clusters[ci].count;
    for (uint k = 0u; k < count; ++k) {
        Light light    = u_lights.lights[u_clusters.clusters[ci].indices[k]];
        vec3  lightCol = light.color.xyz * light.color.w;
        int   type     = int(light.position.w);

        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            direct += lightCol * areaDiffuse(light, type, worldPos, worldN);
            continue;
        }

        vec3  L;
        float atten = punctualAttenuation(light, type, worldPos, L);
        float ndotl = dot(worldN, L);
        if (atten <= 0.0 || ndotl <= 0.0) continue;
        atten *= surfaceShadow(type, int(light.spot.w), worldPos, worldN, ndotl, screenShadow);
        direct += lightCol * atten * ndotl / PI;
    }

    // Lambertian, as the forward pass shades the surface beneath; irradiance is stored over PI.
    vec4 decal = u_material.albedo;
    if (hasTex(MATERIAL_SLOT_ALBEDO)) decal *= texture(u_decalAlbedo, local.xy + 0.5);
    float ao      = aoAt(uv);
    // Read where irradianceVolumeLookup puts the surface; the G-buffer normal is the geometric one.
    vec3  lookup  = irradianceVolumeLookup(worldPos, worldN);
    vec3  ambient = environmentIrradiance(lookup, worldN) * multiBounceOcclusion(ao, decal.rgb);
    vec3  lit     = decal.rgb * (direct + ambient);

    // Carries the fog the forward pass gave the surface beneath, at the same depth.
    vec4 fog = fogAt(uv, viewDepth);
    FragColor = vec4(lit * fog.a + fog.rgb, decal.a * fade * u_opacity);
}

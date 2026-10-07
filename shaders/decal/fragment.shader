/**
 * Projected decal - screen-space projection.
 *
 * Each covered pixel's world position, rebuilt from depth, is discarded outside the decal's unit
 * box, whose XY is the UV. It fades where the G-buffer normal turns from the projector and over the
 * last fifth of each half of the box's depth, so one crossing a corner ends softly, and is lit
 * diffusely as the surface under it: the sun through its cascades, the environment through GTAO.
 */

#include "../constants.glsl"
#include "../normal_codec.glsl"
#include "../depth.glsl"
#include "../camera.glsl"
#include "../shadows.glsl"       // sampleCSMSoft
#include "../fog.glsl"           // fogAt
#include "../ambient.glsl"       // environmentIrradiance
#include "../material.glsl"      // the decal's albedo factor and maps

out vec4 FragColor;

// The decal's albedo map (rgb + alpha), if any.
layout(binding = MATERIAL_SLOT_ALBEDO)    uniform sampler2D u_decalAlbedo;
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2D u_sceneDepth;
layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2D u_sceneGBuffer;  // oct view-normal in rg
layout(binding = POST_SLOT_AO)            uniform sampler2D u_ao;            // GTAO factor in r
uniform int u_hasAO;  // 0 when the GTAO pass did not run this frame

uniform mat4 u_invModel;  // world -> decal local

uniform vec3  u_projDir;      // direction the decal projects along (world): its forward
uniform vec3  u_sunDir;       // direction TO the sun (world)
uniform vec3  u_sunColor;     // sun colour * intensity
uniform int   u_sunShadowed;  // 1 when the sun is the light the cascades were fitted for
uniform float u_angleFade;
uniform float u_opacity;

void main() {
    vec2  uv = gl_FragCoord.xy / u_camera.viewport;
    float d  = texture(u_sceneDepth, uv).r;
    if (d >= 1.0) discard;  // background: nothing to project onto

    vec3 worldPos = worldPosFromDepth(uv, d, u_camera.invViewProjection);

    vec3 local = (u_invModel * vec4(worldPos, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    vec3 viewN  = octDecode(texture(u_sceneGBuffer, uv).rg);
    vec3 worldN = normalize(mat3(u_camera.invView) * viewN);

    float facing = dot(worldN, -u_projDir);
    float ends   = 1.0 - smoothstep(0.4, 0.5, abs(local.z));
    float fade   = smoothstep(0.0, max(u_angleFade, 1e-3), facing) * ends;
    if (fade <= 0.0) discard;

    // Shadowed by the geometric normal the G-buffer holds, as the forward pass shadows the surface.
    float ndotl = dot(worldN, u_sunDir);
    float sun   = max(ndotl, 0.0);
    if (u_sunShadowed == 1 && sun > 0.0) {
        sun *= sampleCSMSoft(worldPos, worldN, ndotl);
    }

    // Lambertian, as the forward pass shades the surface beneath; irradiance is stored over PI.
    vec4 decal = u_material.albedo;
    if (hasTex(MATERIAL_SLOT_ALBEDO)) decal *= texture(u_decalAlbedo, local.xy + 0.5);
    float ao      = (u_hasAO == 1) ? texture(u_ao, uv).r : 1.0;
    // Read where irradianceVolumeLookup puts the surface; the G-buffer normal is the geometric one.
    vec3  lookup  = irradianceVolumeLookup(worldPos, worldN);
    vec3  ambient = environmentIrradiance(lookup, worldN) * multiBounceOcclusion(ao, decal.rgb);
    vec3  lit     = decal.rgb * (u_sunColor * sun / PI + ambient);

    // Carries the fog the forward pass gave the surface beneath, at the same depth.
    vec4 fog = fogAt(uv, linearizeViewDepth(d, u_camera.invProjection));
    FragColor = vec4(lit * fog.a + fog.rgb, decal.a * fade * u_opacity);
}

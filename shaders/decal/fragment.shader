/**
 * Projected decal - screen-space projection.
 *
 * For each pixel the decal's box covers: rebuild the world position from the
 * scene depth, transform it into the decal's unit box, and discard if it falls
 * outside. Inside, the box's XY becomes the decal UV. The surface normal comes
 * from the G-buffer, so the decal fades out where the surface turns away from the
 * projector (no smearing across perpendicular walls) and is lit by the sun.
 */

#include "../_common/normal_codec.glsl"  // signNotZero, octDecode
#include "../_common/depth.glsl"

out vec4 FragColor;

layout(binding = 0)  uniform sampler2D u_decalAlbedo;   // decal material albedo (rgb + alpha)
layout(binding = 19) uniform sampler2D u_sceneDepth;
layout(binding = 20) uniform sampler2D u_sceneGBuffer;  // oct view-normal in rg

uniform mat4  u_invModel;     // world -> decal local
uniform mat4  u_invViewProj;  // clip  -> world
uniform mat4  u_invView;      // view  -> world (for the G-buffer normal)
uniform vec2  u_screenSize;

uniform vec3  u_projDir;      // direction the decal projects along (world)
uniform vec3  u_sunDir;       // direction TO the sun (world)
uniform vec3  u_sunColor;     // sun colour * intensity
uniform float u_ambient;
uniform float u_angleFade;
uniform float u_opacity;

void main() {
    vec2  uv = gl_FragCoord.xy / u_screenSize;
    float d  = texture(u_sceneDepth, uv).r;
    if (d >= 1.0) discard;  // background: nothing to project onto

    // Rebuild the world position under this pixel.
    vec3 worldPos = worldPosFromDepth(uv, d, u_invViewProj);

    // Into the decal's unit box; outside means this pixel isn't decalled.
    vec3 local = (u_invModel * vec4(worldPos, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    // Surface normal from the G-buffer (stored octahedral, view space).
    vec3 viewN  = octDecode(texture(u_sceneGBuffer, uv).rg);
    vec3 worldN = normalize(mat3(u_invView) * viewN);

    // Fade out as the surface turns away from the projector.
    float facing = dot(worldN, -u_projDir);
    float fade   = smoothstep(0.0, max(u_angleFade, 1e-3), facing);
    if (fade <= 0.0) discard;

    vec4 decal = texture(u_decalAlbedo, local.xy + 0.5);
    vec3 lit   = decal.rgb * (u_sunColor * max(dot(worldN, u_sunDir), 0.0) + u_ambient);

    FragColor = vec4(lit, decal.a * fade * u_opacity);
}

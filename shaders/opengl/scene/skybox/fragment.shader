/**
 * Skybox fragment shader.
 *
 * The procedural sky from the sky-view table, plus what night adds to it; an HDR sky, and a
 * capture's, from the environment cubemap. Both as linear radiance; for the procedural sky the
 * sun disc, stars and moon the tables cannot hold are added analytically. The sky holds its own
 * air, so only the froxel fog lies over it, at the far plane, and the horizon fades into the fog;
 * with no sky it is the prepass's black, still fogged.
 */

#include "../fog.glsl"

in vec3 vDir;

out vec4 FragColor;

layout(binding = IBL_SLOT_ENV_CUBE) uniform samplerCube u_envCube;
layout(binding = SKY_SLOT_VIEW)     uniform sampler2D   u_skyViewLut;
uniform float u_iblIntensity;
uniform int   u_hasSky;   // 0 = no sky to show: the background is black, then fogged
uniform int   u_skyView;  // 1 = the procedural sky, from u_skyViewLut; 0 = the env cube

uniform int   u_hasSun;            // 1 = draw the analytic discs (procedural sky)
uniform vec3  u_sunDir;            // direction TO the sun, normalized
uniform float u_sunCosOuter;       // cos(angularRadius): disc edge
uniform float u_sunCosInner;       // cos(0.8 * angularRadius): fully-bright core
uniform float u_sunDiscIntensity;  // disc radiance
uniform vec3  u_sunColor;          // the disc's tint: the sunlight through the atmosphere

uniform vec3  u_moonDir;        // direction TO the moon, normalized
uniform float u_moonCosOuter;
uniform float u_moonCosInner;
uniform float u_moonIntensity;
uniform float u_moonHalo;       // radiance of the glow around the moon
uniform vec3  u_nightRadiance;  // the skyglow floor
uniform float u_starIntensity;  // 0 disables the star field
uniform float u_starDensity;

#include "../atmosphere.glsl"
#include "../sky.glsl"

// The sky-view table along @p dir: its azimuth is measured from the sun's, which overhead has
// none, and then any azimuth reads the same sky.
vec3 skyView(vec3 dir) {
    vec2  across     = dir.xz;
    vec2  sunAcross  = u_sunDir.xz;
    float lengths    = length(across) * length(sunAcross);
    float azimuthCos = lengths > 1e-6 ? dot(across, sunAcross) / lengths : 1.0;
    vec2  unit       = skyViewUnit(dir.y, azimuthCos);
    return textureLod(u_skyViewLut, lutUv(unit, SKY_VIEW_LUT_SIZE), 0.0).rgb;
}

void main() {
    vec3 dir   = normalize(vDir);
    vec3 color = vec3(0.0);
    if (u_hasSky == 1) {
        if (u_skyView == 1) {
            float night = skyNightFactor(u_sunDir);
            color = skyView(dir) + skyNightGlow(dir, u_moonDir, u_nightRadiance, u_moonHalo, night);
        } else {
            color = texture(u_envCube, dir).rgb;
        }
        color *= u_iblIntensity;
    }

    if (u_hasSky == 1 && u_hasSun == 1) {
        float night = skyNightFactor(u_sunDir);

        // Fades with the daylight, or it would burn through the night sky from below the horizon.
        float sun = skyDisc(dir, u_sunDir, u_sunCosOuter, u_sunCosInner);
        color += sun * (1.0 - night) * u_sunDiscIntensity * u_sunColor;

        if (night > 0.0) {
            // Not clipped at the horizon: that would draw attention to an edge the atmosphere
            // already softens.
            float stars = skyStarField(dir, u_starDensity);
            color += stars * skyStarFactor(u_sunDir) * u_starIntensity * vec3(0.92, 0.95, 1.0);

            float moon = skyDisc(dir, u_moonDir, u_moonCosOuter, u_moonCosInner);
            color += moon * night * u_moonIntensity * vec3(0.95, 0.95, 0.88);
        }
    }

    vec4 fog = localFogAt(
        gl_FragCoord.xy / u_camera.viewport,
        linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection)
    );
    FragColor = vec4(color * fog.a + fog.rgb, 1.0);
}

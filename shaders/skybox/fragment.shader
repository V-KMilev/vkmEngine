/**
 * Skybox fragment shader.
 *
 * Samples the baked environment cubemap and outputs LINEAR radiance into the
 * HDR target - the composite pass owns tonemap + gamma, so the sky tone-maps
 * consistently with lit geometry. For the procedural sky, the sharp things the
 * env cube cannot hold are added analytically on top: the sun disc by day, and
 * the stars and moon once the sun is down.
 */

in vec3 vDir;

out vec4 FragColor;

layout(binding = 17) uniform samplerCube u_envCube;
uniform float u_iblIntensity;

uniform int   u_hasSun;            // 1 = draw the analytic discs (procedural sky)
uniform vec3  u_sunDir;            // direction TO the sun, normalized
uniform float u_sunCosOuter;      // cos(angularRadius): disc edge
uniform float u_sunCosInner;      // cos(0.8 * angularRadius): fully-bright core
uniform float u_sunDiscIntensity; // disc radiance

uniform vec3  u_moonDir;           // direction TO the moon, normalized
uniform float u_moonCosOuter;
uniform float u_moonCosInner;
uniform float u_moonIntensity;
uniform float u_starIntensity;     // 0 disables the star field
uniform float u_starDensity;

#include "../_common/sky.glsl"

void main() {
    vec3 dir   = normalize(vDir);
    vec3 color = texture(u_envCube, dir).rgb * u_iblIntensity;

    if (u_hasSun == 1) {
        float night = skyNightFactor(u_sunDir);

        // The sun disc goes with the daylight that justifies it. Without this it
        // would keep burning through the night sky from below the horizon, where
        // there is no ground in the skybox to hide it.
        float sun = skyDisc(dir, u_sunDir, u_sunCosOuter, u_sunCosInner);
        color += sun * (1.0 - night) * u_sunDiscIntensity * vec3(1.0, 0.96, 0.9);

        if (night > 0.0) {
            // Stars sit behind the moon and below the horizon alike: the sky is
            // a backdrop, and clipping them at the horizon line would only draw
            // attention to an edge the atmosphere already softens.
            float stars = skyStarField(dir, u_starDensity);
            color += stars * night * u_starIntensity * vec3(0.92, 0.95, 1.0);

            float moon = skyDisc(dir, u_moonDir, u_moonCosOuter, u_moonCosInner);
            color += moon * night * u_moonIntensity * vec3(0.95, 0.95, 0.88);
        }
    }

    FragColor = vec4(color, 1.0);
}

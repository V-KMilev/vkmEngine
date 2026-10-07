/**
 * IBL bake - the procedural atmosphere (Hillaire 2020), as linear sky radiance for the cube
 * direction.
 *
 * The air's light along the view ray (skyRadiance in shaders/atmosphere.glsl), from the
 * atmosphere's two tables. The sun disc and stars are skybox/fragment.shader's (the stars are too
 * fine to survive the cube's mips); night adds only what lights the scene, the skyglow floor and
 * the moon's halo, so a night world is dark rather than black.
 */
in vec3 vLocalPos;

out vec4 FragColor;

layout(binding = BAKE_SLOT_TRANSMITTANCE)   uniform sampler2D u_transmittance;
layout(binding = BAKE_SLOT_MULTISCATTERING) uniform sampler2D u_multiScattering;

uniform vec3  u_sunDir;          // direction TO the sun, normalized
uniform vec3  u_sunIlluminance;  // above the air: the scene's sunlight before the air dims it
uniform vec3  u_nightRadiance;   // skyglow the scene is lit by once the sun is down
uniform vec3  u_moonDir;         // direction TO the moon, normalized
uniform float u_moonHalo;        // radiance of the glow immediately around the moon

#include "../../atmosphere.glsl"
#include "../../sky.glsl"

void main() {
    vec3 dir    = normalize(vLocalPos);
    vec3 sunDir = normalize(u_sunDir);

    vec3 color = u_sunIlluminance * skyRadiance(u_transmittance, u_multiScattering, dir, sunDir);
    color += skyNightGlow(dir, normalize(u_moonDir), u_nightRadiance, u_moonHalo, skyNightFactor(sunDir));
    FragColor = vec4(color, 1.0);
}

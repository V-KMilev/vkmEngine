/**
 * Skybox fragment shader.
 *
 * Samples the baked environment cubemap and outputs LINEAR radiance into the
 * HDR target - the composite pass owns tonemap + gamma, so the sky tone-maps
 * consistently with lit geometry. For the procedural sky, a crisp analytic sun
 * disc is added on top (the env cube's low resolution can't hold a sharp sun).
 */

in vec3 vDir;

out vec4 FragColor;

layout(binding = 17) uniform samplerCube u_envCube;
uniform float u_iblIntensity;

uniform int   u_hasSun;            // 1 = draw the analytic sun disc (procedural sky)
uniform vec3  u_sunDir;            // direction TO the sun, normalized
uniform float u_sunCosOuter;      // cos(angularRadius): disc edge
uniform float u_sunCosInner;      // cos(0.8 * angularRadius): fully-bright core
uniform float u_sunDiscIntensity; // disc radiance

void main() {
    vec3 dir = normalize(vDir);
    vec3 color = texture(u_envCube, dir).rgb * u_iblIntensity;

    if (u_hasSun == 1) {
        // Soft-edged disc: 1 inside the core, fading to 0 at the outer radius.
        float disc = smoothstep(u_sunCosOuter, u_sunCosInner, dot(dir, u_sunDir));
        color += disc * u_sunDiscIntensity * vec3(1.0, 0.96, 0.9);
    }

    FragColor = vec4(color, 1.0);
}

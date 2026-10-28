/**
 * The atmosphere's transmittance table.
 *
 * Each texel is a radius and a view angle (transmittanceUnit in shaders/atmosphere.glsl); it
 * holds the transmittance from there to the top of the atmosphere.
 */
in vec2 vUV;

out vec4 FragColor;

#include "../../atmosphere.glsl"

void main() {
    vec2  params = transmittanceParams(lutUnit(vUV, TRANSMITTANCE_LUT_SIZE));
    float r      = params.x;
    float mu     = params.y;

    vec3  origin = vec3(0.0, r, 0.0);
    vec3  dir    = vec3(sqrt(max(1.0 - mu * mu, 0.0)), mu, 0.0);
    float dt     = raySphere(origin, dir, ATMOSPHERE_TOP_RADIUS).y / float(ATMOSPHERE_TRANSMITTANCE_STEPS);

    vec3 depth = vec3(0.0);
    for (int i = 0; i < ATMOSPHERE_TRANSMITTANCE_STEPS; ++i) {
        vec3 p = origin + dir * (dt * (float(i) + 0.5));
        depth += airExtinction(airDensity(length(p) - ATMOSPHERE_PLANET_RADIUS)) * dt;
    }
    FragColor = vec4(exp(-depth), 1.0);
}

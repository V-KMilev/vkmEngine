/**
 * The atmosphere's multiple-scattering table (Hillaire 2020, section 5.5).
 *
 * Each texel is an altitude and the sun's zenith cosine. It holds Psi_ms: what every scattering
 * order past the first adds there, per unit of scattering, under a sun of unit illuminance. The
 * second order is gathered isotropically from a sphere of directions, and the orders past it are
 * that times the geometric series 1 / (1 - f_ms), f_ms being the share of light the air around
 * scatters back toward the texel.
 */
in vec2 vUV;

out vec4 FragColor;

layout(binding = BAKE_SLOT_TRANSMITTANCE) uniform sampler2D u_transmittance;

#include "../../atmosphere.glsl"

const int   SQRT_DIRECTIONS = 8;
const int   STEPS           = 20;
const float ISOTROPIC       = 1.0 / (4.0 * PI);

void main() {
    vec2  unit   = lutUnit(vUV, MULTISCATTERING_LUT_SIZE);
    float muSun  = unit.x * 2.0 - 1.0;
    vec3  sunDir = vec3(sqrt(max(1.0 - muSun * muSun, 0.0)), muSun, 0.0);
    vec3  origin = vec3(0.0, mix(ATMOSPHERE_PLANET_RADIUS, ATMOSPHERE_TOP_RADIUS, unit.y), 0.0);

    vec3 secondOrder = vec3(0.0);
    vec3 transfer    = vec3(0.0);
    for (int i = 0; i < SQRT_DIRECTIONS; ++i) {
        for (int j = 0; j < SQRT_DIRECTIONS; ++j) {
            // Uniform over the sphere: even in azimuth and in the cosine.
            float theta  = 2.0 * PI * (float(i) + 0.5) / float(SQRT_DIRECTIONS);
            float cosPhi = 1.0 - 2.0 * (float(j) + 0.5) / float(SQRT_DIRECTIONS);
            float sinPhi = sqrt(max(1.0 - cosPhi * cosPhi, 0.0));
            vec3  dir    = vec3(cos(theta) * sinPhi, cosPhi, sin(theta) * sinPhi);

            float ground = distanceToGround(origin, dir);
            float tMax   = ground >= 0.0 ? ground : raySphere(origin, dir, ATMOSPHERE_TOP_RADIUS).y;
            float dt     = max(tMax, 0.0) / float(STEPS);

            vec3 throughput = vec3(1.0);
            for (int k = 0; k < STEPS; ++k) {
                vec3  p          = origin + dir * (dt * (float(k) + 0.3));
                float r          = length(p);
                vec3  density    = airDensity(r - ATMOSPHERE_PLANET_RADIUS);
                vec3  scattering = airScattering(density);
                vec3  extinction = max(airExtinction(density), vec3(1e-30));
                vec3  stepT      = exp(-extinction * dt);
                // The step's transmittance integrated over its length, as the medium is constant in it.
                vec3  along      = throughput * (1.0 - stepT) / extinction;

                vec3 sun = distanceToGround(p, sunDir) >= 0.0
                    ? vec3(0.0)
                    : transmittanceToTop(u_transmittance, r, dot(p, sunDir) / r);
                secondOrder += along * scattering * ISOTROPIC * sun;
                transfer    += along * scattering;
                throughput  *= stepT;
            }
            if (ground >= 0.0) {
                vec3  up     = normalize(origin + dir * ground);
                float cosSun = dot(up, sunDir);
                vec3  sun    = transmittanceToTop(u_transmittance, ATMOSPHERE_PLANET_RADIUS, cosSun);
                secondOrder += throughput * sun * max(cosSun, 0.0) * ATMOSPHERE_GROUND_ALBEDO / PI;
            }
        }
    }

    // Averaged over the sphere, which the isotropic phase toward the texel weights evenly.
    float count = float(SQRT_DIRECTIONS * SQRT_DIRECTIONS);
    secondOrder /= count;
    transfer    /= count;
    FragColor = vec4(secondOrder / (1.0 - transfer), 1.0);
}

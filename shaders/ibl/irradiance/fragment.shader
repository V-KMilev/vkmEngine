/**
 * IBL bake - cosine-weighted diffuse irradiance convolution.
 *
 * Output is irradiance over pi - what a white Lambertian surface facing that way reflects - so a
 * reader multiplies it by the albedo alone. A draw sums the azimuths of slice u_slice of u_slices,
 * each over the whole sample count, so the slices added together are the face.
 */
in vec3 vLocalPos;

out vec4 FragColor;

layout(binding = BAKE_SLOT_SOURCE) uniform samplerCube u_envCube;
uniform int u_slice;
uniform int u_slices;

#include "../../constants.glsl"

const float SAMPLE_DELTA = 0.025;
const int   PHI_STEPS    = int(ceil(2.0 * PI / SAMPLE_DELTA));
const int   THETA_STEPS  = int(ceil(0.5 * PI / SAMPLE_DELTA));

void main() {
    vec3 N = normalize(vLocalPos);

    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    // Read at the level whose texel spans the step between samples, so a sun a few texels
    // wide is averaged in rather than hit or missed by the grid.
    float texelAngle = 0.5 * PI / float(textureSize(u_envCube, 0).x);
    float lod        = max(log2(SAMPLE_DELTA / texelAngle), 0.0);

    vec3 irradiance = vec3(0.0);
    for (int i = PHI_STEPS * u_slice / u_slices; i < PHI_STEPS * (u_slice + 1) / u_slices; ++i) {
        float phi = float(i) * SAMPLE_DELTA;
        for (int j = 0; j < THETA_STEPS; ++j) {
            float theta = float(j) * SAMPLE_DELTA;
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
            irradiance += textureLod(u_envCube, sampleVec, lod).rgb * cos(theta) * sin(theta);
        }
    }

    irradiance = PI * irradiance / float(PHI_STEPS * THETA_STEPS);
    FragColor = vec4(irradiance, 1.0 / float(u_slices));
}

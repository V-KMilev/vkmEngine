/**
 * IBL bake - cosine-weighted diffuse irradiance convolution.
 *
 * Output is irradiance over pi - what a white Lambertian surface facing that way reflects - so a
 * reader multiplies it by the albedo alone. Directions are drawn by the cosine itself, so each
 * sample counts the same, and each reads the level whose texel covers its share of the
 * hemisphere (Colbert and Krivanek, GPU Gems 3 ch. 20): a sun a few texels wide is averaged in
 * rather than hit or missed.
 */
in vec3 vLocalPos;

out vec4 FragColor;

layout(binding = BAKE_SLOT_SOURCE) uniform samplerCube u_envCube;

#include "../../constants.glsl"
#include "../../sampling.glsl"

const uint SAMPLE_COUNT = 1024u;

void main() {
    vec3 N = normalize(vLocalPos);

    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float envRes  = float(textureSize(u_envCube, 0).x);
    float saTexel = 4.0 * PI / (6.0 * envRes * envRes);

    vec3 irradiance = vec3(0.0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2  xi       = hammersley(i, SAMPLE_COUNT);
        float phi      = 2.0 * PI * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);
        float sinTheta = sqrt(xi.y);
        vec3  L = (cos(phi) * sinTheta) * right + (sin(phi) * sinTheta) * up + cosTheta * N;

        // pdf = cos / pi.
        float pdf      = max(cosTheta, 1e-4) / PI;
        float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf);
        float mip      = 0.5 * log2(saSample / saTexel);
        irradiance += textureLod(u_envCube, L, max(mip, 0.0)).rgb;
    }

    // The integral of L cos over the hemisphere is pi times the samples' mean; over pi, the mean.
    FragColor = vec4(irradiance / float(SAMPLE_COUNT), 1.0);
}

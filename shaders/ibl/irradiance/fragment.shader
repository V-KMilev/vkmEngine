/**
 * IBL bake - cosine-weighted diffuse irradiance convolution.
 *
 * Integrates the environment cubemap over the hemisphere about the cube
 * direction. Output is the irradiance the split-sum diffuse term samples.
 */

in vec3 vLocalPos;

out vec4 FragColor;

uniform samplerCube u_envCube;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vLocalPos);

    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3  irradiance = vec3(0.0);
    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),
                                      sin(theta) * sin(phi),
                                      cos(theta));
            vec3 sampleVec = tangentSample.x * right
                           + tangentSample.y * up
                           + tangentSample.z * N;
            irradiance += texture(u_envCube, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }

    irradiance = PI * irradiance * (1.0 / nrSamples);
    FragColor = vec4(irradiance, 1.0);
}

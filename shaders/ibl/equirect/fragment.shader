/**
 * IBL bake - equirectangular HDR projected onto a cubemap face.
 *
 * Samples the source equirect texture by the spherical mapping of the cube
 * direction. Linear in, linear out (no tone mapping anywhere in the engine
 * except the composite pass).
 */

in vec3 vLocalPos;

out vec4 FragColor;

uniform sampler2D u_equirect;

const vec2 invAtan = vec2(0.1591, 0.3183);  // 1/(2pi), 1/pi

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = sampleSphericalMap(normalize(vLocalPos));
    FragColor = vec4(texture(u_equirect, uv).rgb, 1.0);
}

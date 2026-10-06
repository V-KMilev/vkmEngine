/**
 * IBL bake - equirectangular HDR projected onto a cubemap face.
 *
 * Samples the equirect by the cube direction's spherical mapping; linear in, linear out.
 */
in vec3 vLocalPos;

out vec4 FragColor;

layout(binding = BAKE_SLOT_SOURCE) uniform sampler2D u_equirect;

#include "../../constants.glsl"

// Longitude and latitude, in radians, to the texture's 0..1 across each.
const vec2 INV_ATAN = vec2(0.5 / PI, 1.0 / PI);

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= INV_ATAN;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = sampleSphericalMap(normalize(vLocalPos));

    // The longitude seam is taken out of the footprint: u jumps a whole turn across it, which
    // would pick the coarsest level along that column.
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    dx.x -= round(dx.x);
    dy.x -= round(dy.x);
    FragColor = vec4(textureGrad(u_equirect, uv, dx, dy).rgb, 1.0);
}

/**
 * Hi-Z init: writes view-space distance from camera (-pos.z) into the
 * pyramid's mip 0. Sampled from the prepass view-space position MRT,
 * which is GL_RGBA16F with xyz = view-space position.
 *
 * Conservative-far convention: stores larger numbers for fragments
 * farther from the camera, so a max-reduce in subsequent mips gives
 * the farthest depth in each 2x2 footprint.
 */
#version 420 core

in vec2 vUV;
out float FragDistance;

uniform sampler2D u_viewPos;

void main() {
    // View-space Z is negative inside the camera frustum; negate so the
    // pyramid stores positive distance-from-camera. Sky / cleared pixels
    // (pos.z == 0) end up at 0; that's fine because the max-reduce will
    // ignore them in favour of any non-cleared neighbours.
    vec3 vp = texture(u_viewPos, vUV).xyz;
    FragDistance = -vp.z;
}

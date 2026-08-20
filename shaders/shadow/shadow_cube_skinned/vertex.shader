// Point-light cube shadow depth for skinned geometry. The fragment stage writes
// linear distance-to-light, so the posed world position is passed down. See
// shadow_2d_skinned for why the palette base is a uniform.
layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aModel;  // per-instance model matrix (loc 4-7, divisor 1)
#include "../../_common/skinning.glsl"

uniform mat4 u_faceVP;
uniform uint u_skinBase;

out vec3 vWorldPos;

void main() {
    vec4 wp   = skinnedWorldPosition(aModel, u_skinBase);
    vWorldPos = wp.xyz;
    gl_Position = u_faceVP * wp;
}

// Depth + G-buffer prepass: lay down opaque depth so the forward pass can
// early-Z, and write a view-space normal for the G-buffer. Position must be bit-identical
// to the forward vertex shader (same math + invariant) so the winning depth
// matches under LEQUAL.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 4) in mat4 aModel;         // per-instance model matrix (loc 4-7, divisor 1)
layout (location = 8) in mat4 aNormalMatrix;  // per-instance normal matrix (loc 8-11; mat3 used)

#include "../../_common/camera.glsl"

uniform mat4 u_view;   // world -> view, for the G-buffer normal

out vec3 vViewNormal;

invariant gl_Position;

void main() {
    vec4 worldPos = aModel * vec4(aPos, 1.0);

    // World normal via the per-instance normal matrix, then into view space for the G-buffer.
    vec3 worldN = mat3(aNormalMatrix) * aNormal;
    vViewNormal = mat3(u_view) * worldN;

    gl_Position = u_camera.viewProjection * worldPos;
}

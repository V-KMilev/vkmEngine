// Depth + G-buffer prepass: lay down opaque depth so the forward pass can
// early-Z, and write a view-space normal for the G-buffer. Position must be bit-identical
// to the forward vertex shader (same math + invariant) so the winning depth
// matches under LEQUAL.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
#include "../../_common/instancing.glsl"
#include "../../_common/instancing_normal.glsl"
#include "../../_common/camera.glsl"

uniform mat4 u_view;   // world -> view, for the G-buffer normal

out vec3 vViewNormal;

invariant gl_Position;

void main() {
    vec4 worldPos = instanceModel() * vec4(aPos, 1.0);

    // World normal via the per-instance normal matrix, then into view space for the G-buffer.
    vec3 worldN = instanceNormalMatrix() * aNormal;
    vViewNormal = mat3(u_view) * worldN;

    gl_Position = u_camera.viewProjection * worldPos;
}

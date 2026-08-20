// Depth + G-buffer prepass for skinned geometry. Position must be bit-identical
// to shaders/forward/pbr_skinned (same expression + invariant) so the winning
// depth matches under LEQUAL.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
#include "../../_common/instancing.glsl"
#include "../../_common/instancing_normal.glsl"
#include "../../_common/skinning.glsl"
#include "../../_common/skinning_instanced.glsl"
#include "../../_common/camera.glsl"

uniform mat4 u_view;   // world -> view, for the G-buffer normal

out vec3 vViewNormal;

invariant gl_Position;

void main() {
    const uint base = instanceSkinBase();

    vec4 worldPos = skinnedWorldPosition(instanceModel(), base);

    // Posed world normal, then into view space for the G-buffer - so GTAO and
    // the decal pass see the character's actual surface, not its bind pose.
    vec3 worldN = instanceNormalMatrix() * (mat3(skinMatrix(base)) * aNormal);
    vViewNormal = mat3(u_view) * worldN;

    gl_Position = u_camera.viewProjection * worldPos;
}

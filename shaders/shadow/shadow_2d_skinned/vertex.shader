// Depth-only shadow pass for skinned geometry: pose the vertex, then project it
// into the light's clip space. The model matrix still arrives per-instance at
// locations 4-7; the palette base arrives as a uniform, because this pass reads
// its transforms as attributes and gl_InstanceID does not include baseInstance
// before GL 4.6 - so the pass draws skinned casters one at a time.
layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aModel;  // per-instance model matrix (loc 4-7, divisor 1)
#include "../../_common/skinning.glsl"

uniform mat4 u_lightVP;
uniform uint u_skinBase;

void main() {
    // The same expression the camera path uses, so a character's shadow is cast
    // by the geometry the camera sees rather than by something near it.
    gl_Position = u_lightVP * skinnedWorldPosition(aModel, u_skinBase);
}

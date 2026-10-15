// Irradiance volume surfels: the scene as GLSceneCapture batches it, with what its materials read.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
layout(location = ATTR_UV) in vec2 aUV;

#include "../../instancing.glsl"
#include "../../camera.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    const mat4 model    = instanceModel();
    const vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos   = worldPos.xyz;
    vNormal     = normalize(normalMatrix(model) * aNormal);
    vUV         = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}

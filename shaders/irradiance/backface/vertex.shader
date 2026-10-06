// Irradiance bake backface mask: position only, instanced as GLSceneCapture
// batches the scene.
layout(location = ATTR_POSITION) in vec3 aPos;

#include "../../instancing.glsl"
#include "../../camera.glsl"

void main() {
    gl_Position = u_camera.viewProjection * instanceModel() * vec4(aPos, 1.0);
}

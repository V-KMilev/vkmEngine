// An outline's rim (GLOutlinePass): the mesh with its surface pushed out along its normal, as the
// eye sees it, by u_width pixels of the window however far off it is and whichever way it runs.
// SKINNED (the skinned outline) poses position and normal by the bone palette first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
#include "../../include/instancing.glsl"
#include "../../include/camera.glsl"
#include "../../include/viewmodel.glsl"

uniform vec2  u_pixel;  // a pixel of the window in clip units at w = 1, across and up
uniform float u_width;  // the push in pixels; 0 draws the mesh itself

void main() {
    const vec4 world = instanceWorldPosition();
#ifdef SKINNED
    const vec3 normal = mat3(skinMatrix(instanceSkinBase())) * aNormal;
#else
    const vec3 normal = aNormal;
#endif
    vec4 clip = eyeClip(world);

    // Which way the normal runs on the screen, in pixels, from the point's clip position and the
    // normal's: a direction measured in clip units would stretch with the window's aspect.
    const vec4  along    = u_camera.viewProjection * vec4(normalMatrix(instanceModel()) * normal, 0.0);
    const vec2  inPixels = (along.xy * clip.w - clip.xy * along.w) / u_pixel;
    const float reach    = dot(inPixels, inPixels);
    const vec2  away     = reach > 0.0 ? inPixels * inversesqrt(reach) : vec2(0.0);
    clip.xy += away * u_width * u_pixel * clip.w;
    gl_Position = clip;
}

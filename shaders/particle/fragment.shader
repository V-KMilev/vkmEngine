/**
 * Billboard particle - fragment stage.
 *
 * A procedural soft round sprite, or a frame of the emitter's texture tinted by the particle's
 * colour, in linear radiance, fogged at its own depth.
 * Soft (Lorach 2007): it fades over its authored size (half its width) where the scene behind
 * comes near, so smoke meets a wall or the floor without a line, and over half a metre from the
 * near plane, so a sprite the camera passes through does not pop.
 * It writes zero into the reflection inputs at its own opacity, so blending dims the reflection
 * behind exactly as it dims the surface, and the additive half adds nothing to either.
 */

#include "../fog.glsl"
#include "../depth.glsl"

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // a copy of the scene's
layout(binding = PARTICLE_SLOT_SPRITE)  uniform sampler2D u_sprite;      // read while u_textured

uniform int u_textured;  // 1 while the run shows a texture, 0 for the soft disc

in vec2  vCorner;
in vec2  vUV;
flat in vec4 vFrame;
in vec4  vColor;
in float vSoftness;
in float vSize;

layout(location = OUT_COLOR)          out vec4 FragColor;
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV)    out vec4 ReflectEnv;

uniform int u_additive;  // 1 while the additive half draws: it adds light, so the fog only takes from it

void main() {
    vec4  color   = vColor;
    float falloff = 1.0;
    if (u_textured == 1) {
        // Held half a texel of the coarser level read inside the frame, so a filtered read takes
        // nothing of the frame beside it - down to the level whose texels straddle two frames,
        // which hold both already.
        vec2 inset = 0.5 * exp2(ceil(textureQueryLod(u_sprite, vUV).x)) / vec2(textureSize(u_sprite, 0));
        inset = min(inset, vFrame.zw * 0.5);
        color *= texture(u_sprite, clamp(vUV, vFrame.xy + inset, vFrame.xy + vFrame.zw - inset));
    } else {
        float r = length(vCorner);
        if (r > 1.0) discard;
        // Softness 1 fades over the whole disc, 0 keeps a thin anti-aliased rim.
        float inner = 1.0 - max(vSoftness, 0.04);
        falloff = 1.0 - smoothstep(inner, 1.0, r);
    }

    float sceneDepth = texelFetch(u_sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    float sceneView  = linearSceneDepth(sceneDepth, u_camera.invProjection);
    float ownView    = linearSceneDepth(gl_FragCoord.z, u_camera.invProjection);
    falloff *= clamp((sceneView - ownView) / max(vSize, 1e-3), 0.0, 1.0);
    falloff *= clamp((ownView - u_camera.zNear) / 0.5, 0.0, 1.0);

    vec4 fog = fragmentFog();
    vec3 rgb = color.rgb * fog.a + (u_additive == 1 ? vec3(0.0) : fog.rgb);
    FragColor = vec4(rgb, color.a * falloff);

    ReflectWeight = vec4(0.0, 0.0, 0.0, FragColor.a);
    ReflectEnv    = vec4(0.0, 0.0, 0.0, FragColor.a);
}

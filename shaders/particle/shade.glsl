/*
 * What a particle fragment writes, billboard or strip: its color emitted (additive) or lit
 * (alpha) and fogged at its own depth, or the scene behind it bent (distortion).
 *
 * Soft (Lorach 2007): it fades over its size where the scene behind comes near, so smoke meets a
 * wall or the floor without a line, and over half a metre from the near plane, so a sprite the
 * camera passes through does not pop. A lit or emitting one writes zero into the reflection inputs
 * at its own opacity, so blending dims the reflection behind exactly as it dims the surface - an
 * additive one adds nothing to either. A bending one leaves them be: what it bends is the frame,
 * and the reflection still lands where the surface is.
 */

#include "../fog.glsl"
#include "../depth.glsl"

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // a copy of the scene's depth
layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_sceneColor;  // read while distorting
layout(binding = PARTICLE_SLOT_SPRITE) uniform sampler2D u_sprite;       // read while u_textured

uniform int u_blend;     // PARTICLE_*: ParticleBlend, how the run being drawn blends
uniform int u_textured;  // 1 while the run shows a texture, 0 for the soft shape

// How much shows at @p reach out from the middle (1 the edge): softness 1 fades over the whole
// way, 0 keeps a thin anti-aliased rim.
float edgeFalloff(float softness, float reach) {
    return 1.0 - smoothstep(1.0 - max(softness, 0.04), 1.0, reach);
}

// A distortion texture's texel as the way it bends, x and y in its own frame; a Normal-usage
// texture holds them in red and green.
vec2 bendOf(vec4 texel) {
    return texel.rg * 2.0 - 1.0;
}

layout(location = OUT_COLOR)          out vec4 FragColor;
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV)    out vec4 ReflectEnv;

// How much of the particle shows for the scene behind it and the near plane; @p size is how deep
// its fade runs. Not on the viewmodel layer: there its depth and the weapon's are both the
// squashed band about the near plane, which would fade it to nothing.
float softFade(float size, bool viewmodel) {
    if (viewmodel) return 1.0;
    float sceneDepth = texelFetch(u_sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    float sceneView  = linearSceneDepth(sceneDepth, u_camera.invProjection);
    float ownView    = linearSceneDepth(gl_FragCoord.z, u_camera.invProjection);
    return clamp((sceneView - ownView) / max(size, 1e-3), 0.0, 1.0)
        * clamp((ownView - u_camera.zNear) / 0.5, 0.0, 1.0);
}

// Write @p rgb at @p alpha, fogged: an emitting particle's light only fades through the fog, a lit
// one takes the fog's own light as a surface does.
void writeParticle(vec3 rgb, float alpha) {
    vec4 fog = fragmentFog();
    FragColor     = vec4(rgb * fog.a + (u_blend == PARTICLE_ADDITIVE ? vec3(0.0) : fog.rgb), alpha);
    ReflectWeight = vec4(0.0, 0.0, 0.0, alpha);
    ReflectEnv    = vec4(0.0, 0.0, 0.0, alpha);
}

// Write the scene behind, read @p offset away - in the view's right and up, as a share of its
// height - at @p alpha. A textured bend's alpha is as strong as its @p lean, so where its texture
// is flat it writes nothing over a bend drawn before it.
void writeBent(vec2 lean, vec2 offset, float alpha) {
    float mask = u_textured == 1 ? min(length(lean), 1.0) : 1.0;
    vec2  size = vec2(textureSize(u_sceneColor, 0));
    vec2  uv   = (gl_FragCoord.xy + offset * size.y) / size;
    FragColor     = vec4(texture(u_sceneColor, clamp(uv, 0.0, 1.0)).rgb, alpha * mask);
    ReflectWeight = vec4(0.0);
    ReflectEnv    = vec4(0.0);
}

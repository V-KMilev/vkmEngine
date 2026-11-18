/**
 * The particles drawn at half resolution, over the scene (GLParticlePass): premultiplied, blended
 * as one more particle, dimming the reflection inputs as a particle does. A pixel takes the
 * bilinear blend of the four texels round it or, where one's depth parts from its own - a
 * silhouette - the texel nearest its depth, so a particle neither bleeds onto the surface in front
 * nor leaves a gap beside it (Bavoil's nearest-depth upsampling). Where none was drawn it writes
 * nothing.
 */

#include "../../../include/camera.glsl"
#include "../../../include/depth.glsl"

layout(binding = POST_SLOT_SCENE_DEPTH)     uniform sampler2D u_sceneDepth;  // the full-resolution copy
layout(binding = PARTICLE_SLOT_HALF_COLOR) uniform sampler2D u_particles;   // premultiplied
layout(binding = PARTICLE_SLOT_HALF_DEPTH) uniform sampler2D u_halfDepth;   // what they were drawn over

layout(location = OUT_COLOR)          out vec4 FragColor;
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV)    out vec4 ReflectEnv;

// A texel further in depth than this share of the pixel's own lies across an edge from it.
const float EDGE = 0.1;

void main() {
    const float drawn = texelFetch(u_sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    const float own   = linearSceneDepth(drawn, u_camera.invProjection);
    const vec2  at    = gl_FragCoord.xy * 0.5 - 0.5;
    const ivec2 base  = ivec2(floor(at));
    const vec2  f     = at - vec2(base);
    const ivec2 last  = textureSize(u_particles, 0) - 1;

    vec4  blended = vec4(0.0);
    vec4  nearest = vec4(0.0);
    float closest = SKY_DEPTH * 2.0;
    bool  edge    = false;
    for (int i = 0; i < 4; ++i) {
        const ivec2 side  = ivec2(i & 1, i >> 1);
        const ivec2 texel = clamp(base + side, ivec2(0), last);
        const vec4  shown = texelFetch(u_particles, texel, 0);
        const float depth = linearSceneDepth(texelFetch(u_halfDepth, texel, 0).r, u_camera.invProjection);
        const float gap   = abs(depth - own);
        const vec2  w     = mix(1.0 - f, f, vec2(side));
        blended += shown * (w.x * w.y);
        edge = edge || gap > EDGE * own;
        if (gap < closest) {
            closest = gap;
            nearest = shown;
        }
    }

    const vec4 particles = edge ? nearest : blended;
    if (particles == vec4(0.0)) discard;
    FragColor     = particles;
    ReflectWeight = vec4(0.0, 0.0, 0.0, particles.a);
    ReflectEnv    = vec4(0.0, 0.0, 0.0, particles.a);
}

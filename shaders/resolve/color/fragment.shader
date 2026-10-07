/**
 * Colour resolve: the multisample lit colour and reflection inputs, one value per pixel.
 *
 * Samples are weighted by 1 / (1 + brightest channel) as the viewer will see it, after the
 * exposure (Karis), so one bright highlight sample cannot outweigh the pixel and leave its edge
 * unantialiased - a saturated blue one too, which luma would barely weigh. The reflection
 * inputs take the same weights: the reflection resolve (reflection/resolve/fragment.shader) swaps
 * the environment reflection inside the colour for the traced one, and any other average leaves
 * a fringe along a silhouette. With u_reflectInputs 0 the two outputs go to no draw buffer.
 *
 * Depth takes sample 0, as resolve/geometry does, for what wrote depth since that resolve (see
 * GLResolvePass for when the write lands).
 */

#include "../../color.glsl"

layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2DMS u_color;
layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2DMS u_depth;
layout(binding = REFLECT_SLOT_WEIGHT)   uniform sampler2DMS u_weight;
layout(binding = REFLECT_SLOT_ENV)      uniform sampler2DMS u_env;

uniform int   u_samples;        // the multisample target's sample count
uniform float u_exposure;       // 2^EV the composite scales the frame by
uniform int   u_reflectInputs;  // 1 while the target carries them, for the reflection pass

layout(location = OUT_COLOR)          out vec4 Color;
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV)    out vec4 ReflectEnv;

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);

    vec3  color  = vec3(0.0);
    float alpha  = 0.0;
    float total  = 0.0;
    vec4  weight = vec4(0.0);
    vec3  env    = vec3(0.0);
    for (int i = 0; i < u_samples; ++i) {
        const vec4  c = texelFetch(u_color, texel, i);
        const float w = 1.0 / (1.0 + u_exposure * max(max(c.r, c.g), max(c.b, 0.0)));
        color += c.rgb * w;
        total += w;
        alpha += c.a;
        if (u_reflectInputs == 1) {
            weight += texelFetch(u_weight, texel, i) * w;
            env    += texelFetch(u_env, texel, i).rgb * w;
        }
    }

    Color         = vec4(color / total, alpha / float(u_samples));
    ReflectWeight = weight / total;
    ReflectEnv    = vec4(env / total, 1.0);
    gl_FragDepth  = texelFetch(u_depth, texel, 0).r;
}

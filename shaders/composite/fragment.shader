in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D u_hdr;    // linear HDR scene
layout(binding = 1) uniform sampler2D u_bloom;  // bloom mip 0 (energy-conserving chain)
uniform float u_bloomStrength;                  // 0 when bloom is unavailable

// Debug-view inputs. Only sampled when u_renderMode != MODE_DEFAULT, so the
// backend binds them only then; in the default path these stay untouched.
layout(binding = 19) uniform sampler2D u_sceneDepth;     // scene depth
layout(binding = 20) uniform sampler2D u_sceneGBuffer;   // oct view-normal.xy, roughness.z, metalness.w
layout(binding = 21) uniform sampler2D u_ao;             // GTAO factor
// Deliberately a plain sampler2D, not the sampler2DShadow the lighting shaders
// declare: this view shows stored depth rather than a compare result. The pass
// binds the atlas through GLShadowAtlas::bind2DRaw, which clears the texture's
// comparison mode - sampling it with a non-shadow sampler while that mode is
// set is undefined.
layout(binding = 11) uniform sampler2D u_shadowAtlas;    // tiled 2D shadow depth
layout(binding = 24) uniform sampler3D u_fog;            // integrated froxel fog
uniform int  u_hasAO;        // 0 when GTAO is off and nothing wrote the AO target
uniform int  u_renderMode;   // 0 = final image, else a debug buffer (see MODE_* below)
uniform mat4 u_projection;   // camera projection, for depth linearization (debug only)

#include "../_generated/render_modes.glsl"  // MODE_*, generated from the RenderMode enum

#include "../_common/normal_codec.glsl"  // signNotZero, octDecode
#include "../_common/depth.glsl"

// Visualize one intermediate render target, raw (no tonemap).
vec3 debugColor(vec2 uv) {
    if (u_renderMode == MODE_NORMALS)   return octDecode(texture(u_sceneGBuffer, uv).rg) * 0.5 + 0.5;
    if (u_renderMode == MODE_ROUGHNESS) return vec3(texture(u_sceneGBuffer, uv).b);
    if (u_renderMode == MODE_METALNESS) return vec3(texture(u_sceneGBuffer, uv).a);
    if (u_renderMode == MODE_AMBIENT_OCCLUSION)        return vec3(u_hasAO != 0 ? texture(u_ao, uv).r : 1.0);
    if (u_renderMode == MODE_BLOOM)     return texture(u_bloom, uv).rgb;
    if (u_renderMode == MODE_SHADOW_ATLAS)    return vec3(texture(u_shadowAtlas, uv).r);
    if (u_renderMode == MODE_FOG) {
        // In-scattered fog at each pixel's depth: the same slice mapping the
        // fog-apply pass uses, with near/far derived from the projection.
        float lin  = linearizeViewDepth(texture(u_sceneDepth, uv).r, u_projection);
        float near = u_projection[3][2] / (u_projection[2][2] - 1.0);
        float far  = u_projection[3][2] / (u_projection[2][2] + 1.0);
        float w    = viewDepthToSlice(min(lin, far), near, far, 1.0);
        return texture(u_fog, vec3(uv, w)).rgb;
    }
    if (u_renderMode == MODE_DEPTH) {
        // Positive linear view depth (the shared two-coefficient form), then
        // log-mapped between near and far: a plain lin/far divide crushes all
        // geometry to black when the far plane is large. Near = bright, far = dark.
        float lin  = linearizeViewDepth(texture(u_sceneDepth, uv).r, u_projection);
        float near = u_projection[3][2] / (u_projection[2][2] - 1.0);
        float far  = u_projection[3][2] / (u_projection[2][2] + 1.0);
        float t    = log2(lin / near) / log2(far / near);
        return vec3(clamp(1.0 - t, 0.0, 1.0));
    }
    return texture(u_hdr, uv).rgb;
}

// Bloom-blend + tonemap + gamma a linear HDR sample to perceptual LDR.
vec3 resolve(vec2 uv) {
    vec3 c = texture(u_hdr, uv).rgb;
    c = mix(c, texture(u_bloom, uv).rgb, u_bloomStrength);  // bloom in linear HDR
    c = c / (c + vec3(1.0));        // Reinhard tonemap
    return pow(c, vec3(1.0 / 2.2)); // gamma to the LDR backbuffer
}

void main() {
    // Debug views bypass tonemap and show the raw buffer - except the shading
    // splits (GI/direct/clusters), which the forward pass already wrote into
    // the scene as radiance, so they take the normal tonemap path.
    if (u_renderMode != MODE_DEFAULT && u_renderMode != MODE_GI_ONLY
        && u_renderMode != MODE_DIRECT_ONLY && u_renderMode != MODE_CLUSTERS) {
        FragColor = vec4(debugColor(vUV), 1.0);
        return;
    }

    FragColor = vec4(resolve(vUV), 1.0);
}

/**
 * Temporal anti-aliasing - camera-reprojection accumulation.
 *
 * Reprojects the prior frame's accumulation via the previous view-projection
 * (using this frame's view-space position from the prepass G-buffer), clamps
 * it to the 3x3 neighbourhood of the current colour to suppress ghosting,
 * then blends. No projection jitter (MSAA already does spatial edge AA) and
 * no per-object velocity, so this is temporal stabilisation / screen-space
 * denoise rather than full jittered TAA - moving objects can ghost.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_current;   // resolved HDR this frame
uniform sampler2D u_history;   // previous TAA accumulation
uniform sampler2D u_viewPos;   // prepass view-space position

uniform mat4  u_invView;       // this frame: view -> world
uniform mat4  u_prevViewProj;  // previous frame: world -> clip
uniform float u_blend;         // history weight (e.g. 0.9)
uniform int   u_primed;        // 0 = no valid history yet

void main() {
    vec3 cur = texture(u_current, vUV).rgb;

    if (u_primed == 0) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec3 vp = texture(u_viewPos, vUV).xyz;
    if (dot(vp, vp) < 1e-8) {            // background: keep current
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec4 world = u_invView * vec4(vp, 1.0);
    vec4 pc = u_prevViewProj * world;
    if (pc.w <= 0.0) {
        FragColor = vec4(cur, 1.0);
        return;
    }
    vec2 prevUV = (pc.xy / pc.w) * 0.5 + 0.5;
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec3 hist = texture(u_history, prevUV).rgb;

    // Neighbourhood clamp - reject reprojected history that strays outside
    // the local colour range (disocclusion / motion -> reduces ghosting).
    vec2 texel = 1.0 / vec2(textureSize(u_current, 0));
    vec3 mn = cur;
    vec3 mx = cur;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec3 c = texture(u_current, vUV + vec2(x, y) * texel).rgb;
            mn = min(mn, c);
            mx = max(mx, c);
        }
    }
    hist = clamp(hist, mn, mx);

    FragColor = vec4(mix(cur, hist, u_blend), 1.0);
}

/*
 * UI fragment: solids and text share one program and draw call, alpha-blended over the scene.
 *   Solid - the vertex carries the quad's size, so corners round by a signed-distance box and a
 *           border is the band inside that distance. With neither it is a flat tint, interpolated
 *           top to bottom for a gradient; an image solid is the picture times the tint.
 *   Text  - a corner radius of UI_TEXT_MARK. The atlas stores distance to the edge (0.5 on it);
 *           divided by its change per screen pixel, the edge anti-aliases over exactly one pixel.
 */
in vec2 vUV;
in vec4 vColor;
in vec4 vShape;    // xy = quad size in pixels, z = corner radius, w = border width
in vec4 vBorder;
flat in float vImage;

out vec4 FragColor;

layout(binding = UI_SLOT_ATLAS) uniform sampler2D u_tex;
layout(binding = UI_SLOT_IMAGE) uniform sampler2D u_image;  // the run's image; rows bottom-up
uniform int u_imageSrgb;  // it is stored as sRGB, so sampling returns linear light

#include "../color.glsl"

void main() {
    // A solid's radius is never negative and the mark is, so past half way is a glyph, whatever
    // interpolation did to the constant.
    if (vShape.z < UI_TEXT_MARK * 0.5) {
        // One level finer than the footprint asks: the smooth field still reconstructs there,
        // where the coarser level's averaging would thin every stem.
        float dist  = texture(u_tex, vUV, -1.0).r;
        float perPx = length(vec2(dFdx(dist), dFdy(dist)));
        float cover = clamp((dist - 0.5) / max(perPx, 1e-5) + 0.5, 0.0, 1.0);
        FragColor = vec4(vColor.rgb, vColor.a * cover);
        return;
    }
    vec4 tint = vColor;
    if (vImage > 0.5) {
        vec4 texel = texture(u_image, vec2(vUV.x, 1.0 - vUV.y));
        // The UI blends in display space; encode linear light back to it.
        if (u_imageSrgb != 0) texel.rgb = linearToSrgb(texel.rgb);
        tint *= texel;
    }
    if (vShape.z <= 0.0 && vShape.w <= 0.0) {
        FragColor = tint;
        return;
    }
    // Signed distance to the rounded box, in pixels, negative inside.
    vec2  halfSize = vShape.xy * 0.5;
    float radius   = min(vShape.z, min(halfSize.x, halfSize.y));
    vec2  p        = (vUV - 0.5) * vShape.xy;
    vec2  q        = abs(p) - halfSize + vec2(radius);
    float dist     = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float cover    = clamp(0.5 - dist, 0.0, 1.0);
    vec4  fill     = tint;
    if (vShape.w > 0.0) {
        float edge = clamp(0.5 + dist + vShape.w, 0.0, 1.0);
        fill = mix(fill, vBorder, edge);
    }
    FragColor = vec4(fill.rgb, fill.a * cover);
}

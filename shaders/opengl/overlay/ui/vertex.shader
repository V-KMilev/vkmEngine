/*
 * Screen-space UI: a quad an instance, its six corners - two triangles, top-left, top-right,
 * bottom-right, then top-left, bottom-right, bottom-left - from the vertex's index; viewport pixels
 * (top-left origin) to clip space by an orthographic projection. The fill fades from the top edge to
 * the bottom; the rest passes through to the fragment stage.
 */
layout(location = UI_ATTR_RECT)   in vec4 aRect;    // top-left (xy) and bottom-right (zw) corners, screen px
layout(location = UI_ATTR_UV)     in vec4 aUV;      // coordinates at those corners
layout(location = UI_ATTR_TOP)    in vec4 aTop;     // the fill along the top edge
layout(location = UI_ATTR_BOTTOM) in vec4 aBottom;  // and along the bottom
layout(location = UI_ATTR_BORDER) in vec4 aBorder;
layout(location = UI_ATTR_STYLE)  in vec3 aStyle;   // corner radius (UI_TEXT_MARK on a glyph), border, image

uniform mat4 u_proj;

out vec2 vUV;
out vec4 vColor;
out vec4 vShape;
out vec4 vBorder;
flat out float vImage;

void main() {
    // Which of the six: right on 1, 2 and 4; down on 2, 4 and 5.
    const vec2 corner = vec2((22 >> gl_VertexID) & 1, (52 >> gl_VertexID) & 1);
    vUV     = mix(aUV.xy, aUV.zw, corner);
    vColor  = mix(aTop, aBottom, corner.y);
    vShape  = vec4(aRect.zw - aRect.xy, aStyle.xy);
    vBorder = aBorder;
    vImage  = aStyle.z;
    gl_Position = u_proj * vec4(mix(aRect.xy, aRect.zw, corner), 0.0, 1.0);
}

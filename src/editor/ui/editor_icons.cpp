#include "ui/editor_icons.h"

#include <cmath>
#include <cstdio>

#include "ui/editor_style.h"

namespace Engine {

namespace {

void arrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float len, ImU32 col, float th) {
    ImVec2 perp(-dir.y, dir.x);
    ImVec2 backL(tip.x - dir.x * len + perp.x * len * 0.6f,
                 tip.y - dir.y * len + perp.y * len * 0.6f);
    ImVec2 backR(tip.x - dir.x * len - perp.x * len * 0.6f,
                 tip.y - dir.y * len - perp.y * len * 0.6f);
    dl->AddLine(tip, backL, col, th);
    dl->AddLine(tip, backR, col, th);
}

} // namespace

void drawEditorIcon(ImDrawList* dl, EditorIcon icon, ImVec2 c, float r, ImU32 col) {
    const float th = std::max(1.5f, r * 0.20f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };

    // Isometric cube as one continuous outline + three filled face accents,
    // so the three visible faces read as one solid shape instead of three
    // independent strokes with mismatched anti-aliasing at the centre.
    auto isoCube = [&]() {
        const float R  = std::round(0.82f * r);
        const float hx = std::round(0.86603f * R);  // sqrt(3)/2 * R
        const float hy = std::round(0.5f * R);
        const float cx = std::round(c.x);
        const float cy = std::round(c.y);
        const ImVec2 v0(cx,       cy - R);
        const ImVec2 v1(cx + hx,  cy - hy);
        const ImVec2 v2(cx + hx,  cy + hy);
        const ImVec2 v3(cx,       cy + R);
        const ImVec2 v4(cx - hx,  cy + hy);
        const ImVec2 v5(cx - hx,  cy - hy);
        const ImVec2 m( cx, cy);

        // Subtle face fills (top-right / bottom / top-left rhombuses) so the
        // cube has depth, with the outline drawn over them to crisp the edges.
        const ImU32 fill = (col & 0x00FFFFFF) | (0x30u << 24);
        ImVec2 fTop[4] = { v0, v1, m, v5 };
        ImVec2 fRgt[4] = { v1, v2, v3, m };
        ImVec2 fLft[4] = { m, v3, v4, v5 };
        dl->AddConvexPolyFilled(fTop, 4, fill);
        dl->AddConvexPolyFilled(fRgt, 4, fill);
        dl->AddConvexPolyFilled(fLft, 4, fill);

        const ImVec2 hexa[6] = { v0, v1, v2, v3, v4, v5 };
        dl->AddPolyline(hexa, 6, col, ImDrawFlags_Closed, th);
        // Internal Y as a single polyline (v0 -> m -> v2 then m -> v4) so the
        // strokes share endpoints and the centre never accumulates AA stubs.
        const ImVec2 yShape[3] = { v0, m, v2 };
        dl->AddPolyline(yShape, 3, col, ImDrawFlags_None, th);
        dl->AddLine(m, v4, col, th);
    };

    switch (icon) {
        case EditorIcon::Select: {
            ImVec2 pts[] = {
                P(-0.55f, -0.70f), P(-0.55f, 0.58f), P(-0.20f, 0.24f),
                P(-0.02f, 0.66f), P(0.18f, 0.56f), P(0.00f, 0.16f), P(0.42f, 0.16f),
            };
            dl->AddPolyline(pts, IM_ARRAYSIZE(pts), col, ImDrawFlags_Closed, th);
            break;
        }
        case EditorIcon::Move: {
            const float L = 0.78f;
            dl->AddLine(P(-L, 0), P(L, 0), col, th);
            dl->AddLine(P(0, -L), P(0, L), col, th);
            float a = r * 0.30f;
            arrowHead(dl, P(L, 0),  ImVec2(1, 0),  a, col, th);
            arrowHead(dl, P(-L, 0), ImVec2(-1, 0), a, col, th);
            arrowHead(dl, P(0, L),  ImVec2(0, 1),  a, col, th);
            arrowHead(dl, P(0, -L), ImVec2(0, -1), a, col, th);
            break;
        }
        case EditorIcon::Rotate: {
            // Circular arrow: ~275deg arc + a solid triangular arrowhead so
            // the direction reads clearly even at small sizes.
            const float rr = r * 0.60f;
            const float aMin = -2.4f, aMax = 2.4f;
            dl->PathArcTo(c, rr, aMin, aMax, 32);
            dl->PathStroke(col, ImDrawFlags_None, th);

            float ce = std::cos(aMax), se = std::sin(aMax);
            ImVec2 e(c.x + ce * rr, c.y + se * rr);  // arc end point
            ImVec2 t(-se, ce);                       // tangent (travel dir)
            ImVec2 n(ce, se);                        // radial (perp to tangent)
            float hl = r * 0.46f, hw = r * 0.30f;
            ImVec2 tip(e.x + t.x * hl, e.y + t.y * hl);
            ImVec2 b1(e.x - t.x * hl * 0.35f + n.x * hw,
                      e.y - t.y * hl * 0.35f + n.y * hw);
            ImVec2 b2(e.x - t.x * hl * 0.35f - n.x * hw,
                      e.y - t.y * hl * 0.35f - n.y * hw);
            dl->AddTriangleFilled(tip, b1, b2, col);
            break;
        }
        case EditorIcon::Scale: {
            // Build ONE arrowhead (bottom-left) from fully pixel-snapped
            // points, then derive the other as its exact integer mirror about
            // the snapped centre - so the two heads are byte-identical, and
            // smaller than before with a longer clear shaft between them.
            const float u = 0.65f;
            float cx = std::round(c.x), cy = std::round(c.y);
            float e  = std::round(0.7f * r);
            const float hl = r * 0.4f, hw = r * 0.3f;
            const float K  = hl * 1.4f;
            auto mir = [&](ImVec2 p) { return ImVec2(2.0f * cx - p.x, 2.0f * cy - p.y); };

            ImVec2 A(cx - e, cy + e);                                  // tip
            ImVec2 bA(std::round(A.x + u * K), std::round(A.y - u * K)); // base centre
            ImVec2 a1(std::round(bA.x + u * hw), std::round(bA.y + u * hw));
            ImVec2 a2(std::round(bA.x - u * hw), std::round(bA.y - u * hw));

            ImVec2 B = mir(A), b1 = mir(a1), b2 = mir(a2);

            // Shaft = the full A->B diagonal. That line passes through each
            // tip and the centre, i.e. it IS the symmetry axis of both heads,
            // so it is exactly centred on them. Draw it first; the filled
            // triangles on top cap the ends, leaving a centred connector.
            dl->AddLine(A, B, col, th);
            dl->AddTriangleFilled(A, a1, a2, col);
            dl->AddTriangleFilled(B, b1, b2, col);
            break;
        }
        case EditorIcon::SpaceLocal:
        case EditorIcon::Mesh: {
            isoCube();
            break;
        }
        case EditorIcon::SpaceWorld: {
            dl->AddCircle(c, r * 0.78f, col, 24, th);
            dl->AddLine(P(-0.78f, 0), P(0.78f, 0), col, th);
            dl->AddEllipse(c, ImVec2(r * 0.34f, r * 0.78f), col, 0.0f, 24, th);
            break;
        }
        case EditorIcon::Snap: {
            // 3x3 grid: inner lines first (full length so they reach/connect
            // to the frame), then a single AddRect frame on top. AddRect is a
            // closed path so its corners join cleanly - no stubs, no plugs.
            // All coords pixel-snapped and symmetric.
            float cx = std::round(c.x), cy = std::round(c.y);
            float half  = std::round(0.8f * r);
            float inner = std::round(half / 4.0f);
            float offset = 0.4f;
            float cxInner = cx - offset;
            float cyInner = cy - offset;
            dl->AddLine(ImVec2(cxInner - inner, cyInner - half), ImVec2(cxInner - inner, cyInner + half), col, th / 1.3f);
            dl->AddLine(ImVec2(cxInner + inner, cyInner - half), ImVec2(cxInner + inner, cyInner + half), col, th / 1.3f);
            dl->AddLine(ImVec2(cxInner - half, cyInner - inner), ImVec2(cxInner + half, cyInner - inner), col, th / 1.3f);
            dl->AddLine(ImVec2(cxInner - half, cyInner + inner), ImVec2(cxInner + half, cyInner + inner), col, th / 1.3f);
            dl->AddRect(ImVec2(cx - half - offset, cy - half - offset), ImVec2(cx + half + offset, cy + half + offset), col, 0.0f, 0, th);
            break;
        }
        case EditorIcon::Duplicate: {
            dl->AddRect(P(-0.05f, -0.62f), P(0.62f, 0.05f), col, 2.0f, 0, th);
            dl->AddRect(P(-0.62f, -0.05f), P(0.05f, 0.62f), col, 2.0f, 0, th);
            break;
        }
        case EditorIcon::Focus: {
            dl->AddCircle(c, r * 0.30f, col, 16, th);
            dl->AddCircle(c, r * 0.66f, col, 20, th);
            dl->AddLine(P(0, -0.95f), P(0, -0.55f), col, th);
            dl->AddLine(P(0, 0.55f),  P(0, 0.95f),  col, th);
            dl->AddLine(P(-0.95f, 0), P(-0.55f, 0), col, th);
            dl->AddLine(P(0.55f, 0),  P(0.95f, 0),  col, th);
            break;
        }
        case EditorIcon::Trash: {
            dl->AddLine(P(-0.62f, -0.5f), P(0.62f, -0.5f), col, th);
            dl->AddLine(P(-0.22f, -0.5f), P(-0.16f, -0.72f), col, th);
            dl->AddLine(P(0.22f, -0.5f),  P(0.16f, -0.72f), col, th);
            dl->AddLine(P(-0.16f, -0.72f), P(0.16f, -0.72f), col, th);
            dl->AddLine(P(-0.46f, -0.5f), P(-0.36f, 0.72f), col, th);
            dl->AddLine(P(0.46f, -0.5f),  P(0.36f, 0.72f),  col, th);
            dl->AddLine(P(-0.36f, 0.72f), P(0.36f, 0.72f),  col, th);
            dl->AddLine(P(-0.12f, -0.32f), P(-0.10f, 0.58f), col, th);
            dl->AddLine(P(0.12f, -0.32f),  P(0.10f, 0.58f),  col, th);
            break;
        }
        case EditorIcon::Play: {
            dl->AddTriangleFilled(P(-0.45f, -0.62f), P(-0.45f, 0.62f), P(0.62f, 0.0f), col);
            break;
        }
        case EditorIcon::Pause: {
            dl->AddRectFilled(P(-0.45f, -0.6f), P(-0.12f, 0.6f), col);
            dl->AddRectFilled(P(0.12f, -0.6f),  P(0.45f, 0.6f),  col);
            break;
        }
        case EditorIcon::Stop: {
            dl->AddRectFilled(P(-0.52f, -0.52f), P(0.52f, 0.52f), col);
            break;
        }
        case EditorIcon::Step: {
            // Step-forward: a play triangle nudged against a bar (>|).
            dl->AddTriangleFilled(P(-0.55f, -0.55f), P(-0.55f, 0.55f), P(0.22f, 0.0f), col);
            dl->AddRectFilled(P(0.34f, -0.6f), P(0.58f, 0.6f), col);
            break;
        }
        case EditorIcon::Key: {
            ImVec2 d[] = { P(0, -0.7f), P(0.62f, 0), P(0, 0.7f), P(-0.62f, 0) };
            dl->AddConvexPolyFilled(d, 4, col);
            break;
        }
        case EditorIcon::Plus: {
            dl->AddLine(P(0, -0.62f), P(0, 0.62f), col, th * 1.3f);
            dl->AddLine(P(-0.62f, 0), P(0.62f, 0), col, th * 1.3f);
            break;
        }
        case EditorIcon::Cross: {
            dl->AddLine(P(-0.5f, -0.5f), P(0.5f, 0.5f), col, th * 1.3f);
            dl->AddLine(P(-0.5f, 0.5f),  P(0.5f, -0.5f), col, th * 1.3f);
            break;
        }
        case EditorIcon::Entity: {
            // Generic transform entity: a hollow diamond with a smaller
            // filled diamond inside. Symmetric, reads at any size, and
            // doesn't compete visually with the type-specific glyphs.
            const ImVec2 outer[4] = { P(0,-0.78f), P(0.78f,0), P(0,0.78f), P(-0.78f,0) };
            dl->AddPolyline(outer, 4, col, ImDrawFlags_Closed, th);
            const ImVec2 inner[4] = { P(0,-0.32f), P(0.32f,0), P(0,0.32f), P(-0.32f,0) };
            dl->AddConvexPolyFilled(inner, 4, col);
            break;
        }
        case EditorIcon::Camera: {
            // Camera body (rounded rectangle) + a small viewfinder bump on
            // top + a filled lens trapezoid on the right + a hollow lens
            // glass. Drawn as a single body + a closed polyline for the lens
            // so every seam joins cleanly at small sizes.
            const ImVec2 bMin = P(-0.62f, -0.32f);
            const ImVec2 bMax = P( 0.30f,  0.40f);
            dl->AddRect(bMin, bMax, col, r * 0.14f, 0, th);
            // Top viewfinder tab (centered on body x-axis 0).
            dl->AddRectFilled(P(-0.16f, -0.46f), P(0.14f, -0.32f), col, r * 0.06f);

            // Lens as a closed trapezoid - convex poly for clean joins.
            const ImVec2 lens[4] = {
                P(0.30f, -0.18f), P(0.66f, -0.30f),
                P(0.66f,  0.30f), P(0.30f,  0.18f),
            };
            dl->AddPolyline(lens, 4, col, ImDrawFlags_Closed, th);

            // Lens glass: a centered circle on the lens midline.
            dl->AddCircleFilled(P(0.48f, 0.0f), r * 0.10f, col);
            break;
        }
        case EditorIcon::LightDir:
        case EditorIcon::LightPoint:
        case EditorIcon::LightSpot: {
            // One canonical lightbulb for every light type. At icon size the
            // directional/point/spot differences never read clearly; the
            // row label and the viewport wireframe already convey type. A
            // single confident bulb with emission rays is much more legible.
            //
            // Geometry (pixel-snapped, symmetric about the vertical axis):
            //   * Tall ellipse bulb (A-shape silhouette - taller than wide)
            //   * 5 emission rays in the upper hemisphere
            //   * Soft halo behind the bulb for the "glow" cue
            //   * Trapezoid neck joining bulb to base
            //   * Screw cap with two faint thread lines
            const float cx     = std::round(c.x);
            const float bulbRx = std::round(r * 0.32f);
            const float bulbRy = std::round(r * 0.44f);
            const ImVec2 bulbC(cx, std::round(c.y - r * 0.22f));

            // Soft halo - underplays the bulb as "lit" without competing
            // with the rays.
            const ImU32 halo = (col & 0x00FFFFFF) | (0x28u << 24);
            dl->AddEllipseFilled(bulbC,
                ImVec2(bulbRx + std::round(r * 0.10f),
                       bulbRy + std::round(r * 0.10f)),
                halo, 0.0f, 24);

            // Five emission rays in the upper hemisphere (left -> up -> right).
            // Even angular spacing with a small inset from the horizontal so
            // the side rays don't graze the bulb's equator.
            constexpr float PI_F = 3.14159265f;
            const float rayGap = std::max(2.0f, std::round(r * 0.16f));
            const float rayLen = std::round(r * 0.22f);
            const int   rayN   = 5;
            for (int i = 0; i < rayN; ++i) {
                const float a  = -PI_F + (i + 1.0f) * PI_F / (rayN + 1);
                const float dx = std::cos(a);
                const float dy = std::sin(a);
                // Ellipse edge along (dx, dy):
                //   r_edge = (rx * ry) / sqrt((ry*dx)^2 + (rx*dy)^2)
                const float er = (bulbRx * bulbRy) /
                    std::sqrt((bulbRy * dx) * (bulbRy * dx) +
                              (bulbRx * dy) * (bulbRx * dy));
                const ImVec2 s(bulbC.x + dx * (er + rayGap),
                               bulbC.y + dy * (er + rayGap));
                const ImVec2 e(s.x + dx * rayLen, s.y + dy * rayLen);
                dl->AddLine(s, e, col, th);
            }

            // Bulb glass: tall ellipse for classic A-shape.
            dl->AddEllipseFilled(bulbC, ImVec2(bulbRx, bulbRy), col, 0.0f, 24);

            // Neck: trapezoid joining bulb to base.
            const float neckTopY = bulbC.y + std::round(bulbRy * 0.80f);
            const float neckH    = std::round(bulbRy * 0.22f);
            const float neckW0   = std::round(bulbRx * 0.78f);
            const float neckW1   = std::round(bulbRx * 0.58f);
            const ImVec2 neck[4] = {
                ImVec2(cx - neckW0, neckTopY),
                ImVec2(cx + neckW0, neckTopY),
                ImVec2(cx + neckW1, neckTopY + neckH),
                ImVec2(cx - neckW1, neckTopY + neckH),
            };
            dl->AddConvexPolyFilled(neck, 4, col);

            // Screw cap below the neck.
            const float baseY0 = neckTopY + neckH;
            const float baseW  = std::round(bulbRx * 0.58f);
            const float baseH  = std::round(bulbRy * 0.32f);
            dl->AddRectFilled(ImVec2(cx - baseW, baseY0),
                              ImVec2(cx + baseW, baseY0 + baseH),
                              col, std::max(1.0f, baseH * 0.10f));

            // Thread hints on the cap so it reads as a screw base, not a tab.
            const ImU32 dim = (col & 0x00FFFFFF) | (0x60u << 24);
            const float tInset = std::max(1.0f, baseW * 0.20f);
            dl->AddLine(ImVec2(cx - baseW + tInset, baseY0 + baseH * 0.38f),
                        ImVec2(cx + baseW - tInset, baseY0 + baseH * 0.38f), dim, 1.0f);
            dl->AddLine(ImVec2(cx - baseW + tInset, baseY0 + baseH * 0.72f),
                        ImVec2(cx + baseW - tInset, baseY0 + baseH * 0.72f), dim, 1.0f);
            break;
        }
        case EditorIcon::Anim: {
            // Timeline with three keyframes (small / large / small),
            // centred on the entity's icon row. The horizontal rule
            // is drawn first so the diamond fills sit cleanly over it.
            dl->AddLine(P(-0.78f, 0.0f), P(0.78f, 0.0f), col, th);
            auto diamond = [&](float x, float s) {
                const ImVec2 d[4] = {
                    ImVec2(c.x + x * r,        c.y - s * r),
                    ImVec2(c.x + (x + s) * r,  c.y           ),
                    ImVec2(c.x + x * r,        c.y + s * r),
                    ImVec2(c.x + (x - s) * r,  c.y           ),
                };
                dl->AddConvexPolyFilled(d, 4, col);
            };
            diamond(-0.50f, 0.20f);
            diamond( 0.00f, 0.36f);  // hero key
            diamond( 0.50f, 0.20f);
            break;
        }
        case EditorIcon::FrameAll: {
            // Brackets enclosing a small square - "fit everything in view".
            for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                const ImVec2 corner = P(0.62f * sx, 0.62f * sy);
                dl->AddLine(corner, P(0.32f * sx, 0.62f * sy), col, th);
                dl->AddLine(corner, P(0.62f * sx, 0.32f * sy), col, th);
            }
            dl->AddRect(P(-0.20f, -0.20f), P(0.20f, 0.20f), col, 0.0f, 0, th);
            break;
        }
    }
}

bool iconButton(
    const char* idStr,
    EditorIcon icon,
    bool active,
    bool enabled,
    const char* tooltip,
    float size
) {
    if (!enabled) ImGui::BeginDisabled();
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT);
    }

    char id[48];
    snprintf(id, sizeof(id), "###%s", idStr);
    bool pressed = ImGui::Button(id, ImVec2(size, size));

    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    ImU32 col = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    drawEditorIcon(ImGui::GetWindowDrawList(), icon, c, size * 0.34f, col);

    if (active) ImGui::PopStyleColor(2);
    if (!enabled) ImGui::EndDisabled();

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);

    return pressed;
}

} // namespace Engine

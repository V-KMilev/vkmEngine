#include "editor_icons.h"
#include "editor_style.h"

#include <cmath>
#include <cstdio>

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
            // the snapped centre -- so the two heads are byte-identical, and
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
        case EditorIcon::SpaceLocal: {
            // Isometric cube: regular hexagon outline + a 3-spoke "Y" to the
            // centre vertex. Mirror-symmetric about the vertical axis.
            const float R  = 0.84f;
            const float hx = 0.86603f * R;  // sqrt(3)/2 * R
            const float hy = 0.5f * R;
            ImVec2 v0 = P(0.0f, -R);
            ImVec2 v1 = P(hx, -hy);
            ImVec2 v2 = P(hx, hy);
            ImVec2 v3 = P(0.0f, R);
            ImVec2 v4 = P(-hx, hy);
            ImVec2 v5 = P(-hx, -hy);
            ImVec2 hexa[6] = { v0, v1, v2, v3, v4, v5 };
            dl->AddPolyline(hexa, 6, col, ImDrawFlags_Closed, th);
            ImVec2 m = P(0.0f, 0.0f);
            dl->AddLine(m, v0, col, th);  // edge up to top
            dl->AddLine(m, v2, col, th);  // edge to lower-right
            dl->AddLine(m, v4, col, th);  // edge to lower-left
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
            // closed path so its corners join cleanly -- no stubs, no plugs.
            // All coords pixel-snapped and symmetric.
            float cx = std::round(c.x), cy = std::round(c.y);
            float half  = std::round(0.8f * r);
            float inner = std::round(half / 3.0f);
            float offset = 0.4f;
            dl->AddLine(ImVec2(cx - inner, cy - half), ImVec2(cx - inner, cy + half), col, th / 1.2f);
            dl->AddLine(ImVec2(cx + inner, cy - half), ImVec2(cx + inner, cy + half), col, th / 1.2f);
            dl->AddLine(ImVec2(cx - half, cy - inner), ImVec2(cx + half, cy - inner), col, th / 1.2f);
            dl->AddLine(ImVec2(cx - half, cy + inner), ImVec2(cx + half, cy + inner), col, th / 1.2f);
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
    }
}

bool iconButton(const char* idStr, EditorIcon icon, bool active,
                bool enabled, const char* tooltip, float size) {
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

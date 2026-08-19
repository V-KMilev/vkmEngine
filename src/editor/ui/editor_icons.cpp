#include "ui/editor_icons.h"

#include <cfloat>
#include <cmath>
#include <cstdio>

#include "ui/editor_style.h"

namespace Vkm::Engine {

namespace {

ImFont* s_iconFont = nullptr;

// EditorIcon -> Lucide codepoint (lucide-static font/info.json). Every enum
// value maps; the trailing 0 only catches an out-of-range cast.
ImWchar iconCodepoint(EditorIcon icon) {
    switch (icon) {
        case EditorIcon::Select:     return 0xe1c3;  // mouse-pointer-2
        case EditorIcon::Move:       return 0xe121;  // move
        case EditorIcon::Rotate:     return 0xe149;  // rotate-cw
        case EditorIcon::Scale:      return 0xe2ec;  // scaling
        case EditorIcon::SpaceLocal: return 0xe2fe;  // axis-3d
        case EditorIcon::SpaceWorld: return 0xe0e8;  // globe
        case EditorIcon::Snap:       return 0xe2b5;  // magnet
        case EditorIcon::Duplicate:  return 0xe09e;  // copy
        case EditorIcon::Focus:      return 0xe29e;  // focus
        case EditorIcon::Trash:      return 0xe18e;  // trash-2
        case EditorIcon::Play:       return 0xe13c;  // play
        case EditorIcon::Pause:      return 0xe12e;  // pause
        case EditorIcon::Stop:       return 0xe167;  // square
        case EditorIcon::Step:       return 0xe160;  // skip-forward
        case EditorIcon::Loop:       return 0xe146;  // repeat
        case EditorIcon::Key:        return 0xe2d2;  // diamond (keyframe)
        case EditorIcon::Plus:       return 0xe13d;  // plus
        case EditorIcon::Cross:      return 0xe1b2;  // x
        case EditorIcon::Entity:     return 0xe061;  // box
        case EditorIcon::Mesh:       return 0xe524;  // cuboid
        case EditorIcon::Camera:     return 0xe1a5;  // video
        case EditorIcon::LightDir:   return 0xe178;  // sun
        case EditorIcon::LightPoint: return 0xe1c2;  // lightbulb
        case EditorIcon::LightSpot:  return 0xe0d3;  // flashlight
        case EditorIcon::Anim:       return 0xe0d0;  // film
        case EditorIcon::Probe:      return 0xe3e7;  // orbit
        case EditorIcon::Volume:     return 0xe0e9;  // grid-3x3
        case EditorIcon::Decal:      return 0xe302;  // sticker
        case EditorIcon::Particle:   return 0xe412;  // sparkles
        case EditorIcon::UIWidget:   return 0xe426;  // app-window
        case EditorIcon::FrameAll:   return 0xe257;  // scan
        case EditorIcon::UICanvas:   return 0xe291;  // frame
        case EditorIcon::UIText:     return 0xe198;  // type
        case EditorIcon::UIImage:    return 0xe0f6;  // image
        case EditorIcon::UIButton:   return 0xe202;  // square-mouse-pointer
        case EditorIcon::LightRect:  return 0xe376;  // rectangle-horizontal
        case EditorIcon::LightDisk:  return 0xe0af;  // disc
        case EditorIcon::Cube:       return 0xe061;  // box
        case EditorIcon::Sphere:     return 0xe076;  // circle
        case EditorIcon::Plane:      return 0xe167;  // square
        case EditorIcon::Pyramid:    return 0xe52c;  // pyramid
        case EditorIcon::Cone:       return 0xe523;  // cone
        case EditorIcon::Triangle:   return 0xe192;  // triangle
        case EditorIcon::Empty:      return 0xe4b0;  // circle-dashed
        case EditorIcon::Import:     return 0xe22f;  // import
        case EditorIcon::Colliders:  return 0xe1cb;  // box-select
    }
    return 0;
}

// Encode one codepoint as UTF-8 (Lucide sits in the U+E000 private area: 3 bytes).
void encodeUtf8(ImWchar cp, char out[5]) {
    out[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    out[3] = '\0';
    out[4] = '\0';
}

} // namespace

bool loadEditorIconFont(const char* path) {
    ImGuiIO& io = ImGui::GetIO();
    s_iconFont  = io.Fonts->AddFontFromFileTTF(path, 15.0f);
    return s_iconFont != nullptr;
}

void drawEditorIcon(ImDrawList* dl, EditorIcon icon, ImVec2 c, float r, ImU32 col) {
    // The designed set: the mapped Lucide glyph, centered in the (c, r) frame
    // the caller asked for.
    if (s_iconFont) {
        if (const ImWchar cp = iconCodepoint(icon)) {
            // Lucide art fills ~20/24 of its em, so the glyph is drawn at 2.3r
            // to occupy the (c, r) frame the caller reserved for it.
            const float sz = std::round(std::max(8.0f, r * 2.3f));
            char txt[5];
            encodeUtf8(cp, txt);

            // Optical centering on the glyph's actual bounds, not the em box:
            // icon fonts hang their art off the baseline, so em-box centering
            // sat every glyph slightly high (clipping tops in tight rows).
            ImVec2 pos;
            ImFontBaked* baked = s_iconFont->GetFontBaked(sz);
            const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback(static_cast<ImWchar>(cp)) : nullptr;
            if (g) {
                pos = ImVec2(std::floor(c.x - (g->X0 + g->X1) * 0.5f),
                             std::floor(c.y - (g->Y0 + g->Y1) * 0.5f));
            } else {
                const ImVec2 ts = s_iconFont->CalcTextSizeA(sz, FLT_MAX, 0.0f, txt);
                pos = ImVec2(std::floor(c.x - ts.x * 0.5f),
                             std::floor(c.y - sz * 0.5f));
            }
            dl->AddText(s_iconFont, sz, pos, col, txt);
            return;
        }
    }

    // No icon font: one neutral primitive for every icon, so a button stays
    // visibly clickable and its tooltip still names the action. Deliberately not
    // per-icon art - the font ships with the engine, so this path only runs when
    // the installed file was removed by hand.
    r = std::max(3.0f, std::round(r));
    c = ImVec2(std::floor(c.x) + 0.5f, std::floor(c.y) + 0.5f);
    const float th = std::max(1.0f, std::round(r * 0.20f));
    dl->AddRect(ImVec2(c.x - r * 0.7f, c.y - r * 0.7f),
                ImVec2(c.x + r * 0.7f, c.y + r * 0.7f),
                col, r * 0.25f, 0, th);
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
        // Hover still brightens an active tool - identical colors would make
        // the active button feel dead under the cursor.
        ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
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

} // namespace Vkm::Engine

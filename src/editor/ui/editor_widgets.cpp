#include "ui/editor_widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/editor_style.h"
#include "core/utf8.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/name.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"

namespace Vkm::Engine {

namespace {

// The value's position in [lo, hi] as 0..1, mapped as ImGui maps a click (logarithmic too).
float sliderShare(ImGuiDataType type, const void* v, const void* lo, const void* hi, ImGuiSliderFlags flags) {
    double value = 0.0, low = 0.0, high = 0.0;
    if (type == ImGuiDataType_S32) {
        value = *static_cast<const int*>(v);
        low   = *static_cast<const int*>(lo);
        high  = *static_cast<const int*>(hi);
    } else {
        value = *static_cast<const float*>(v);
        low   = *static_cast<const float*>(lo);
        high  = *static_cast<const float*>(hi);
    }
    if (high == low) return 0.0f;
    double share = (value - low) / (high - low);
    if ((flags & ImGuiSliderFlags_Logarithmic) && low > 0.0 && high > 0.0 && value > 0.0)
        share = std::log(value / low) / std::log(high / low);
    return static_cast<float>(std::clamp(share, 0.0, 1.0));
}

} // namespace

bool sliderScalar(
    const char* id,
    ImGuiDataType type,
    void* v,
    const void* lo,
    const void* hi,
    const char* format,
    ImGuiSliderFlags flags
) {
    // Typed into, it is ImGui's own text field.
    if (ImGui::TempInputIsActive(ImGui::GetCurrentWindow()->GetID(id)))
        return ImGui::SliderScalar(id, type, v, lo, hi, format, flags);

    // ImGui draws the frame; grab and text are drawn here, text last.
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, clear);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, clear);
    ImGui::PushStyleColor(ImGuiCol_Text, clear);
    const bool changed = ImGui::SliderScalar(id, type, v, lo, hi, format, flags);
    ImGui::PopStyleColor(3);
    if (!ImGui::IsItemVisible()) return changed;

    // ImGui's grab track: two pixels in from each end, less the grab width (an int's one step).
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float pad = 2.0f;
    const float track = (max.x - min.x) - pad * 2.0f;
    float grab = style.GrabMinSize;
    if (type == ImGuiDataType_S32) {
        const float steps = static_cast<float>(*static_cast<const int*>(hi) - *static_cast<const int*>(lo));
        if (steps >= 0.0f) grab = std::max(track / (steps + 1.0f), style.GrabMinSize);
    }
    grab = std::min(grab, track);
    const float edge = min.x + pad + grab * 0.5f
        + sliderShare(type, v, lo, hi, flags) * (track - grab);

    const bool lit = ImGui::IsItemActive() || ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
    fill.w *= lit ? 0.50f : 0.36f;
    draw->AddRectFilled(
        min,
        ImVec2(edge, max.y),
        ImGui::GetColorU32(fill),
        style.FrameRounding,
        ImDrawFlags_RoundCornersLeft
    );

    char text[64];
    const char* end = text + ImGui::DataTypeFormatString(text, IM_ARRAYSIZE(text), type, v, format);
    ImGui::RenderTextClipped(min, max, text, end, nullptr, ImVec2(0.5f, 0.5f));
    return changed;
}

bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    const bool open = ImGui::BeginCombo(id, preview, flags);
    ImGui::PopStyleColor(2);
    return open;
}

bool comboList(const char* id, int* index, const char* const* labels, int count) {
    const char* preview = (*index >= 0 && *index < count) ? labels[*index] : "?";
    if (!beginCombo(id, preview)) return false;
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const bool selected = (i == *index);
        if (ImGui::Selectable(labels[i], selected) && !selected) {
            *index  = i;
            changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

bool beginOverlayStrip(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleColor(ImGuiCol_Border, EditorStyle::OVERLAY_EDGE);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::overlayRounding());
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(EditorStyle::overlayPad(), EditorStyle::overlayPad())
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(EditorStyle::overlayGap(), EditorStyle::overlayGap())
    );
    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (size.x <= 0.0f) flags |= ImGuiChildFlags_AutoResizeX;
    if (size.y <= 0.0f) flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(
        id,
        size,
        flags,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
}

void endOverlayStrip() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

bool drawVec3Control(const char* label, float* values, float resetValue, float speed, float lo, float hi) {
    bool changed = false;
    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize(lineHeight * 0.8f, lineHeight);
    // Floored, or the drags vanish on a narrow panel; overflowing is the lesser failure.
    const float spareWidth = ImGui::GetContentRegionAvail().x - EditorStyle::labelWidth()
        - buttonSize.x * 3 - ImGui::GetStyle().ItemSpacing.x * 5;
    float inputWidth = std::max(spareWidth / 3.0f, ImGui::GetFontSize() * 2.5f);

    // The full width drawPropertyLabel sets is spent on the X button.
    drawPropertyLabel(label);

    static const struct {
        const char*   button;
        const char*   drag;
        const ImVec4& color;
        const ImVec4& hover;
    } AXES[3] = {
        { "X", "##X", EditorStyle::AXIS_X, EditorStyle::AXIS_X_HOV },
        { "Y", "##Y", EditorStyle::AXIS_Y, EditorStyle::AXIS_Y_HOV },
        { "Z", "##Z", EditorStyle::AXIS_Z, EditorStyle::AXIS_Z_HOV },
    };

    // The button only shows on hover: solid RGB blocks on every row would be the loudest thing.
    auto tint = [](ImVec4 c, float a) {
        c.w = a;
        return c;
    };
    for (int i = 0; i < 3; ++i) {
        ImGui::PushStyleColor(ImGuiCol_Text, AXES[i].hover);
        ImGui::PushStyleColor(ImGuiCol_Button, tint(AXES[i].color, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tint(AXES[i].color, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, tint(AXES[i].color, 0.40f));
        if (ImGui::Button(AXES[i].button, buttonSize)) {
            values[i] = resetValue;
            changed = true;
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset %s", AXES[i].button);
        ImGui::SameLine(0, EditorStyle::px(2.0f));
        ImGui::SetNextItemWidth(inputWidth);
        changed |= ImGui::DragFloat(AXES[i].drag, &values[i], speed, lo, hi, "%.2f", PROP_CLAMP);
        if (i < 2) ImGui::SameLine(0, EditorStyle::px(6.0f));
    }

    ImGui::PopID();
    return changed;
}

void drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();

    // Measured from the row's start, indent included, or a label in an indented card slides
    // under its widget. A long label ellipsizes.
    const float startX = ImGui::GetCursorPosX();
    const float colW   = EditorStyle::labelWidth();
    const float maxW   = colW - ImGui::GetStyle().ItemSpacing.x;

    if (ImGui::CalcTextSize(label).x <= maxW) {
        ImGui::TextUnformatted(label);
    } else {
        const float dotsW = ImGui::CalcTextSize("..").x;
        char clipped[96];
        size_t n = 0;
        for (const char* c = label; *c && n < sizeof(clipped) - 4; ++c) {
            clipped[n] = *c;
            clipped[n + 1] = '\0';
            if (ImGui::CalcTextSize(clipped).x + dotsW > maxW) break;
            ++n;
        }
        snprintf(clipped + n, sizeof(clipped) - n, "..");
        ImGui::TextUnformatted(clipped);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
    }

    ImGui::SameLine(startX + colW);
    ImGui::SetNextItemWidth(-1);
}

namespace {
struct CardState {
    ImVec4 accent;
    float  startY = 0.0f;   ///< Body top, screen-space y.
    float  lineX  = 0.0f;   ///< Left accent-line x, screen-space.
    int    frame  = 0;      ///< The ImGui frame that pushed it.
    bool   open   = false;
};

// The cards begun and not yet ended; ImGui, and so every card, is on one thread.
std::vector<CardState>& cardStack() {
    static std::vector<CardState> s_cards;
    return s_cards;
}
// A function: px() needs an ImGui context, absent when file-scope initializers run.
float cardIndent() { return EditorStyle::px(14.0f); }

// Tinted, accent-stripped CollapsingHeader (no body/end pairing).
bool styledCollapsingHeader(const char* title, const ImVec4& accent, bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_AllowOverlap
        | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushStyleColor(ImGuiCol_Header,        EditorStyle::CARD_HEADER);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorStyle::CARD_HEADER_HOV);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  EditorStyle::CARD_HEADER_ACT);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorStyle::px(8.0f), EditorStyle::px(7.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorStyle::px(4.0f));

    const bool open = ImGui::CollapsingHeader(title, flags);
    const ImVec2 rMin = ImGui::GetItemRectMin();
    const ImVec2 rMax = ImGui::GetItemRectMax();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // Accent strip welded to the header's left edge.
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(rMin.x, rMin.y),
        ImVec2(rMin.x + EditorStyle::px(3.0f), rMax.y),
        ImGui::GetColorU32(accent)
    );
    return open;
}
} // namespace

bool beginComponentCard(const char* title, const ImVec4& accent, bool defaultOpen, bool* removeClicked) {
    // Reset per frame, as ImGui's stacks are, so a skipped end cannot shift later cards.
    std::vector<CardState>& stack = cardStack();
    if (!stack.empty() && stack.back().frame != ImGui::GetFrameCount()) stack.clear();

    ImGui::PushID(title);
    ImGui::Spacing();

    const bool open   = styledCollapsingHeader(title, accent, defaultOpen);
    const ImVec2 rMin = ImGui::GetItemRectMin();

    if (removeClicked) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - EditorStyle::px(20.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
        if (ImGui::SmallButton("x")) *removeClicked = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
        ImGui::PopStyleColor(2);
    }

    CardState st;
    st.accent = accent;
    st.open   = open;
    st.frame  = ImGui::GetFrameCount();
    st.lineX  = rMin.x + cardIndent() * 0.5f;
    if (open) {
        ImGui::Indent(cardIndent());
        ImGui::Spacing();
        st.startY = ImGui::GetCursorScreenPos().y;
    }
    stack.push_back(st);
    return open;
}

void endComponentCard() {
    std::vector<CardState>& stack = cardStack();
    // No matching begin, so no PushID to pop.
    if (stack.empty()) return;

    const CardState st = stack.back();
    stack.pop_back();

    if (st.open) {
        ImGui::Spacing();
        const float endY = ImGui::GetCursorScreenPos().y;
        ImGui::Unindent(cardIndent());
        const ImU32 c = ImGui::GetColorU32(ImVec4(st.accent.x, st.accent.y, st.accent.z, 0.30f));
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(st.lineX, st.startY),
            ImVec2(st.lineX, endY),
            c,
            EditorStyle::px(2.0f)
        );
    }
    ImGui::PopID();
    ImGui::Spacing();
}

namespace {

/// Width of a line keeping [0, head) and [tail, len) with the ellipsis between.
float keptWidth(const char* s, size_t head, size_t tail, size_t len) {
    return ImGui::CalcTextSize(s, s + head).x + ImGui::CalcTextSize(s + tail, s + len).x;
}

} // namespace

std::string elidedLine(const char* text, float maxWidth) {
    const char* str = (text && text[0]) ? text : "(unnamed)";
    if (ImGui::CalcTextSize(str).x <= maxWidth) return str;

    const size_t len    = std::strlen(str);
    const float  budget = maxWidth - ImGui::CalcTextSize("...").x;

    // Alternate ends, so the halves stay equal in characters; never cut inside a
    // UTF-8 sequence, which renders a box.
    const std::string_view view(str, len);
    size_t head = 0;
    size_t tail = len;
    for (;;) {
        size_t grownHead = head;
        Utf8::next(view, grownHead);
        if (grownHead >= tail || keptWidth(str, grownHead, tail, len) > budget) break;
        head = grownHead;

        const size_t grownTail = Utf8::previous(view, tail);
        if (grownTail <= head || keptWidth(str, head, grownTail, len) > budget) break;
        tail = grownTail;
    }
    return std::string(str, head) + "..." + std::string(str + tail);
}

void emptyStateHeading(EditorIcon icon, const char* headline, const char* detail, int actionRows) {
    const ImVec2 region    = ImGui::GetContentRegionAvail();
    const float  glyphSize = EditorStyle::px(56.0f);
    const float  lineH     = ImGui::GetTextLineHeightWithSpacing();
    const float  blockH    = glyphSize + lineH * 2.0f
        + ImGui::GetFrameHeight() * static_cast<float>(actionRows)
        + EditorStyle::px(24.0f);
    ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (region.y - blockH) * 0.35f)));

    const ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(0.0f, glyphSize));
    drawEditorIcon(
        ImGui::GetWindowDrawList(),
        icon,
        ImVec2(cur.x + region.x * 0.5f, cur.y + glyphSize * 0.5f),
        glyphSize * 0.40f,
        ImGui::GetColorU32(ImGuiCol_TextDisabled)
    );

    ImGui::Spacing();
    centreNextItem(ImGui::CalcTextSize(headline).x);
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted(headline);
    ImGui::PopStyleColor();
    // Wrapped, not clipped, when the panel is narrower than the line.
    const float detailW = ImGui::CalcTextSize(detail).x;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (detailW <= region.x) {
        centreNextItem(detailW);
        ImGui::TextUnformatted(detail);
    } else {
        ImGui::TextWrapped("%s", detail);
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void centreNextItem(float width) {
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
}

bool tileFace(
    ImTextureID picture,
    float face,
    const ImVec4& accent,
    EditorIcon glyph,
    const ImVec4& glyphColor,
    ImVec2& faceMin,
    ImVec2& faceMax
) {
    faceMin = ImGui::GetCursorScreenPos();

    const ImVec4 inert = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImGui::SetNextItemAllowOverlap();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, inert);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, inert);
    const bool clicked = picture
        ? ImGui::ImageButton("##face", picture, ImVec2(face, face), ImVec2(0, 1), ImVec2(1, 0))
        : ImGui::Button("##face", ImVec2(face, face));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    faceMax = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        dl->AddRect(faceMin, faceMax, ImGui::GetColorU32(accent), 0.0f, 0, EditorStyle::px(2.0f));
    }
    if (!picture) {
        drawEditorIcon(
            dl,
            glyph,
            ImVec2((faceMin.x + faceMax.x) * 0.5f, (faceMin.y + faceMax.y) * 0.5f),
            face * 0.22f,
            ImGui::GetColorU32(glyphColor)
        );
    }
    return clicked;
}

void tileStrip(ImVec2 faceMin, ImVec2 faceMax, const ImVec4& color) {
    ImGui::GetWindowDrawList()->AddRectFilled(
        faceMin,
        ImVec2(faceMin.x + EditorStyle::px(3.0f), faceMax.y),
        ImGui::GetColorU32(color)
    );
}

void clippedLine(const char* text, float maxWidth, bool dim) {
    const std::string line = elidedLine(text, maxWidth);
    if (dim) ImGui::TextDisabled("%s", line.c_str());
    else     ImGui::TextUnformatted(line.c_str());
}

bool matchesFilter(const char* text, const char* filter) {
    if (!filter || !filter[0]) return true;
    for (const char* p = text; *p; ++p) {
        const char* s = filter;
        const char* t = p;
        while (*s && *t
            && tolower(static_cast<unsigned char>(*s)) == tolower(static_cast<unsigned char>(*t))) {
            ++s;
            ++t;
        }
        if (!*s) return true;
    }
    return false;
}

namespace {

struct EntityLabel {
    const char* name;
    EditorIcon  icon;
};

// One row per LightType, in order; the viewport marker wears the same glyph.
constexpr EntityLabel LIGHT_LABELS[] = {
    {"Dir Light",   EditorIcon::LightDir},
    {"Point Light", EditorIcon::LightPoint},
    {"Spot Light",  EditorIcon::LightSpot},
    {"Rect Light",  EditorIcon::LightRect},
    {"Disk Light",  EditorIcon::LightDisk},
};
static_assert(
    std::size(LIGHT_LABELS) == static_cast<size_t>(LightType::Count),
    "every light type needs a label and a glyph"
);

EntityLabel lightLabelOf(LightType type) {
    const auto index = static_cast<size_t>(type);
    return index < std::size(LIGHT_LABELS)
        ? LIGHT_LABELS[index]
        : EntityLabel{"Light", EditorIcon::LightPoint};
}

// Order is precedence. A Light answers by its type, and a Mesh carrying an
// Animation reads "Animated Mesh" while keeping the plain mesh glyph.
EntityLabel entityLabelOf(const Scene& scene, EntityId id) {
    // First: being a prefab constrains editing. Only the root holds PrefabInstance;
    // PrefabEntity is on the whole subtree.
    if (scene.has<PrefabInstance>(id)) return {"Prefab", EditorIcon::Prefab};
    if (scene.has<Camera>(id)) return {"Camera", EditorIcon::Camera};
    if (const Light* light = scene.tryGet<Light>(id)) return lightLabelOf(light->type);
    // Before Mesh: an Animator makes it the rig (its meshes are children); a socket likewise.
    if (scene.has<Animator>(id)) return {"Rig", EditorIcon::Anim};
    if (scene.has<BoneSocket>(id)) return {"Socket", EditorIcon::Socket};
    if (scene.has<Mesh>(id)) {
        if (scene.has<Animation>(id)) return {"Animated Mesh", EditorIcon::Mesh};
        if (scene.has<LOD>(id))       return {"LOD Mesh",      EditorIcon::Mesh};
        return {"Mesh", EditorIcon::Mesh};
    }
    if (scene.has<Animation>(id))        return {"Animation", EditorIcon::Anim};
    if (scene.has<ReflectionProbe>(id))  return {"Probe",     EditorIcon::Probe};
    if (scene.has<IrradianceVolume>(id)) return {"GI Volume", EditorIcon::Volume};
    if (scene.has<Decal>(id))            return {"Decal",     EditorIcon::Decal};
    if (scene.has<ParticleEmitter>(id))  return {"Emitter",   EditorIcon::Particle};
    if (const AudioSource* source = scene.tryGet<AudioSource>(id)) {
        return source->spatial
            ? EntityLabel{"Sound",    EditorIcon::Audio}
            : EntityLabel{"2D Sound", EditorIcon::Audio2D};
    }
    if (scene.has<AudioListener>(id))    return {"Listener",  EditorIcon::Listener};
    if (scene.has<UIButton>(id))         return {"Button",    EditorIcon::UIButton};
    if (scene.has<UIText>(id))           return {"Text",      EditorIcon::UIText};
    if (scene.has<UIScroll>(id))         return {"Scroll",    EditorIcon::UIScroll};
    if (scene.has<UIImage>(id))          return {"Panel",     EditorIcon::UIImage};
    if (scene.has<UICanvas>(id))         return {"Canvas",    EditorIcon::UICanvas};
    if (scene.has<UIElement>(id))        return {"Widget",    EditorIcon::UIWidget};
    // Below the visual rows: a crate keeps its mesh glyph; these catch what has no other face.
    if (scene.has<CharacterController>(id)) return {"Character", EditorIcon::Character};
    if (scene.has<Ragdoll>(id))          return {"Ragdoll",   EditorIcon::Ragdoll};
    if (scene.has<Joint>(id))            return {"Jointed Body", EditorIcon::Joint};
    if (scene.has<Collider>(id))         return {"Collider",  EditorIcon::Colliders};
    if (scene.has<Rigidbody>(id))        return {"Body",      EditorIcon::Colliders};
    return {"Entity", EditorIcon::Entity};
}

} // namespace

void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize) {
    const Name* name = scene.tryGet<Name>(id);
    if (name && name->value[0] != '\0') {
        snprintf(buf, bufSize, "%s", name->value);
        return;
    }
    snprintf(buf, bufSize, "%s %u", entityLabelOf(scene, id).name, id.slot());
}

namespace {
// Shared by the reserved label space and the drawn icon.
float rowIconRadius() { return ImGui::GetFontSize() * 0.52f; }

// Leading spaces that clear the glyph; an optional id keeps ImGui ids stable when names collide.
void iconPaddedLabel(char* out, size_t n, const char* name, const char* idStr) {
    const float sw = ImGui::CalcTextSize(" ").x;
    int pad = (sw > 0.0f)
        ? static_cast<int>((rowIconRadius() * 2.0f + EditorStyle::px(6.0f)) / sw) + 1 : 4;
    if (pad < 2)  pad = 2;
    if (pad > 18) pad = 18;
    char sp[20];
    for (int i = 0; i < pad; ++i) sp[i] = ' ';
    sp[pad] = '\0';
    if (idStr) snprintf(out, n, "%s%s###%s", sp, name, idStr);
    else       snprintf(out, n, "%s%s", sp, name);
}
void drawRowGlyph(EditorIcon ic, float startX, ImVec2 rmin, float rh) {
    const float iconR = rowIconRadius();
    drawEditorIcon(
        ImGui::GetWindowDrawList(),
        ic,
        ImVec2(startX + iconR, rmin.y + rh * 0.5f),
        iconR,
        ImGui::GetColorU32(ImGuiCol_Text)
    );
}
} // namespace

EditorIcon entityIconKind(const Scene& scene, EntityId id) {
    return entityLabelOf(scene, id).icon;
}

EditorIcon lightIcon(LightType type) {
    return lightLabelOf(type).icon;
}

void inlineIcon(EditorIcon icon, float size, ImU32 color) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawEditorIcon(
        ImGui::GetWindowDrawList(),
        icon,
        ImVec2(p.x + size * 0.5f, p.y + size * 0.5f),
        size * 0.40f,
        color
    );
}

bool entityTreeNode(const void* idPtr, ImGuiTreeNodeFlags flags, EditorIcon icon, const char* name) {
    char label[96];
    iconPaddedLabel(label, sizeof(label), name, nullptr);
    // Touching rows read as one list and fit more.
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(ImGui::GetStyle().FramePadding.x, EditorStyle::px(3.0f))
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, EditorStyle::px(1.0f))
    );
    const bool open = ImGui::TreeNodeEx(const_cast<void*>(idPtr), flags, "%s", label);
    ImGui::PopStyleVar(2);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const float  rh   = ImGui::GetItemRectSize().y;
    drawRowGlyph(icon, rmin.x + ImGui::GetTreeNodeToLabelSpacing(), rmin, rh);
    return open;
}

bool iconMenuItem(EditorIcon icon, const char* label, const char* shortcut, bool enabled) {
    char padded[96];
    iconPaddedLabel(padded, sizeof(padded), label, nullptr);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::MenuItem(padded, shortcut, false, enabled);
    drawRowGlyph(icon, p.x + EditorStyle::px(4.0f), p, ImGui::GetItemRectSize().y);
    return pressed;
}

bool entitySelectable(const char* idStr, bool selected, EditorIcon icon, const char* name) {
    char label[96];
    iconPaddedLabel(label, sizeof(label), name, idStr);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable(label, selected);
    drawRowGlyph(icon, p.x + EditorStyle::px(4.0f), p, ImGui::GetItemRectSize().y);
    return clicked;
}

} // namespace Vkm::Engine

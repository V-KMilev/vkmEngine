#include "ui/editor_widgets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include <imgui.h>

#include "ui/editor_style.h"
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
#include "ecs/component/ui/ui_text.h"

namespace Vkm::Engine {

bool drawVec3Control(const char* label, float* values,
                     float resetValue, float speed) {
    bool changed = false;
    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize(lineHeight + EditorStyle::px(2.0f), lineHeight);
    // Floored, because the share left over goes to zero on a narrow panel and the
    // three drags disappear - a Transform card reduced to axis buttons with no
    // number to drag. Overflowing is the lesser failure; the panel resizes.
    float inputWidth = std::max((ImGui::GetContentRegionAvail().x - EditorStyle::labelWidth()
                                 - buttonSize.x * 3 - ImGui::GetStyle().ItemSpacing.x * 5) / 3.0f,
                                ImGui::GetFontSize() * 2.5f);

    // Column measured from the row's start (card-indent aware), like
    // drawPropertyLabel.
    const float startX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(startX + EditorStyle::labelWidth());

    static const struct {
        const char*   button;
        const char*   drag;
        const ImVec4& color;
        const ImVec4& hover;
    } axes[3] = {
        { "X", "##X", EditorStyle::AXIS_X, EditorStyle::AXIS_X_HOV },
        { "Y", "##Y", EditorStyle::AXIS_Y, EditorStyle::AXIS_Y_HOV },
        { "Z", "##Z", EditorStyle::AXIS_Z, EditorStyle::AXIS_Z_HOV },
    };

    for (int i = 0; i < 3; ++i) {
        ImGui::PushStyleColor(ImGuiCol_Button, axes[i].color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, axes[i].hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, axes[i].hover);
        if (ImGui::Button(axes[i].button, buttonSize)) { values[i] = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, EditorStyle::px(2.0f));
        ImGui::SetNextItemWidth(inputWidth);
        changed |= ImGui::DragFloat(axes[i].drag, &values[i], speed, 0.0f, 0.0f, "%.2f");
        if (i < 2) ImGui::SameLine(0, EditorStyle::px(6.0f));
    }

    ImGui::PopID();
    return changed;
}

void drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();

    // A fixed column measured from the row's start, card indent included -
    // measured from the window edge, a wide label inside an indented card slides
    // under its widget. A long label ellipsizes rather than pushing the widget.
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
    float  startY = 0.0f;   // body top, screen-space y
    float  lineX  = 0.0f;   // left accent-line x, screen-space
    int    frame  = 0;      // the ImGui frame that pushed it
    bool   open   = false;
};
// Accessor instead of a bare global, so the lifetime stays explicit.
// thread_local because the only context where it is valid is the ImGui-owning
// thread.
std::vector<CardState>& cardStack() {
    thread_local std::vector<CardState> s;
    return s;
}
// A function, not a constant: px() reads the live font and there is no ImGui
// context yet when a file-scope initializer runs.
float cardIndent() { return EditorStyle::px(14.0f); }

// Tinted, accent-stripped CollapsingHeader (no body/end pairing). File-local -
// only beginComponentCard below uses it.
bool styledCollapsingHeader(const char* title, const ImVec4& accent,
                            bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_AllowOverlap
                             | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushStyleColor(ImGuiCol_Header,        EditorStyle::CARD_HEADER);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorStyle::CARD_HEADER_HOV);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  EditorStyle::CARD_HEADER_ACT);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(EditorStyle::px(8.0f), EditorStyle::px(7.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorStyle::px(4.0f));

    const bool open = ImGui::CollapsingHeader(title, flags);
    const ImVec2 rMin = ImGui::GetItemRectMin();
    const ImVec2 rMax = ImGui::GetItemRectMax();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // Accent strip welded to the header's left edge.
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(rMin.x, rMin.y), ImVec2(rMin.x + EditorStyle::px(3.0f), rMax.y),
        ImGui::GetColorU32(accent));
    return open;
}
} // namespace

bool beginComponentCard(const char* title, const ImVec4& accent,
                        bool defaultOpen, bool* removeClicked) {
    // A card whose end was skipped would otherwise sit on this stack for the
    // rest of the session, shifting every later card's guide line. ImGui resets
    // its own id and indent stacks per frame; this one follows.
    std::vector<CardState>& stack = cardStack();
    if (!stack.empty() && stack.back().frame != ImGui::GetFrameCount()) stack.clear();

    ImGui::PushID(title);
    ImGui::Spacing();

    const bool open   = styledCollapsingHeader(title, accent, defaultOpen);
    const ImVec2 rMin = ImGui::GetItemRectMin();

    if (removeClicked) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x
                        + ImGui::GetCursorPosX() - EditorStyle::px(20.0f));
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
    // No matching begin, so there is no PushID of ours to pop either.
    if (stack.empty()) return;

    const CardState st = stack.back();
    stack.pop_back();

    if (st.open) {
        ImGui::Spacing();
        const float endY = ImGui::GetCursorScreenPos().y;
        ImGui::Unindent(cardIndent());
        const ImU32 c = ImGui::GetColorU32(ImVec4(
            st.accent.x, st.accent.y, st.accent.z, 0.30f));
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(st.lineX, st.startY), ImVec2(st.lineX, endY), c,
            EditorStyle::px(2.0f));
    }
    ImGui::PopID();
    ImGui::Spacing();
}

bool drawEasingCombo(const char* id, EasingFunction& easing) {
    const int current = Easing::indexOf(easing);
    ImGui::SetNextItemWidth(-1);
    if (!ImGui::BeginCombo(id, Easing::EASINGS[current].name)) return false;

    bool changed = false;
    for (int i = 0; i < Easing::EASING_COUNT; ++i) {
        const bool selected = (i == current);
        if (ImGui::Selectable(Easing::EASINGS[i].name, selected)) {
            easing = Easing::byIndex(i);
            changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

namespace {
// Byte offsets of the next / previous character. Never land inside a UTF-8
// sequence: a lone continuation byte renders as the font's replacement box.
size_t utf8Next(const char* s, size_t i, size_t len) {
    for (++i; i < len && (s[i] & 0xC0) == 0x80; ++i) {}
    return i;
}

size_t utf8Prev(const char* s, size_t i) {
    for (--i; i > 0 && (s[i] & 0xC0) == 0x80; --i) {}
    return i;
}

/// Width of a line keeping [0, head) and [tail, len) with the ellipsis between.
float keptWidth(const char* s, size_t head, size_t tail, size_t len) {
    return ImGui::CalcTextSize(s, s + head).x + ImGui::CalcTextSize(s + tail, s + len).x;
}

} // namespace

void clippedLine(const char* text, float maxWidth, bool dim) {
    const char* str = (text && text[0]) ? text : "(unnamed)";
    char buf[192];
    if (ImGui::CalcTextSize(str).x > maxWidth) {
        const size_t len    = std::strlen(str);
        const float  budget = maxWidth - ImGui::CalcTextSize("...").x;

        // Grown one character in from each end in turn, so the two halves stay
        // the same length whichever end the wide characters are at.
        size_t head = 0;
        size_t tail = len;
        for (;;) {
            const size_t grownHead = utf8Next(str, head, len);
            if (grownHead >= tail || keptWidth(str, grownHead, tail, len) > budget) break;
            head = grownHead;

            const size_t grownTail = utf8Prev(str, tail);
            if (grownTail <= head || keptWidth(str, head, grownTail, len) > budget) break;
            tail = grownTail;
        }
        snprintf(buf, sizeof(buf), "%.*s...%s", static_cast<int>(head), str, str + tail);
        str = buf;
    }
    if (dim) ImGui::TextDisabled("%s", str);
    else     ImGui::TextUnformatted(str);
}

bool matchesFilter(const char* text, const char* filter) {
    if (!filter || !filter[0]) return true;
    for (const char* p = text; *p; ++p) {
        const char* s = filter;
        const char* t = p;
        while (*s && *t && tolower(static_cast<unsigned char>(*s)) ==
                            tolower(static_cast<unsigned char>(*t))) { ++s; ++t; }
        if (!*s) return true;
    }
    return false;
}

namespace {

// What kind of thing an entity is, answered once for both the label the
// hierarchy shows and the glyph beside it. The two used to be separate ladders
// over the same components and had already diverged - the icons grew a
// UIElement row the names never got, so a bare UI widget drew the widget glyph
// beside the text "Entity 12", and the inspector's "name this entity" button
// baked that string into a real Name.
//
// Stays a hand-written ladder rather than a component->row table: a Light's
// answer comes from its inner type, and a Mesh carrying an Animation reads
// "Animated Mesh" while keeping the plain mesh glyph. Order is precedence.
struct EntityLabel {
    const char* name;
    EditorIcon  icon;
};

EntityLabel entityLabelOf(const Scene& scene, EntityId id) {
    // Above everything: an instance is a prefab whatever else it carries, and
    // that is the fact which constrains how it may be edited. Only the root
    // holds one - PrefabEntity is stamped on every entity in the subtree, so a
    // row for it would say "prefab" about an entire hierarchy and identify
    // nothing.
    if (scene.has<PrefabInstance>(id)) return {"Prefab", EditorIcon::Prefab};
    if (scene.has<Camera>(id)) return {"Camera", EditorIcon::Camera};
    if (scene.has<Light>(id)) {
        switch (scene.get<Light>(id).type) {
            case LightType::Directional: return {"Dir Light",   EditorIcon::LightDir};
            case LightType::Point:       return {"Point Light", EditorIcon::LightPoint};
            case LightType::Spot:        return {"Spot Light",  EditorIcon::LightSpot};
            case LightType::Rect:        return {"Rect Light",  EditorIcon::LightRect};
            case LightType::Disk:        return {"Disk Light",  EditorIcon::LightDisk};
            case LightType::Count:       break;  // enum-size sentinel, never stored
        }
        return {"Light", EditorIcon::LightPoint};
    }
    // Before Mesh: an entity carrying an Animator is the rig whatever else it
    // carries, and its meshes are the entities under it. A socket reads the
    // same way - a sword riding a hand is a socket first and geometry second,
    // because where it is attached is what someone is looking for.
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
    if (scene.has<AudioSource>(id)) {
        return scene.get<AudioSource>(id).spatial
            ? EntityLabel{"Sound",    EditorIcon::Audio}
            : EntityLabel{"2D Sound", EditorIcon::Audio2D};
    }
    if (scene.has<AudioListener>(id))    return {"Listener",  EditorIcon::Listener};
    if (scene.has<UIButton>(id))         return {"Button",    EditorIcon::UIButton};
    if (scene.has<UIText>(id))           return {"Text",      EditorIcon::UIText};
    if (scene.has<UIImage>(id))          return {"Panel",     EditorIcon::UIImage};
    if (scene.has<UICanvas>(id))         return {"Canvas",    EditorIcon::UICanvas};
    if (scene.has<UIElement>(id))        return {"Widget",    EditorIcon::UIWidget};
    // Physics rows sit below the visual ones on purpose: a crate keeps its mesh
    // glyph however it collides, and these catch what has no other face - a
    // character capsule, a ragdoll's bones, an invisible blocker.
    if (scene.has<CharacterController>(id)) return {"Character", EditorIcon::Character};
    if (scene.has<Ragdoll>(id))          return {"Ragdoll",   EditorIcon::Ragdoll};
    if (scene.has<Joint>(id))            return {"Jointed Body", EditorIcon::Joint};
    if (scene.has<Collider>(id))         return {"Collider",  EditorIcon::Colliders};
    if (scene.has<Rigidbody>(id))        return {"Body",      EditorIcon::Colliders};
    return {"Entity", EditorIcon::Entity};
}

} // namespace

void getEntityDisplayName(const Scene& scene, EntityId id,
                          char* buf, size_t bufSize) {
    if (scene.has<Name>(id)) {
        const auto& name = scene.get<Name>(id);
        if (name.value[0] != '\0') {
            snprintf(buf, bufSize, "%s", name.value);
            return;
        }
    }
    snprintf(buf, bufSize, "%s %u", entityLabelOf(scene, id).name, id.slot());
}

namespace {
// One source of truth for the entity-row glyph size, so the reserved
// label space and the drawn icon always agree.
float rowIconRadius() { return ImGui::GetFontSize() * 0.62f; }

// Leading spaces that clear the glyph, so a row's text starts to the
// right of the icon drawn into that gap. An optional id keeps ImGui ids
// stable when names collide.
void iconPaddedLabel(char* out, size_t n, const char* name,
                     const char* idStr) {
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
    drawEditorIcon(ImGui::GetWindowDrawList(), ic,
        ImVec2(startX + iconR, rmin.y + rh * 0.5f), iconR,
        ImGui::GetColorU32(ImGuiCol_Text));
}
} // namespace

EditorIcon entityIconKind(const Scene& scene, EntityId id) {
    return entityLabelOf(scene, id).icon;
}

void inlineIcon(EditorIcon icon, float size, ImU32 color) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawEditorIcon(ImGui::GetWindowDrawList(), icon,
        ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.40f, color);
}

bool entityTreeNode(const void* idPtr, ImGuiTreeNodeFlags flags,
                    EditorIcon icon, const char* name) {
    char label[96];
    iconPaddedLabel(label, sizeof(label), name, nullptr);
    const bool open = ImGui::TreeNodeEx(const_cast<void*>(idPtr), flags, "%s", label);
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

bool entitySelectable(const char* idStr, bool selected,
                      EditorIcon icon, const char* name) {
    char label[96];
    iconPaddedLabel(label, sizeof(label), name, idStr);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable(label, selected);
    drawRowGlyph(icon, p.x + EditorStyle::px(4.0f), p, ImGui::GetItemRectSize().y);
    return clicked;
}

} // namespace Vkm::Engine

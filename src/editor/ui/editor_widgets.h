#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "core/reflect.h"
#include "core/math/easing.h"
#include "ui/editor_style.h"
#include "ui/editor_icons.h"

namespace Engine {

class Scene;

/**
 * @brief XYZ vector control: three drag floats with colored reset buttons (X/Y/Z).
 *
 * Returns true the frame any axis is edited. @p resetValue is restored on
 * button click; @p speed is the ImGui drag speed for each axis.
 */
bool drawVec3Control(const char* label, float* values,
                     float resetValue = 0.0f, float speed = 0.1f);

/**
 * @brief Draw a right-aligned property label with a consistent column width.
 *
 * Keeps the label column aligned across every property row in a panel and
 * sets the next item to full width so the paired widget fills the remainder.
 *
 * @param label Text shown in the label column.
 */
void drawPropertyLabel(const char* label);

/**
 * @brief A full "property row": right-aligned label + a full-width control +
 * optional hover tooltip. propRow is the shared core; the prop* wrappers below
 * each supply one ImGui control.
 *
 * The widget id is scoped by @p label (PushID) with a hidden "##v" handle, so
 * rows with distinct labels never collide. @p widget is a callable that draws
 * the control and returns whether it was edited; propRow returns that.
 */
template <typename Widget>
inline bool propRow(const char* label, const char* tooltip, Widget&& widget) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool changed = widget();
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return changed;
}

/// Property row backed by a SliderFloat over [lo, hi].
inline bool propSlider(const char* label, float* v, float lo, float hi,
                       const char* fmt = "%.3f", const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::SliderFloat("##v", v, lo, hi, fmt); });
}

/// Property row backed by an integer SliderInt over [lo, hi].
inline bool propSliderInt(const char* label, int* v, int lo, int hi,
                          const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::SliderInt("##v", v, lo, hi); });
}

/// Property row backed by a DragFloat with the given drag speed and clamp.
inline bool propDrag(const char* label, float* v, float speed, float lo, float hi,
                     const char* fmt = "%.3f", const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::DragFloat("##v", v, speed, lo, hi, fmt); });
}

/// Property row backed by an integer DragInt with the given drag speed and clamp.
inline bool propDragInt(const char* label, int* v, float speed, int lo, int hi,
                        const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::DragInt("##v", v, speed, lo, hi); });
}

/// Property row backed by a 3-component DragFloat3 (shared clamp/speed per axis).
inline bool propDrag3(const char* label, float* v, float speed, float lo, float hi,
                      const char* fmt = "%.3f", const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::DragFloat3("##v", v, speed, lo, hi, fmt); });
}

/// Property row backed by an RGB ColorEdit3.
inline bool propColor3(const char* label, float* v,
                       ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float,
                       const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::ColorEdit3("##v", v, flags); });
}

/// Property row backed by an RGBA ColorEdit4.
inline bool propColor4(const char* label, float* v,
                       ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float,
                       const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::ColorEdit4("##v", v, flags); });
}

/// Property row backed by a Checkbox (the box sits right after the label column).
inline bool propCheckbox(const char* label, bool* v, const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::Checkbox("##v", v); });
}

/**
 * @brief Property row backed by a Combo over a fixed table of raw values.
 *
 * For quality steps stored as plain numbers (MSAA samples, atlas resolution):
 * @p labels and @p values are parallel arrays of @p count entries. A stored
 * value not present in the table previews as "?" until edited.
 *
 * @param label   Property label (also the ImGui id scope).
 * @param labels  Display string per selectable value.
 * @param values  The raw value each label maps to.
 * @param count   Entries in both arrays.
 * @param v       The edited value.
 * @param tooltip Optional hover tooltip.
 * @return Whether the value changed this frame.
 */
inline bool propValueCombo(const char* label, const char* const* labels,
                           const uint32_t* values, int count, uint32_t* v,
                           const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] {
        int idx = -1;
        for (int i = 0; i < count; ++i)
            if (values[i] == *v) idx = i;
        bool changed = false;
        if (ImGui::BeginCombo("##v", idx >= 0 ? labels[idx] : "?")) {
            for (int i = 0; i < count; ++i) {
                if (ImGui::Selectable(labels[i], i == idx) && i != idx) {
                    *v = values[i];
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    });
}

/// Property row: an integer drag staged over a uint32_t member.
inline bool propDragU32(const char* label, uint32_t* v, float speed,
                        uint32_t lo, uint32_t hi, const char* tooltip = nullptr) {
    int staged = static_cast<int>(*v);
    const bool changed = propDragInt(label, &staged, speed,
                                     static_cast<int>(lo), static_cast<int>(hi), tooltip);
    if (changed) *v = static_cast<uint32_t>(staged < 0 ? 0 : staged);
    return changed;
}

/// Property row: a degrees drag staged over a radians-stored member.
inline bool propAngleDrag(const char* label, float* radians, float speed,
                          float loDeg, float hiDeg, const char* tooltip = nullptr) {
    float deg = glm::degrees(*radians);
    const bool changed = propDrag(label, &deg, speed, loDeg, hiDeg, "%.1f deg", tooltip);
    if (changed) *radians = glm::radians(deg);
    return changed;
}

/// Property row: a degrees slider staged over a radians-stored member.
inline bool propAngleSlider(const char* label, float* radians,
                            float loDeg, float hiDeg, const char* tooltip = nullptr) {
    float deg = glm::degrees(*radians);
    const bool changed = propSlider(label, &deg, loDeg, hiDeg, "%.0f deg", tooltip);
    if (changed) *radians = glm::radians(deg);
    return changed;
}

/**
 * @brief Property row: a string edit staged through a fixed buffer.
 *
 * imgui_stdlib isn't compiled in, so the edit round-trips a 256-byte stack
 * buffer; longer strings are clamped on edit.
 */
inline bool propString(const char* label, std::string& s, const char* tooltip = nullptr) {
    char buf[256];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    const bool changed = propRow(label, tooltip, [&] { return ImGui::InputText("##v", buf, sizeof(buf)); });
    if (changed) s = buf;
    return changed;
}

/**
 * @brief Full-width Rebake button: bumps @p bakeVersion when pressed.
 *
 * Shared by the reflection-probe and irradiance-volume cards (and anything
 * else whose backend re-bakes on a version change).
 */
inline bool rebakeButton(uint32_t& bakeVersion) {
    if (ImGui::Button("Rebake", ImVec2(-1, 0))) {
        ++bakeVersion;
        return true;
    }
    return false;
}

/**
 * @brief Section heading inside a panel or popup.
 *
 * One treatment for group headings (Preferences keybind groups, popup
 * headers), distinct from TextDisabled - which stays for hints and metadata
 * so the three no longer share one grey.
 */
inline void sectionLabel(const char* text) {
    ImGui::TextColored(EditorStyle::HEADER_TEXT, "%s", text);
}

/**
 * @brief Test whether a string contains a filter substring, case-insensitively.
 *
 * @param text Candidate string being filtered.
 * @param filter Needle to search for; an empty filter matches every text.
 * @return true when filter occurs in text ignoring case (or filter is empty).
 */
bool matchesFilter(const char* text, const char* filter);

/**
 * @brief A MenuItem with a leading entity/tool glyph.
 *
 * Draws the icon into label padding the same way the hierarchy rows do, so
 * menus (Create, context menus) carry the same iconography as the tree.
 *
 * @param icon     Glyph drawn ahead of the label.
 * @param label    Menu item text.
 * @param shortcut Optional right-aligned shortcut label.
 * @param enabled  Standard MenuItem enabled flag.
 * @return true on the frame the item is activated.
 */
bool iconMenuItem(EditorIcon icon, const char* label, const char* shortcut = nullptr,
                  bool enabled = true);

/**
 * @brief Modern component "card": a framed, accent-colored collapsible block.
 *
 * Replaces a bare CollapsingHeader so each Inspector component reads as a
 * distinct grouped unit (the Unity / Godot idiom).
 *
 * Always pair with endComponentCard(). When @p removeClicked is non-null a
 * small "x" is drawn on the header row and *removeClicked is set true the
 * frame it is pressed (caller does the actual removal AFTER endComponentCard).
 *
 * @return true when the body is expanded and should be drawn.
 */
bool beginComponentCard(const char* title, const ImVec4& accent,
                        bool defaultOpen, bool* removeClicked = nullptr);
void endComponentCard();

/**
 * @brief Easing-function dropdown. Lists all named easings; on selection updates
 * @p easing in place and returns true. @p id must be a unique ImGui id
 * (e.g. "##easePos"). Sets the next item width to fill the row.
 */
bool drawEasingCombo(const char* id, EasingFunction& easing);

/**
 * @brief Enum dropdown: one row per name, writes the picked index back into
 * @p value. Returns true on change. Fills the row, so it pairs with
 * drawPropertyLabel. Replaces the int-cast/Combo/cast-back boilerplate.
 *
 * Names + count come from the enum's VKM_ENUM_NAMES registration, so the combo
 * and the JSON (de)serialization share one source and cannot drift.
 */
template <typename E>
bool drawEnumCombo(const char* id, E& value) {
    using Names = Reflect::EnumNames<E>;
    int idx = static_cast<int>(value);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo(id, &idx, Names::values, static_cast<int>(Names::count))) {
        value = static_cast<E>(idx);
        return true;
    }
    return false;
}

/**
 * @brief Property row wrapping drawEnumCombo: right-aligned label + full-width
 * enum dropdown, so an enum field reads like the other prop* rows in a card.
 */
template <typename E>
bool propEnumCombo(const char* label, E& value) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool changed = drawEnumCombo("##v", value);
    ImGui::PopID();
    return changed;
}

/**
 * @brief Stable Euler-angle edit cache for quaternion-backed rotations.
 *
 * Quaternion -> Euler is many-to-one and singular at +/-90 deg (gimbal
 * lock); re-deriving the display from the stored quaternion every frame
 * makes typed axes snap to +/-180 and the orthogonal axis jitter. This
 * cache keeps the edited Euler as the source of truth and only re-seeds
 * from the quaternion when it changed externally (selection switched,
 * gizmo drag, scene load, etc.).
 *
 * Usage:
 *   EulerCache<EntityId> cache;  // panel member
 *   ...
 *   cache.sync(id, q);           // before drawing the field
 *   if (drawVec3Control(..., cache.degrees(), ...)) q = cache.toQuat();
 *
 * The Key template parameter is whatever identifies "this is the same
 * rotation source" - an EntityId for inspectors, a keyframe index for
 * animation tracks. A different key forces a re-seed from @p q.
 */
template<class Key>
class EulerCache {
    public:
        /**
         * @brief Reseed the cache from @p q if the key changed or @p q diverged
         * from the cached quaternion (rotation set from outside).
         */
        void sync(const Key& key, const glm::quat& q) {
            const glm::quat cached = glm::quat(glm::radians(m_degrees));
            const bool keyChanged   = !m_haveKey || !(m_key == key);
            const bool quatDiverged = glm::abs(glm::dot(cached, q)) < 0.9999f;
            if (keyChanged || quatDiverged) {
                m_degrees = glm::degrees(glm::eulerAngles(q));
                m_key     = key;
                m_haveKey = true;
            }
        }

        float* degrees() { return &m_degrees.x; }
        glm::quat toQuat() const { return glm::normalize(glm::quat(glm::radians(m_degrees))); }

    private:
        glm::vec3 m_degrees{0.0f};
        Key       m_key{};
        bool      m_haveKey = false;
};

/**
 * @brief Write the user-visible name of @p id into @p buf (falls back to a default
 * like "Entity 7" when the entity has no Name component).
 */
void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize);

/**
 * @brief Which entity-type glyph represents @p id (camera / light variant / mesh /
 * animation / generic).
 */
EditorIcon entityIconKind(const Scene& scene, EntityId id);

/**
 * @brief Draw a non-interactive vector icon inline: reserves a @p size square at the
 * cursor and renders @p icon centered in it (pair with SameLine + text).
 */
void inlineIcon(EditorIcon icon, float size, ImU32 color);

/**
 * @brief A tree node row prefixed with a type glyph: <arrow> <icon> <name>.
 * Forwards to TreeNodeEx; the caller still handles click / drag / context.
 * Returns the TreeNodeEx result.
 */
bool entityTreeNode(const void* idPtr, ImGuiTreeNodeFlags flags,
                    EditorIcon icon, const char* name);

/**
 * @brief A Selectable row prefixed with a type glyph. @p idStr keeps the ImGui id
 * stable when names collide. Returns true the frame it is clicked.
 */
bool entitySelectable(const char* idStr, bool selected,
                      EditorIcon icon, const char* name);

} // namespace Engine

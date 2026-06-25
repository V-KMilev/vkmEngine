#pragma once

#include <cstddef>
#include <cstdint>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "system/animation/easing.h"
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

/** @brief Right-aligned property label with consistent column width across the panel. */
void drawPropertyLabel(const char* label);

/** @brief Case-insensitive substring match. Empty @p filter matches every @p text. */
bool matchesFilter(const char* text, const char* filter);

/**
 * @brief Modern component "card": a framed, accent-colored collapsible block.
 *
 * Replaces a bare CollapsingHeader so each Inspector component reads as a
 * distinct grouped unit (the Unity / Godot idiom): a left accent strip, a
 * tinted header, indented body with a faint accent guide line, and an
 * optional inline remove affordance.
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
 * @brief Consistent panel title: header-tinted text with a full-width accent rule.
 * Replaces the old "TextUnformatted + Separator + Spacing" boilerplate.
 */
void drawPanelTitle(const char* title);

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
 */
template <typename E>
bool drawEnumCombo(const char* id, E& value, const char* const names[], int count) {
    int idx = static_cast<int>(value);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo(id, &idx, names, count)) {
        value = static_cast<E>(idx);
        return true;
    }
    return false;
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
 * animation / generic). Replaces the [C]/[M] ASCII badge.
 */
EditorIcon entityIconKind(const Scene& scene, EntityId id);

/**
 * @brief Draw a non-interactive vector icon inline: reserves a @p size square at the
 * cursor and renders @p icon centered in it (pair with SameLine + text).
 */
void inlineIcon(EditorIcon icon, float size, ImU32 color);

/**
 * @brief A tree node row prefixed with a type glyph (replaces the [C] ASCII):
 * <arrow> <icon> <name>. Forwards to TreeNodeEx; the caller still handles
 * click / drag / context exactly as before. Returns the TreeNodeEx result.
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

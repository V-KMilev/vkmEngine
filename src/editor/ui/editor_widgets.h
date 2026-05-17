#pragma once

#include <cstddef>
#include <cstdint>

#include <imgui.h>

#include "ecs/entity.h"
#include "system/animation/easing.h"

namespace Engine {

class Scene;

/**
 * @brief Reusable ImGui widget helpers for the editor.
 *
 * Free functions for common UI patterns: XYZ vector controls with colored reset
 * buttons, aligned property labels, component remove buttons, fuzzy search,
 * and entity display name/icon generation.
 */
bool drawVec3Control(const char* label, float* values,
                     float resetValue = 0.0f, float speed = 0.1f);
void drawPropertyLabel(const char* label);
bool drawRemoveButton(const char* compLabel, uint32_t entityIdx);
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

/// The component-card header on its own: a tinted, rounded CollapsingHeader
/// with a left accent strip. Drop-in for ImGui::CollapsingHeader (no end
/// pairing, no body indent) - used so dense panels (Bottom, etc.) read in
/// the same visual language as the Inspector without restructuring.
bool styledCollapsingHeader(const char* title, const ImVec4& accent,
                            bool defaultOpen = false);

/// Consistent panel title: header-tinted text with a full-width accent rule.
/// Replaces the old "TextUnformatted + Separator + Spacing" boilerplate.
void drawPanelTitle(const char* title);

/// Section title with a dim hint to its right and the same accent rule.
void drawSectionHeader(const char* title, const char* hint);

/// Easing-function dropdown. Lists all named easings; on selection updates
/// @p easing in place and returns true. @p id must be a unique ImGui id
/// (e.g. "##easePos"). Sets the next item width to fill the row.
bool drawEasingCombo(const char* id, EasingFunction& easing);

void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize);
void getEntityIcon(const Scene& scene, EntityId id, char* buf, size_t bufSize);

} // namespace Engine

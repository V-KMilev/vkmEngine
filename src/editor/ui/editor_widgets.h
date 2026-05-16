#pragma once

#include <cstddef>
#include <cstdint>

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

/// Easing-function dropdown. Lists all named easings; on selection updates
/// @p easing in place and returns true. @p id must be a unique ImGui id
/// (e.g. "##easePos"). Sets the next item width to fill the row.
bool drawEasingCombo(const char* id, EasingFunction& easing);

void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize);
void getEntityIcon(const Scene& scene, EntityId id, char* buf, size_t bufSize);

} // namespace Engine

#pragma once

#include <cstddef>

#include "ecs/entity.h"
#include "ui/editor_icons.h"

namespace Vkm::Engine {

class Scene;
enum class LightType;

/**
 * @brief Write the user-visible name of @p id into @p buf.
 *
 * Falls back to kind and slot ("Camera 7") without a non-empty Name.
 *
 * @param scene   The scene @p id lives in.
 * @param id      Entity named; it need not carry a Name.
 * @param buf     Receives the name, truncated.
 * @param bufSize Capacity of @p buf.
 */
void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize);

/**
 * @brief Which entity-type glyph represents @p id.
 *
 * Shares getEntityDisplayName's fallback ladder, so glyph and label agree.
 *
 * @param scene The scene @p id lives in.
 * @param id Its components choose the glyph.
 * @return The glyph for its kind.
 */
EditorIcon entityIconKind(const Scene& scene, EntityId id);

/**
 * @brief The glyph for a light of @p type.
 *
 * entityIconKind's light rung on its own, so a light's row and marker agree.
 *
 * @param type The light's type.
 * @return Its glyph; the point light's for a value out of range.
 */
EditorIcon lightIcon(LightType type);

} // namespace Vkm::Engine

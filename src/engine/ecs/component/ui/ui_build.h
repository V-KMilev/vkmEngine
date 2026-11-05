#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

} // namespace Vkm::Engine

/**
 * @brief The engine's controls, each assembled from the UI components and placed under a parent:
 *        a game's HUD and menus in a line each. docs/recipes.md shows them.
 */
namespace Vkm::Engine::UI {

/**
 * @brief What the controls look like: a game's own, passed to each, or the engine's.
 */
struct Style {
    std::string font     = DEFAULT_UI_FONT;
    float       textSize = 22.0f;                     ///< Canvas reference pixels.
    glm::vec4   text     = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4   button   = UIButton{}.normalColor;
    glm::vec4   hover    = UIButton{}.hoverColor;
    glm::vec4   accent   = UIButton{}.hoverColor;     ///< A slider's fill, a toggle that is on.
    float       rounding = 6.0f;                      ///< Corner radius, reference pixels.
    glm::vec4   shadow   = {0.0f, 0.0f, 0.0f, 0.0f};  ///< Every text's drop shadow (UIText::shadow), so a
                                                      ///< HUD reads over the scene; none while clear.
};

/// A canvas named @p name, drawn over those of a lower @p sortOrder.
EntityId canvas(Scene& scene, std::string_view name, int sortOrder = 0);

/// A plain rectangle of @p color at @p at under @p parent.
EntityId panel(Scene& scene, EntityId parent, const UIElement& at, const glm::vec4& color);

/// @p words at @p at under @p parent, aligned the way the element is anchored.
EntityId text(Scene& scene, EntityId parent, const UIElement& at, std::string words, const Style& style = {});

/// An element at @p at under @p parent that lays its children out top to bottom, @p spacing apart (UIStack).
EntityId column(Scene& scene, EntityId parent, const UIElement& at, float spacing = 0.0f);

/// An element at @p at under @p parent that lays its children out left to right, @p spacing apart (UIStack).
EntityId row(Scene& scene, EntityId parent, const UIElement& at, float spacing = 0.0f);

/// A button saying @p caption; BehaviorBase::onClick hears it.
EntityId button(
    Scene& scene,
    EntityId parent,
    const UIElement& at,
    std::string caption,
    const Style& style = {}
);

/// A slider from @p min to @p max at @p value; BehaviorBase::onChange hears its value.
EntityId slider(
    Scene& scene,
    EntityId parent,
    const UIElement& at,
    float min,
    float max,
    float value,
    const Style& style = {}
);

/// A switch, @p on or off; BehaviorBase::onChange hears 1 or 0.
EntityId toggle(Scene& scene, EntityId parent, const UIElement& at, bool on, const Style& style = {});

/// One of @p options, the one at @p index shown between two arrows; BehaviorBase::onChange hears the index.
EntityId choice(
    Scene& scene,
    EntityId parent,
    const UIElement& at,
    std::vector<std::string> options,
    int index,
    const Style& style = {}
);

} // namespace Vkm::Engine::UI

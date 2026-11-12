#pragma once

#include "io/scene/scene_components.h"
#include "ui/style/editor_icons.h"

namespace Vkm::Engine {

struct ScriptComponent;

/**
 * @brief The Add Component menu's headings, in its order: ecs/component/'s folders, then the registered
 *        behaviors, which bring their ScriptComponent.
 */
enum class AddSection {
    Scene,
    Net,
    Render,
    Light,
    Effect,
    Landscape,
    Animation,
    Audio,
    Physics,
    UI,
    Behaviors,
    Count
};

/**
 * @brief A component's card as Add Component and the history name it; a null hint leaves it out of Add.
 */
struct CardInfo {
    const char* title   = "";
    AddSection  section = AddSection::Scene;
    const char* hint    = nullptr;
    EditorIcon  icon    = EditorIcon::None;
};

/// A type as a value, so an overload can be picked by it.
template <typename T>
struct Tag {
    using Type = T;
};

// One per scene component, from the list itself: a row without a card does not link.
#define VKM_CARD_INFO(Type, Key) CardInfo cardInfo(Tag<Type>);
VKM_SCENE_VALUE_COMPONENTS(VKM_CARD_INFO, VKM_CARD_INFO, VKM_CARD_INFO)
#undef VKM_CARD_INFO

CardInfo cardInfo(Tag<ScriptComponent>);

} // namespace Vkm::Engine

#include "system/script/behavior.h"

#include <utility>

#include "system/script/script_component.h"

namespace Vkm::Engine {

const Behavior* findBehaviorNamed(const Scene& scene, EntityId entity, std::string_view typeName) {
    const ScriptComponent* script = scene.tryGet<ScriptComponent>(entity);
    if (!script) return nullptr;

    for (const std::unique_ptr<Behavior>& behavior : script->behaviors) {
        if (behavior && typeName == behavior->typeName()) return behavior.get();
    }
    return nullptr;
}

Behavior* findBehaviorNamed(Scene& scene, EntityId entity, std::string_view typeName) {
    // The scene's own, so the behavior is as writable as the scene handed in.
    return const_cast<Behavior*>(findBehaviorNamed(std::as_const(scene), entity, typeName));
}

EntityId findEntityWithBehaviorNamed(Scene& scene, std::string_view typeName) {
    EntityId found{};
    scene.forEach<ScriptComponent>([&](EntityId entity, ScriptComponent&) {
        if (found && found.slot() < entity.slot()) return;
        if (findBehaviorNamed(scene, entity, typeName)) found = entity;
    });
    return found;
}

ScreenView Behavior::screen() const {
    if (!m_ctx->window || !m_ctx->chrome) return {};
    return ScreenView::of(*m_ctx->scene, *m_ctx->window, *m_ctx->chrome);
}

std::optional<Math::Ray> Behavior::pointerRay() const {
    return screen().rayThrough(m_ctx->input->pointer());
}

} // namespace Vkm::Engine

#include "lab_step_pad.h"

#include <glm/gtc/quaternion.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "system/audio/audio_events.h"

namespace Lab {

namespace {

// How far below the tile's middle, and above it, a character's feet count as standing on it, metres.
constexpr float BELOW = 0.3f;
constexpr float ABOVE = 0.6f;

} // namespace

void LabStepPad::onStart() {
    m_glow = HierarchyOperations::findNamed(scene(), entity(), "Glow");
}

bool LabStepPad::isStoodOn() {
    const Transform* at = tryGet<Transform>();
    if (!at) return false;
    const glm::vec3 middle  = resolvedWorldPosition(scene(), entity(), *at);
    const glm::quat inverse = glm::inverse(resolvedWorldRotation(scene(), entity(), *at));
    bool stood = false;
    scene().forEach<CharacterController, Transform>([&](EntityId, CharacterController&, Transform& feet) {
        const glm::vec3 local = inverse * (feet.position - middle);
        const bool across = glm::abs(local.x) <= halfSize.x && glm::abs(local.z) <= halfSize.y;
        stood = stood || (across && local.y >= -BELOW && local.y <= ABOVE);
    });
    return stood;
}

void LabStepPad::onUpdate(float dt) {
    const bool stood = isStoodOn();
    if (stood && !m_stood) {
        const Transform* at = tryGet<Transform>();
        const AudioClipHandle sound = resources().find(note);
        if (at && sound) {
            events().emit(PlaySoundEvent::at(sound, resolvedWorldPosition(scene(), entity(), *at)));
        }
    }
    m_stood = stood;
    m_lit   = stood ? glowSeconds : glm::max(0.0f, m_lit - dt);
    if (Mesh* glow = scene().tryGet<Mesh>(m_glow)) glow->visible = m_lit > 0.0f;
}

} // namespace Lab

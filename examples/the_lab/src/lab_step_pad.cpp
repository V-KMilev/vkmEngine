#include "lab_step_pad.h"

#include <glm/gtc/quaternion.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"

namespace Lab {

namespace {

// How far below the tile's middle, and above it, a character's feet count as standing on it, metres.
constexpr float BELOW = 0.3f;
constexpr float ABOVE = 0.6f;

} // namespace

void LabStepPad::onStart() {
    m_glow = findPart("Glow");
}

bool LabStepPad::isStoodOn() {
    if (!has<Transform>()) return false;
    const glm::vec3 middle  = worldPosition();
    const glm::quat inverse = glm::inverse(worldRotation());
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
    if (stood && !m_stood) playSound(note);
    m_stood = stood;
    m_lit   = stood ? glowSeconds : glm::max(0.0f, m_lit - dt);
    if (Mesh* glow = scene().tryGet<Mesh>(m_glow)) glow->visible = m_lit > 0.0f;
}

} // namespace Lab

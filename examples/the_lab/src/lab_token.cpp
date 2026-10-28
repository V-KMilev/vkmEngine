#define VKM_LOG_CATEGORY "LAB"

#include "lab_token.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "resource/resource_manager.h"

#include "lab_seat.h"
#include "lab_state.h"

namespace Lab {

namespace {

// Seconds the gem takes to rise and fall once.
constexpr double BOB_SECONDS = 2.5;

constexpr double DEGREES_A_TURN = 360.0;

uint32_t bitOf(int index) {
    return index >= 0 && index < TOKEN_BITS ? 1u << static_cast<uint32_t>(index) : 0u;
}

} // namespace

uint32_t everyToken(Scene& scene) {
    uint32_t every = 0;
    forEachBehavior<LabToken>(scene, [&](EntityId, const LabToken& token) { every |= bitOf(token.index); });
    return every;
}

void LabToken::onStart() {
    m_gem   = findPart("Gem");
    m_burst = findPart("Burst");
    if (const Transform* gem = scene().tryGet<Transform>(m_gem)) m_gemRest = gem->position;
    m_parts = HierarchyOperations::collectSubtree(scene(), entity());
}

void LabToken::onTriggerEnter(EntityId other) {
    if (!isAuthority()) return;
    LabScore* score = scene().tryGet<LabScore>(other);
    const uint32_t bit = bitOf(index);
    if (!score || !bit || (score->tokens & bit)) return;
    score->tokens |= bit;
    LOG_INFO("Player entity %u found token %d", other.slot(), index);
}

void LabToken::show(bool shown) {
    for (const EntityId part : m_parts) {
        if (part == m_burst) continue;
        if (Mesh* mesh = scene().tryGet<Mesh>(part)) mesh->visible = shown;
        if (Light* light = scene().tryGet<Light>(part)) light->enabled = shown;
        if (ParticleEmitter* motes = scene().tryGet<ParticleEmitter>(part)) motes->emitting = shown;
    }
}

void LabToken::onUpdate(float) {
    const LabScore* score = scene().tryGet<LabScore>(localCharacter(scene(), net()));
    const bool found = score && (score->tokens & bitOf(index));
    // Found before this end looked is history: hidden, without the fanfare.
    if (score && m_finds.rose(found ? 1u : 0u)) {
        if (ParticleEmitter* burst = scene().tryGet<ParticleEmitter>(m_burst)) {
            burst->burst += static_cast<uint32_t>(std::max(sparkle, 0));
        }
        playSound(chime);
    }
    if (found != m_hidden) {
        m_hidden = found;
        show(!found);
    }
    if (found) return;

    Transform* gem = scene().tryGet<Transform>(m_gem);
    if (!gem) return;
    const double now  = worldTime();
    const float  tau  = glm::two_pi<float>();
    const float  turn = tau * cycle(now * static_cast<double>(spin), DEGREES_A_TURN);
    const float  rise = bob * std::sin(tau * cycle(now, BOB_SECONDS));
    gem->rotation = glm::angleAxis(turn, Math::WORLD_UP);
    gem->position = m_gemRest + Math::WORLD_UP * rise;
}

} // namespace Lab

#include "lab_token.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_log/logger.h"

#include "base/math/axes.h"
#include "base/math/cycle.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/light/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/effect/particle_emitter.h"
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

} // namespace

uint32_t LabToken::bit() const {
    return index >= 0 && index < TOKEN_BITS ? 1u << static_cast<uint32_t>(index) : 0u;
}

void LabToken::onStart() {
    m_gem   = findPart("Gem");
    m_burst = findPart("Burst");
    if (const Transform* gem = scene().tryGet<Transform>(m_gem)) m_gemRest = gem->position;
    m_parts = HierarchyOperations::collectSubtree(scene(), entity());
}

void LabToken::onTriggerEnter(EntityId other) {
    if (!isAuthority()) return;
    LabScore* score     = scene().tryGet<LabScore>(other);
    const uint32_t mine = bit();
    if (!score || !mine || (score->tokens & mine)) return;
    score->tokens |= mine;
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
    const bool found = score && (score->tokens & bit());
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
    const float  turn = tau * Math::cycle(now * static_cast<double>(spin), DEGREES_A_TURN);
    const float  rise = bob * std::sin(tau * Math::cycle(now, BOB_SECONDS));
    gem->rotation = glm::angleAxis(turn, Math::WORLD_UP);
    gem->position = m_gemRest + Math::WORLD_UP * rise;
}

} // namespace Lab

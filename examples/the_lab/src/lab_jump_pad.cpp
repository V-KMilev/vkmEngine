#include "lab_jump_pad.h"

#include <algorithm>

#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "net/wire/protocol.h"
#include "resource/resource_manager.h"
#include "system/audio/audio_events.h"

#include "lab_state.h"

namespace Lab {

namespace {

// How deep the bed dips at a launch, as a share of its height, and how fast it springs back, per second.
constexpr float BED_DIP      = 0.6f;
constexpr float BED_RECOVERY = 5.0f;

} // namespace

void LabJumpPad::onStart() {
    reportIfSilenced(scene(), entity(), "LabJumpPad");
    if (isAuthority(net().role()) && !has<LabTally>()) add(LabTally{});
    m_bed = HierarchyOperations::findNamed(scene(), entity(), "Bed");
    if (const Transform* bed = scene().tryGet<Transform>(m_bed)) m_bedScale = bed->scale;
}

void LabJumpPad::launched() {
    if (!isAuthority(net().role())) return;
    if (LabTally* tally = tryGet<LabTally>()) ++tally->count;
}

void LabJumpPad::onUpdate(float dt) {
    const LabTally* tally = tryGet<LabTally>();
    if (tally && m_launches.rose(tally->count)) {
        m_dip = 1.0f;
        HierarchyOperations::forEachChild(scene(), entity(), [&](EntityId child) {
            if (ParticleEmitter* burst = scene().tryGet<ParticleEmitter>(child)) {
                burst->burst += static_cast<uint32_t>(std::max(puff, 0));
            }
        });
        const Transform* at = tryGet<Transform>();
        const AudioClipHandle sound = resources().find(boing);
        if (at && sound) {
            events().emit(PlaySoundEvent::at(sound, resolvedWorldPosition(scene(), entity(), *at)));
        }
    }
    if (m_dip <= 0.0f) return;
    m_dip = std::max(0.0f, m_dip - BED_RECOVERY * dt);
    if (Transform* bed = scene().tryGet<Transform>(m_bed)) {
        bed->scale   = m_bedScale;
        bed->scale.y = m_bedScale.y * (1.0f - BED_DIP * m_dip);
    }
}

} // namespace Lab

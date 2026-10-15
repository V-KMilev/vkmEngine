#include "lab_switch.h"

#include <optional>

#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "net/wire/protocol.h"
#include "resource/resource_manager.h"
#include "system/audio/audio_events.h"

#include "lab_state.h"

namespace Lab {

void LabSwitch::onStart() {
    reportIfSilenced(scene(), entity(), "LabSwitch");
    if (isAuthority(net().role()) && !has<LabTally>()) add(LabTally{});
}

void LabSwitch::press() {
    if (!isAuthority(net().role())) return;
    if (LabTally* tally = tryGet<LabTally>()) ++tally->count;
}

std::optional<uint32_t> LabSwitch::presses() const {
    const LabTally* tally = tryGet<LabTally>();
    return tally ? std::optional<uint32_t>(tally->count) : std::nullopt;
}

void LabSwitch::onUpdate(float) {
    const std::optional<uint32_t> now = presses();
    if (!now || !m_heard.rose(*now)) return;
    const Transform* at = tryGet<Transform>();
    const AudioClipHandle sound = resources().find(click);
    if (at && sound) events().emit(PlaySoundEvent::at(sound, resolvedWorldPosition(scene(), entity(), *at)));
}

} // namespace Lab

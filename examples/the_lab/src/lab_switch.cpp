#include "lab_switch.h"

#include <optional>

#include "ecs/scene.h"

#include "lab_state.h"

namespace Lab {

void LabSwitch::onStart() {
    reportIfSilenced(scene(), entity(), "LabSwitch");
    if (isAuthority()) getOrAdd<LabTally>();
}

void LabSwitch::press() {
    if (!isAuthority()) return;
    if (LabTally* tally = tryGet<LabTally>()) ++tally->count;
}

std::optional<uint32_t> LabSwitch::presses() const {
    const LabTally* tally = tryGet<LabTally>();
    return tally ? std::optional<uint32_t>(tally->count) : std::nullopt;
}

void LabSwitch::onUpdate(float) {
    const std::optional<uint32_t> now = presses();
    if (!now || !m_heard.rose(*now)) return;
    playSound(click);
}

} // namespace Lab

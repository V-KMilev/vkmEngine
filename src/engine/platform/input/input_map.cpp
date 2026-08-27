#define VKM_LOG_CATEGORY "INPUT"

#include "platform/input/input_map.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>

#include "logger.h"

#include "platform/window/input_handle.h"

namespace Vkm::Engine {

namespace {

// Returned for an undefined action so bindings() can hand back a reference
// without the caller checking for null first.
const std::vector<InputBinding> NO_BINDINGS;

// Active enough to count as "down" for held/pressed/released. Digital sources
// give exactly 0 or 1; the threshold is what lets an analogue source - a trigger,
// a stick past centre - resolve to a button the same way.
constexpr float ACTIVE_THRESHOLD = 0.5f;

} // namespace

void InputMap::assignSlot(Action& entry, const std::string& action) {
    if (entry.slot >= 0) return;
    if (m_nextSlot >= MAX_INPUT_ACTIONS) {
        LOG_WARNING("Input action '%s' is past the %u-action command limit; it is "
                    "readable this frame but absent from every command",
                    action.c_str(), MAX_INPUT_ACTIONS);
        return;
    }
    entry.slot = static_cast<int>(m_nextSlot++);
}

void InputMap::define(const std::string& action, std::vector<InputBinding> bindings) {
    Action& entry = m_actions[action];
    entry.bindings = std::move(bindings);
    assignSlot(entry, action);
}

void InputMap::addBinding(const std::string& action, InputBinding binding) {
    Action& entry = m_actions[action];
    entry.bindings.push_back(binding);
    assignSlot(entry, action);
}

int InputMap::indexOf(const std::string& action) const {
    const Action* entry = find(action);
    return entry ? entry->slot : -1;
}

void InputMap::clearBindings(const std::string& action) {
    auto it = m_actions.find(action);
    if (it != m_actions.end()) it->second.bindings.clear();
}

const std::vector<InputBinding>& InputMap::bindings(const std::string& action) const {
    const Action* entry = find(action);
    return entry ? entry->bindings : NO_BINDINGS;
}

std::vector<std::string> InputMap::actions() const {
    std::vector<std::string> names;
    names.reserve(m_actions.size());
    for (const auto& [name, _] : m_actions) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

void InputMap::update(const InputHandle& input) {
    const KeyboardInputHandle& keyboard = input.getKeyboard();
    const MouseInputHandle&    mouse    = input.getMouse();

    for (auto& [_, action] : m_actions) {
        action.lastValue = action.value;

        float value = 0.0f;
        for (const InputBinding& binding : action.bindings) {
            bool down = false;
            switch (binding.source) {
                case InputSource::Key:         down = keyboard.isKeyPressed(binding.code);  break;
                case InputSource::MouseButton: down = mouse.isButtonPressed(binding.code);  break;
            }
            if (down) value += binding.scale;
        }

        // Opposing bindings cancel, so holding both directions reads as zero.
        action.value = glm::clamp(value, -1.0f, 1.0f);

        // Edges are latched here rather than read at tick time: one seen on a
        // frame that no tick follows would otherwise be lost entirely.
        if (action.slot < 0) continue;
        const uint32_t bit = uint32_t(1) << action.slot;
        const bool wasActive = std::abs(action.lastValue) >= ACTIVE_THRESHOLD;
        const bool isActive  = std::abs(action.value)     >= ACTIVE_THRESHOLD;
        if (isActive && !wasActive) m_pendingPressed  |= bit;
        if (!isActive && wasActive) m_pendingReleased |= bit;
    }
}

void InputMap::beginTick(uint32_t tick) {
    m_command.sequence = m_nextSequence++;
    m_command.tick     = tick;
    m_command.view     = m_view;
    m_command.axis.fill(0.0f);
    for (const auto& [_, action] : m_actions) {
        if (action.slot >= 0) m_command.axis[static_cast<size_t>(action.slot)] = action.value;
    }

    // Drained, not copied: the second tick of a slow frame must not see the
    // press the first one already consumed.
    m_command.pressed  = m_pendingPressed;
    m_command.released = m_pendingReleased;
    m_pendingPressed  = 0;
    m_pendingReleased = 0;
}

bool InputMap::held(const std::string& action) const {
    const Action* entry = find(action);
    return entry && std::abs(entry->value) >= ACTIVE_THRESHOLD;
}

bool InputMap::pressed(const std::string& action) const {
    const Action* entry = find(action);
    if (!entry) return false;
    return std::abs(entry->value)     >= ACTIVE_THRESHOLD
        && std::abs(entry->lastValue) <  ACTIVE_THRESHOLD;
}

bool InputMap::released(const std::string& action) const {
    const Action* entry = find(action);
    if (!entry) return false;
    return std::abs(entry->value)     <  ACTIVE_THRESHOLD
        && std::abs(entry->lastValue) >= ACTIVE_THRESHOLD;
}

float InputMap::axis(const std::string& action) const {
    const Action* entry = find(action);
    return entry ? entry->value : 0.0f;
}

const InputMap::Action* InputMap::find(const std::string& action) const {
    auto it = m_actions.find(action);
    return it == m_actions.end() ? nullptr : &it->second;
}

} // namespace Vkm::Engine

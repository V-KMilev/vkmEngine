#include "platform/input/input_map.h"

#include <algorithm>

#include <glm/common.hpp>

#include "platform/window/input_handle.h"

namespace Vkm::Engine {

namespace {

// Returned for an undefined action so bindings() can hand back a reference
// without the caller checking for null first.
const std::vector<InputBinding> NO_BINDINGS;

// Active enough to count as "down" for held/pressed/released. Digital sources
// give exactly 0 or 1; the threshold is here so an analogue source (a trigger,
// a stick past centre) resolves to a button the same way without every caller
// picking its own cut-off.
constexpr float ACTIVE_THRESHOLD = 0.5f;

} // namespace

void InputMap::define(const std::string& action, std::vector<InputBinding> bindings) {
    Action& entry = m_actions[action];
    entry.bindings = std::move(bindings);
}

void InputMap::addBinding(const std::string& action, InputBinding binding) {
    m_actions[action].bindings.push_back(binding);
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
    }
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

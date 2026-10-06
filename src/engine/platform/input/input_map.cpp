#define VKM_LOG_CATEGORY "INPUT"

#include "platform/input/input_map.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/common.hpp>

#include "logger.h"

#include "core/fnv1a.h"
#include "core/host_chrome.h"
#include "platform/input/input_handle.h"
#include "platform/window/glfw_include.h"

namespace Vkm::Engine {

namespace {

// Returned for an undefined action, so bindings() can return a reference.
const std::vector<InputBinding> NO_BINDINGS;

// Digital sources give exactly 0 or 1; the threshold lets an analogue one resolve to a button.
constexpr float ACTIVE_THRESHOLD = 0.5f;

} // namespace

InputMap::InputMap() {
    defineEngineActions();
}

void InputMap::defineEngineActions() {
    define(InputActions::UI_CLICK, { InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_LEFT, 1.0f} });
}

void InputMap::assignSlot(Action& entry, std::string_view action) {
    if (entry.slot >= 0) return;
    if (m_nextSlot >= MAX_INPUT_ACTIONS) {
        LOG_WARNING(
            "Input action '%.*s' is past the %u-action command limit; it is "
            "readable this frame but absent from every command",
            static_cast<int>(action.size()),
            action.data(),
            MAX_INPUT_ACTIONS
        );
        return;
    }
    entry.slot = static_cast<int>(m_nextSlot++);
}

void InputMap::define(std::string_view action, std::vector<InputBinding> bindings) {
    Action& entry = mutableAction(action);
    entry.bindings = std::move(bindings);
    assignSlot(entry, action);
}

void InputMap::addBinding(std::string_view action, InputBinding binding) {
    Action& entry = mutableAction(action);
    entry.bindings.push_back(binding);
    assignSlot(entry, action);
}

InputMap::Action& InputMap::mutableAction(std::string_view action) {
    auto it = m_actions.find(action);
    if (it == m_actions.end()) it = m_actions.emplace(std::string(action), Action{}).first;
    return it->second;
}

uint64_t InputMap::actionFingerprint() const {
    if (m_nextSlot == 0) return 0;

    std::vector<std::string_view> bySlot(m_nextSlot);
    for (const auto& [name, action] : m_actions) {
        if (action.slot >= 0) bySlot[static_cast<size_t>(action.slot)] = name;
    }

    // A separator after each name, so "ab","c" and "a","bc" differ.
    uint64_t hash = FNV1A_OFFSET_BASIS;
    const char separator = '\0';
    for (std::string_view name : bySlot) {
        hash = fnv1a64(name, hash);
        hash = fnv1a64Bytes(&separator, 1, hash);
    }
    return hash;
}

int InputMap::indexOf(std::string_view action) const {
    const Action* entry = find(action);
    return entry ? entry->slot : -1;
}

void InputMap::clearBindings(std::string_view action) {
    auto it = m_actions.find(action);
    if (it != m_actions.end()) it->second.bindings.clear();
}

const std::vector<InputBinding>& InputMap::bindings(std::string_view action) const {
    const Action* entry = find(action);
    return entry ? entry->bindings : NO_BINDINGS;
}

std::vector<std::string> InputMap::actions() const {
    std::vector<std::string> names;
    names.reserve(m_actions.size());
    for (const auto& [name, _] : m_actions) names.push_back(name);
    return names;
}

bool InputMap::heldElsewhere(const InputBinding& binding, bool down, bool hostHas, bool uiHas, bool forUI) {
    const auto sameButton = [&](const HeldPress& held) {
        return held.source == binding.source && held.code == binding.code;
    };
    if (!down) return false;

    auto found = std::find_if(m_heldPresses.begin(), m_heldPresses.end(), sameButton);
    if (found == m_heldPresses.end()) {
        const Holder holder = hostHas ? Holder::Host : (uiHas ? Holder::UI : Holder::Gameplay);
        found = m_heldPresses.insert(m_heldPresses.end(), HeldPress{binding.source, binding.code, holder});
    } else if (hostHas) {
        // The host taking the device ends the gesture, so the press does not come back after.
        found->holder = Holder::Host;
    }

    switch (found->holder) {
        case Holder::Host: return true;
        case Holder::UI:   return !forUI;
        case Holder::Gameplay: return false;
    }
    return false;
}

void InputMap::update(InputHandle& input, const HostChrome& chrome) {
    const bool pointerOurs  = !chrome.capturesPointer();
    const bool keyboardOurs = !chrome.capturesKeyboard();

    // A record ends with the press, bound or not: one left by a reset or rebind would hand
    // the next press to its old holder.
    const auto letGo = [&](const HeldPress& held) {
        switch (held.source) {
            case InputSource::Key:
                return !input.isKeyPressed(held.code) && !input.wasKeyStruck(held.code);
            case InputSource::MouseButton:
                return !input.isButtonPressed(held.code) && !input.wasButtonStruck(held.code);
        }
        return true;
    };
    const auto ended = std::remove_if(m_heldPresses.begin(), m_heldPresses.end(), letGo);
    m_heldPresses.erase(ended, m_heldPresses.end());

    m_pointer = {static_cast<float>(input.getX()), static_cast<float>(input.getY())};
    m_pointerDelta = pointerOurs
        ? glm::vec2(static_cast<float>(input.getDeltaX()), static_cast<float>(input.getDeltaY()))
        : glm::vec2(0.0f);
    m_uiWheel = pointerOurs ? static_cast<float>(input.getScrollY()) : 0.0f;
    m_wheel   = m_pointerOverUI ? 0.0f : m_uiWheel;

    m_text.clear();
    m_typed.reset();
    if (keyboardOurs) {
        m_text = input.text();
        for (int key = 0; key <= MAX_KEY; ++key) {
            m_typed[static_cast<size_t>(key)] = input.wasKeyTyped(key);
        }
    }

    // The UI takes presses of its click's buttons, only while over a blocking element.
    const Action* uiClick = find(InputActions::UI_CLICK);
    const auto clicksUI = [&](const InputBinding& binding) {
        if (!m_pointerOverUI || !uiClick || binding.source != InputSource::MouseButton) return false;
        const auto sameButton = [&](const InputBinding& click) {
            return click.source == binding.source && click.code == binding.code;
        };
        return std::any_of(uiClick->bindings.begin(), uiClick->bindings.end(), sameButton);
    };

    for (auto& [_, action] : m_actions) {
        const bool forUI = &action == uiClick;
        float value = 0.0f;
        for (const InputBinding& binding : action.bindings) {
            bool down = false;
            bool ours = false;
            switch (binding.source) {
                case InputSource::Key:
                    down = input.isKeyPressed(binding.code) || input.wasKeyStruck(binding.code);
                    ours = keyboardOurs;
                    break;
                case InputSource::MouseButton:
                    down = input.isButtonPressed(binding.code) || input.wasButtonStruck(binding.code);
                    ours = pointerOurs;
                    break;
            }
            if (heldElsewhere(binding, down, !ours, clicksUI(binding), forUI)) continue;
            if (down) value += binding.scale;
        }

        action.value     = glm::clamp(value, -1.0f, 1.0f);
        action.wasActive = action.active;
        action.active    = std::abs(action.value) >= ACTIVE_THRESHOLD;

        // Latched here, not at tick time: an edge on a frame no tick follows would be lost.
        if (action.slot < 0) continue;
        const uint32_t bit = uint32_t(1) << action.slot;
        if (action.active && !action.wasActive) m_pendingPressed  |= bit;
        if (!action.active && action.wasActive) m_pendingReleased |= bit;
    }

    // After every action has read them: two actions can share a key.
    input.clearStrikes();
}

void InputMap::discardPendingEdges() {
    m_pendingPressed  = 0;
    m_pendingReleased = 0;
}

void InputMap::reset() {
    m_actions.clear();
    m_nextSlot = 0;
    discardPendingEdges();

    m_pointerDelta = glm::vec2(0.0f);
    m_wheel        = 0.0f;
    m_uiWheel      = 0.0f;
    m_text.clear();
    m_typed.reset();
    m_view         = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // Zeroed, as before the first tick, so nothing reads the last session's.
    m_command = InputCommand{};
    defineEngineActions();
}

void InputMap::beginTick(uint32_t tick) {
    m_command.sequence = m_nextSequence++;
    m_command.tick     = tick;
    m_command.view     = m_view;
    m_command.axis.fill(0.0f);
    for (const auto& [_, action] : m_actions) {
        if (action.slot >= 0) m_command.axis[static_cast<size_t>(action.slot)] = action.value;
    }

    // Drained, not copied: a slow frame's second tick must not see the first's press.
    m_command.pressed  = m_pendingPressed;
    m_command.released = m_pendingReleased;
    m_pendingPressed  = 0;
    m_pendingReleased = 0;
}

bool InputMap::held(std::string_view action) const {
    const Action* entry = find(action);
    return entry && entry->active;
}

bool InputMap::pressed(std::string_view action) const {
    const Action* entry = find(action);
    return entry && entry->active && !entry->wasActive;
}

bool InputMap::released(std::string_view action) const {
    const Action* entry = find(action);
    return entry && !entry->active && entry->wasActive;
}

float InputMap::axis(std::string_view action) const {
    const Action* entry = find(action);
    return entry ? entry->value : 0.0f;
}

bool InputMap::held(const InputCommand& command, std::string_view action) const {
    return std::abs(axis(command, action)) >= ACTIVE_THRESHOLD;
}

bool InputMap::pressed(const InputCommand& command, std::string_view action) const {
    const int slot = indexOf(action);
    return slot >= 0 && (command.pressed & (uint32_t(1) << slot)) != 0;
}

bool InputMap::released(const InputCommand& command, std::string_view action) const {
    const int slot = indexOf(action);
    return slot >= 0 && (command.released & (uint32_t(1) << slot)) != 0;
}

float InputMap::axis(const InputCommand& command, std::string_view action) const {
    const int slot = indexOf(action);
    return slot >= 0 ? command.axis[static_cast<size_t>(slot)] : 0.0f;
}

const InputMap::Action* InputMap::find(std::string_view action) const {
    auto it = m_actions.find(action);
    return it == m_actions.end() ? nullptr : &it->second;
}

} // namespace Vkm::Engine

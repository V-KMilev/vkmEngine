#define VKM_LOG_CATEGORY "INPUT"

#include "core/input_map.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <variant>

#include <glm/common.hpp>
#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "base/hash/fnv1a.h"
#include "core/host_chrome.h"
#include "base/reflect/reflect.h"
#include "io/json/own_file.h"
#include "io/project/actions_json.h"
#include "vkm_platform/input/input_handle.h"

namespace Vkm::Engine {

namespace {

// Returned for an undefined action, so bindings() can return a reference.
const std::vector<InputBinding> NO_BINDINGS;

// Digital sources give exactly 0 or 1; the threshold lets an analog one resolve to a button.
constexpr float ACTIVE_THRESHOLD = 0.5f;

/**
 * @brief What the device says of one key or button since the last sample.
 */
struct Reading {
    bool struck = false;  ///< Went down, even if up again.
    bool down   = false;  ///< Held now, or struck.
    bool again  = false;  ///< Let go and struck again.
};

Reading read(const Platform::InputHandle& input, Platform::Key key) {
    const bool struck = input.wasKeyStruck(key);
    return {struck, input.isKeyPressed(key) || struck, input.wasKeyRestruck(key)};
}

Reading read(const Platform::InputHandle& input, Platform::MouseButton button) {
    const bool struck = input.wasButtonStruck(button);
    return {struck, input.isButtonPressed(button) || struck, input.wasButtonRestruck(button)};
}

Reading read(const Platform::InputHandle& input, const InputControl& control) {
    return std::visit([&](auto pressed) { return read(input, pressed); }, control);
}

} // namespace

InputMap::InputMap() {
    rebuildActions();
}

void InputMap::rebuildActions() {
    m_actions.clear();
    m_nextSlot    = 0;
    m_fingerprint = FNV1A_OFFSET_BASIS;
    define(InputActions::UI_CLICK, { InputBinding::mouse(Platform::MouseButton::Left) });
    for (const InputAction& action : m_gameActions) define(action.name, action.bindings);
}

void InputMap::setActions(std::vector<InputAction> actions) {
    std::sort(actions.begin(), actions.end(), [](const InputAction& a, const InputAction& b) {
        return a.name < b.name;
    });
    m_gameActions = std::move(actions);
    rebuildActions();
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
    // Slots are only appended, so the fingerprint folds each name in as it is slotted. A separator
    // after each, so "ab","c" and "a","bc" differ.
    const char separator = '\0';
    m_fingerprint = fnv1a64(action, m_fingerprint);
    m_fingerprint = fnv1a64Bytes(&separator, 1, m_fingerprint);
}

void InputMap::define(std::string_view action, std::vector<InputBinding> bindings) {
    Action& entry    = mutableAction(action);
    const auto saved = m_saved.find(action);

    entry.defaults = std::move(bindings);
    entry.bindings = saved != m_saved.end() ? saved->second : entry.defaults;
    assignSlot(entry, action);
}

void InputMap::rebind(std::string_view action, std::vector<InputBinding> bindings) {
    const auto entry = m_actions.find(action);
    if (entry == m_actions.end()) {
        LOG_WARNING(
            "No action '%.*s' to rebind: the game's are project.json's",
            static_cast<int>(action.size()),
            action.data()
        );
        return;
    }
    m_saved[std::string(action)] = bindings;
    entry->second.bindings = std::move(bindings);
    writeBindings();
}

void InputMap::resetBindings() {
    for (auto& [name, entry] : m_actions) entry.bindings = entry.defaults;
    m_saved.clear();
    writeBindings();
}

void InputMap::keepBindingsIn(std::filesystem::path file) {
    m_bindingsFile = std::move(file);
    loadBindings(readOwnFile(m_bindingsFile));
}

void InputMap::writeBindings() const {
    if (!m_bindingsFile.empty()) writeOwnFile(m_bindingsFile, saveBindings());
}

std::string InputMap::keyName(std::string_view action) const {
    const Action* entry = queried(action);
    return entry && !entry->bindings.empty() ? bindingName(entry->bindings.front()) : std::string{};
}

namespace {

bool sameBindings(const std::vector<InputBinding>& a, const std::vector<InputBinding>& b) {
    const auto same = [](const InputBinding& x, const InputBinding& y) {
        return x.control == y.control && x.scale == y.scale;
    };
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), same);
}

} // namespace

nlohmann::json InputMap::saveBindings() const {
    std::vector<InputAction> rebound;
    for (const auto& [name, bindings] : m_saved) {
        // Back at its defaults, it follows them; never defined this run, kept for one that does.
        const auto entry = m_actions.find(name);
        if (entry != m_actions.end() && sameBindings(bindings, entry->second.defaults)) continue;
        rebound.push_back({name, bindings});
    }
    return actionsToJson(rebound);
}

void InputMap::loadBindings(const nlohmann::json& saved) {
    for (InputAction& action : actionsFromJson(saved)) {
        const auto entry = m_actions.find(action.name);
        if (entry != m_actions.end()) entry->second.bindings = action.bindings;
        m_saved[action.name] = std::move(action.bindings);
    }
}

InputMap::Action& InputMap::mutableAction(std::string_view action) {
    auto it = m_actions.find(action);
    if (it == m_actions.end()) it = m_actions.emplace(std::string(action), Action{}).first;
    return it->second;
}

int InputMap::indexOf(std::string_view action) const {
    const Action* entry = find(action);
    return entry ? entry->slot : -1;
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
    const auto sameControl = [&](const HeldPress& held) { return held.control == binding.control; };
    if (!down) return false;

    auto found = std::find_if(m_heldPresses.begin(), m_heldPresses.end(), sameControl);
    if (found == m_heldPresses.end()) {
        const Holder holder = hostHas ? Holder::Host : (uiHas ? Holder::UI : Holder::Gameplay);
        found = m_heldPresses.insert(m_heldPresses.end(), HeldPress{binding.control, holder});
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

void InputMap::update(Platform::InputHandle& input, const HostChrome& chrome) {
    const bool pointerOurs  = !chrome.capturePointer;
    const bool keyboardOurs = !chrome.captureKeyboard;

    // A record ends with the press, bound or not: one left by a reset or rebind would hand
    // the next press to its old holder.
    const auto letGo = [&](const HeldPress& held) { return !read(input, held.control).down; };
    const auto ended = std::remove_if(m_heldPresses.begin(), m_heldPresses.end(), letGo);
    m_heldPresses.erase(ended, m_heldPresses.end());

    m_pointer = {static_cast<float>(input.getX()), static_cast<float>(input.getY())};
    // Over the UI as last laid out, but where the pointer is now.
    if (m_blockersKnown) m_pointerOverUI = pointerInBlockers();
    m_pointerDelta = pointerOurs
        ? glm::vec2(static_cast<float>(input.getDeltaX()), static_cast<float>(input.getDeltaY()))
        : glm::vec2(0.0f);
    m_uiWheel = pointerOurs ? static_cast<float>(input.getScrollY()) : 0.0f;
    m_wheel   = m_pointerOverUI ? 0.0f : m_uiWheel;

    m_text.clear();
    m_typed.reset();
    m_struck.reset();
    if (keyboardOurs) {
        m_text = input.text();
        for (size_t at = 0; at < KEYS; ++at) {
            const auto key = static_cast<Platform::Key>(at);
            m_typed[at] = input.wasKeyTyped(key);
            if (!m_struck && input.wasKeyStruck(key)) m_struck = InputBinding::key(key);
        }
    }
    for (size_t at = 0; at < BUTTONS && pointerOurs && !m_struck; ++at) {
        const auto button = static_cast<Platform::MouseButton>(at);
        if (input.wasButtonStruck(button)) m_struck = InputBinding::mouse(button);
    }

    // The UI takes presses of its click's buttons, only while over a blocking element.
    const Action* uiClick = find(InputActions::UI_CLICK);
    const auto clicksUI = [&](const InputBinding& binding) {
        if (!m_pointerOverUI || !uiClick || !std::holds_alternative<Platform::MouseButton>(binding.control)) {
            return false;
        }
        const auto sameButton = [&](const InputBinding& click) { return click.control == binding.control; };
        return std::any_of(uiClick->bindings.begin(), uiClick->bindings.end(), sameButton);
    };

    for (auto& [_, action] : m_actions) {
        const bool forUI = &action == uiClick;
        float value = 0.0f;
        bool restruck = false;
        bool stayed   = false;
        for (const InputBinding& binding : action.bindings) {
            const bool    isKey = std::holds_alternative<Platform::Key>(binding.control);
            const bool    ours  = isKey ? keyboardOurs : pointerOurs;
            const Reading now   = read(input, binding.control);
            if (heldElsewhere(binding, now.down, !ours, clicksUI(binding), forUI)) continue;
            if (now.down) value += binding.scale;
            restruck = restruck || now.again;
            stayed   = stayed || (now.down && !now.struck);
        }

        action.value     = glm::clamp(value, -1.0f, 1.0f);
        action.wasActive = action.active;
        action.active    = std::abs(action.value) >= ACTIVE_THRESHOLD;
        // Down at both samples, it was let go and struck between: both edges, or a quick second
        // click in a long frame is lost. Not while another binding held it down throughout.
        action.restruck = action.wasActive && restruck && !stayed;

        // Latched here, not at tick time: an edge on a frame no tick follows would be lost.
        if (action.slot < 0) continue;
        const uint32_t bit = uint32_t(1) << action.slot;
        if (action.active && (!action.wasActive || action.restruck)) m_pendingPressed  |= bit;
        if (action.wasActive && (!action.active || action.restruck)) m_pendingReleased |= bit;
    }

    // After every action has read them: two actions can share a key.
    input.clearStrikes();
}

void InputMap::discardPendingEdges() {
    m_pendingPressed  = 0;
    m_pendingReleased = 0;
}

void InputMap::reset() {
    m_blockers.clear();
    m_blockersKnown = false;
    discardPendingEdges();

    m_pointerDelta = glm::vec2(0.0f);
    m_wheel        = 0.0f;
    m_uiWheel      = 0.0f;
    m_text.clear();
    m_typed.reset();
    m_view = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_struck.reset();
    m_payload.fill(0);
    m_payloadSize = 0;

    // Zeroed, as before the first tick, so nothing reads the last session's.
    m_command = InputCommand{};
    rebuildActions();
}

void InputMap::beginTick(uint32_t tick) {
    m_command.sequence    = m_nextSequence++;
    m_command.tick        = tick;
    m_command.view        = m_view;
    m_command.payload     = m_payload;
    m_command.payloadSize = m_payloadSize;
    m_command.axis.fill(0.0f);
    for (const auto& [_, action] : m_actions) {
        if (action.slot >= 0) m_command.axis[static_cast<size_t>(action.slot)] = action.value;
    }

    // Drained, not copied: a slow frame's second tick must not see the first's press.
    m_command.pressed  = m_pendingPressed;
    m_command.released = m_pendingReleased;
    m_pendingPressed   = 0;
    m_pendingReleased  = 0;
}

bool InputMap::setPayload(const uint8_t* bytes, size_t size) {
    if (size > MAX_COMMAND_PAYLOAD) return false;
    m_payloadSize = static_cast<uint8_t>(bytes ? size : 0);
    m_payload.fill(0);
    if (bytes) std::copy(bytes, bytes + m_payloadSize, m_payload.begin());
    return true;
}

bool InputMap::held(std::string_view action) const {
    const Action* entry = queried(action);
    return entry && entry->active;
}

bool InputMap::pressed(std::string_view action) const {
    const Action* entry = queried(action);
    return entry && entry->active && (!entry->wasActive || entry->restruck);
}

bool InputMap::released(std::string_view action) const {
    const Action* entry = queried(action);
    return entry && entry->wasActive && (!entry->active || entry->restruck);
}

float InputMap::axis(std::string_view action) const {
    const Action* entry = queried(action);
    return entry ? entry->value : 0.0f;
}

bool InputMap::held(const InputCommand& command, std::string_view action) const {
    return std::abs(axis(command, action)) >= ACTIVE_THRESHOLD;
}

bool InputMap::pressed(const InputCommand& command, std::string_view action) const {
    const Action* entry = queried(action);
    const int slot = entry ? entry->slot : -1;
    return slot >= 0 && (command.pressed & (uint32_t(1) << slot)) != 0;
}

bool InputMap::released(const InputCommand& command, std::string_view action) const {
    const Action* entry = queried(action);
    const int slot = entry ? entry->slot : -1;
    return slot >= 0 && (command.released & (uint32_t(1) << slot)) != 0;
}

float InputMap::axis(const InputCommand& command, std::string_view action) const {
    const Action* entry = queried(action);
    const int slot = entry ? entry->slot : -1;
    return slot >= 0 ? command.axis[static_cast<size_t>(slot)] : 0.0f;
}

const InputMap::Action* InputMap::find(std::string_view action) const {
    auto it = m_actions.find(action);
    return it == m_actions.end() ? nullptr : &it->second;
}

const InputMap::Action* InputMap::queried(std::string_view action) const {
    const Action* entry = find(action);
    if (!entry && m_unknownRead.find(action) == m_unknownRead.end()) {
        m_unknownRead.emplace(action);
        LOG_WARNING(
            "Input action '%.*s' is read but never listed, so it is never pressed; project.json's "
            "actions list the game's",
            static_cast<int>(action.size()),
            action.data()
        );
    }
    return entry;
}

bool InputMap::pointerInBlockers() const {
    const auto under = [&](const glm::vec4& area) {
        return m_pointer.x >= area.x && m_pointer.y >= area.y
            && m_pointer.x < area.x + area.z && m_pointer.y < area.y + area.w;
    };
    return std::any_of(m_blockers.begin(), m_blockers.end(), under);
}

} // namespace Vkm::Engine

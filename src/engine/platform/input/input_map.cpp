#define VKM_LOG_CATEGORY "INPUT"

#include "platform/input/input_map.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <glm/common.hpp>
#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/fnv1a.h"
#include "core/host_chrome.h"
#include "core/reflect.h"
#include "platform/input/input_handle.h"
#include "platform/window/glfw_include.h"

namespace Vkm::Engine {

namespace {

// Returned for an undefined action, so bindings() can return a reference.
const std::vector<InputBinding> NO_BINDINGS;

// Digital sources give exactly 0 or 1; the threshold lets an analogue one resolve to a button.
constexpr float ACTIVE_THRESHOLD = 0.5f;

} // namespace

std::string bindingName(const InputBinding& binding) {
    const int code = binding.code;
    if (binding.source == InputSource::MouseButton) {
        switch (code) {
            case GLFW_MOUSE_BUTTON_LEFT:   return "Left mouse";
            case GLFW_MOUSE_BUTTON_RIGHT:  return "Right mouse";
            case GLFW_MOUSE_BUTTON_MIDDLE: return "Middle mouse";
            default:                       return "Mouse " + std::to_string(code + 1);
        }
    }
    if (code >= GLFW_KEY_A && code <= GLFW_KEY_Z) {
        return std::string(1, static_cast<char>('A' + code - GLFW_KEY_A));
    }
    if (code >= GLFW_KEY_0 && code <= GLFW_KEY_9) {
        return std::string(1, static_cast<char>('0' + code - GLFW_KEY_0));
    }
    if (code >= GLFW_KEY_F1 && code <= GLFW_KEY_F25) return "F" + std::to_string(code - GLFW_KEY_F1 + 1);
    if (code >= GLFW_KEY_KP_0 && code <= GLFW_KEY_KP_9) {
        return "Keypad " + std::to_string(code - GLFW_KEY_KP_0);
    }
    switch (code) {
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_APOSTROPHE:    return "'";
        case GLFW_KEY_COMMA:         return ",";
        case GLFW_KEY_MINUS:         return "-";
        case GLFW_KEY_PERIOD:        return ".";
        case GLFW_KEY_SLASH:         return "/";
        case GLFW_KEY_SEMICOLON:     return ";";
        case GLFW_KEY_EQUAL:         return "=";
        case GLFW_KEY_LEFT_BRACKET:  return "[";
        case GLFW_KEY_BACKSLASH:     return "\\";
        case GLFW_KEY_RIGHT_BRACKET: return "]";
        case GLFW_KEY_GRAVE_ACCENT:  return "`";
        case GLFW_KEY_ESCAPE:        return "Escape";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_INSERT:        return "Insert";
        case GLFW_KEY_DELETE:        return "Delete";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_DOWN:          return "Down";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_PAGE_UP:       return "Page Up";
        case GLFW_KEY_PAGE_DOWN:     return "Page Down";
        case GLFW_KEY_HOME:          return "Home";
        case GLFW_KEY_END:           return "End";
        case GLFW_KEY_CAPS_LOCK:     return "Caps Lock";
        case GLFW_KEY_KP_ENTER:      return "Keypad Enter";
        case GLFW_KEY_LEFT_SHIFT:    return "Left Shift";
        case GLFW_KEY_LEFT_CONTROL:  return "Left Ctrl";
        case GLFW_KEY_LEFT_ALT:      return "Left Alt";
        case GLFW_KEY_RIGHT_SHIFT:   return "Right Shift";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_RIGHT_ALT:     return "Right Alt";
        default:                     return "Key " + std::to_string(code);
    }
}

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
    Action& entry  = mutableAction(action);
    entry.defaults = std::move(bindings);
    const auto saved = m_saved.find(action);
    entry.bindings   = saved != m_saved.end() ? saved->second : entry.defaults;
    assignSlot(entry, action);
}

void InputMap::rebind(std::string_view action, std::vector<InputBinding> bindings) {
    Action& entry = mutableAction(action);
    m_saved[std::string(action)] = bindings;
    entry.bindings = std::move(bindings);
    assignSlot(entry, action);
}

void InputMap::resetBindings() {
    for (auto& [name, entry] : m_actions) entry.bindings = entry.defaults;
    m_saved.clear();
}

namespace {

bool sameBindings(const std::vector<InputBinding>& a, const std::vector<InputBinding>& b) {
    const auto same = [](const InputBinding& x, const InputBinding& y) {
        return x.source == y.source && x.code == y.code && x.scale == y.scale;
    };
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), same);
}

nlohmann::json bindingToJson(const InputBinding& binding) {
    return {{"source", Reflect::enumName(binding.source)}, {"code", binding.code}, {"scale", binding.scale}};
}

// A saved binding as it was written, or nothing: a source this build names, a code that source
// has, and a finite number for a scale, which may be left out.
std::optional<InputBinding> bindingFromJson(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    const auto source = j.find("source");
    const auto code   = j.find("code");
    const auto scale  = j.find("scale");
    InputBinding binding;
    if (source == j.end() || !source->is_string()) return std::nullopt;
    if (!Reflect::enumFromNameChecked(source->get<std::string>(), binding.source)) return std::nullopt;
    if (code == j.end() || !code->is_number_integer()) return std::nullopt;
    const int64_t last  = binding.source == InputSource::MouseButton ? MAX_MOUSE_BUTTON : MAX_KEY;
    const int64_t value = code->get<int64_t>();
    if (value < 0 || value > last) return std::nullopt;
    binding.code = static_cast<int>(value);
    if (scale != j.end()) {
        if (!scale->is_number() || !std::isfinite(scale->get<float>())) return std::nullopt;
        binding.scale = scale->get<float>();
    }
    return binding;
}

} // namespace

nlohmann::json InputMap::saveBindings() const {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, bindings] : m_saved) {
        // Back at its defaults, it follows them; never defined this run, kept for one that does.
        const auto entry = m_actions.find(name);
        if (entry != m_actions.end() && sameBindings(bindings, entry->second.defaults)) continue;
        nlohmann::json list = nlohmann::json::array();
        for (const InputBinding& binding : bindings) list.push_back(bindingToJson(binding));
        out[name] = std::move(list);
    }
    return out;
}

void InputMap::loadBindings(const nlohmann::json& saved) {
    if (!saved.is_object()) return;
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        // Wrong anywhere, the action keeps its own: half a list could leave it unpressable.
        std::vector<InputBinding> bindings;
        bool                      readable = it.value().is_array();
        for (size_t i = 0; readable && i < it.value().size(); ++i) {
            const std::optional<InputBinding> binding = bindingFromJson(it.value()[i]);
            readable = binding.has_value();
            if (readable) bindings.push_back(*binding);
        }
        if (!readable) {
            LOG_WARNING("Saved bindings for '%s' cannot be read; keeping its own", it.key().c_str());
            continue;
        }
        const auto entry = m_actions.find(it.key());
        if (entry != m_actions.end()) entry->second.bindings = bindings;
        m_saved[it.key()] = std::move(bindings);
    }
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
            case InputSource::Count:
                break;
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
    m_struck.reset();
    if (keyboardOurs) {
        m_text = input.text();
        for (int key = 0; key <= MAX_KEY; ++key) {
            m_typed[static_cast<size_t>(key)] = input.wasKeyTyped(key);
            if (!m_struck && input.wasKeyStruck(key)) m_struck = InputBinding{InputSource::Key, key, 1.0f};
        }
    }
    for (int button = 0; button <= MAX_MOUSE_BUTTON && pointerOurs && !m_struck; ++button) {
        if (input.wasButtonStruck(button)) m_struck = InputBinding{InputSource::MouseButton, button, 1.0f};
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
                case InputSource::Count:
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
    m_struck.reset();
    m_payload.fill(0);
    m_payloadSize  = 0;

    // Zeroed, as before the first tick, so nothing reads the last session's.
    m_command = InputCommand{};
    defineEngineActions();
}

void InputMap::beginTick(uint32_t tick) {
    m_command.sequence = m_nextSequence++;
    m_command.tick     = tick;
    m_command.view     = m_view;
    m_command.payload     = m_payload;
    m_command.payloadSize = m_payloadSize;
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

bool InputMap::setPayload(const uint8_t* bytes, size_t size) {
    if (size > MAX_COMMAND_PAYLOAD) return false;
    m_payloadSize = static_cast<uint8_t>(bytes ? size : 0);
    m_payload.fill(0);
    if (bytes) std::copy(bytes, bytes + m_payloadSize, m_payload.begin());
    return true;
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

#include "platform/input/input_binding.h"

#include <string>

namespace Vkm::Engine {

std::string bindingName(const InputBinding& binding) {
    return std::visit([](auto control) { return std::string(Reflect::enumName(control)); }, binding.control);
}

std::optional<InputBinding> bindingNamed(std::string_view name) {
    Platform::MouseButton button = Platform::MouseButton::Left;
    if (Reflect::enumFromNameChecked(name, button)) return InputBinding::mouse(button);
    Platform::Key key = Platform::Key::Space;
    if (Reflect::enumFromNameChecked(name, key)) return InputBinding::key(key);
    return std::nullopt;
}

} // namespace Vkm::Engine

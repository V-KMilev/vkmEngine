#include "lab_actions.h"

#include <vector>

#include "platform/input/input_map.h"

namespace Lab {

std::string keyFor(const Vkm::Engine::InputMap& input, std::string_view action) {
    const std::vector<Vkm::Engine::InputBinding>& bound = input.bindings(action);
    return bound.empty() ? std::string{} : Vkm::Engine::bindingName(bound.front());
}

} // namespace Lab

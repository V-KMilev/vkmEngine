#define VKM_LOG_CATEGORY "INPUT"

#include "io/project/actions_json.h"

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "io/json/json_number.h"

namespace Vkm::Engine {

std::vector<InputAction> actionsFromJson(const nlohmann::json& doc) {
    std::vector<InputAction> actions;
    if (!doc.is_object()) return actions;
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        InputAction action{it.key(), {}};
        const nlohmann::json& list = it.value();
        for (size_t i = 0; list.is_array() && i < list.size(); ++i) {
            const nlohmann::json& entry = list[i];
            const bool scaled = entry.is_object() && entry.contains("bind");
            const nlohmann::json& named = scaled ? entry["bind"] : entry;
            std::optional<InputBinding> binding =
                named.is_string() ? bindingNamed(named.get<std::string>()) : std::nullopt;
            if (binding && scaled) {
                const nlohmann::json scale = entry.value("scale", nlohmann::json(1.0f));
                if (detail::fitsIn<float>(scale)) binding->scale = scale.get<float>();
                else binding.reset();
            }
            if (binding) {
                action.bindings.push_back(*binding);
                continue;
            }
            const std::string held = entry.dump();
            LOG_WARNING(
                "Action '%s' binds %s, which is no key or button; skipped",
                it.key().c_str(),
                held.c_str()
            );
        }
        if (!list.is_array() || (!list.empty() && action.bindings.empty())) {
            LOG_WARNING("Action '%s' has no binding that can be read; left out", it.key().c_str());
            continue;
        }
        actions.push_back(std::move(action));
    }
    return actions;
}

nlohmann::json actionsToJson(const std::vector<InputAction>& actions) {
    nlohmann::json doc = nlohmann::json::object();
    for (const InputAction& action : actions) {
        nlohmann::json list = nlohmann::json::array();
        for (const InputBinding& binding : action.bindings) {
            if (binding.scale == 1.0f) list.push_back(bindingName(binding));
            else list.push_back({{"bind", bindingName(binding)}, {"scale", binding.scale}});
        }
        doc[action.name] = std::move(list);
    }
    return doc;
}

} // namespace Vkm::Engine

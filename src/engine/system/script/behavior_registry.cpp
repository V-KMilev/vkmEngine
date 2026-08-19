#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior_registry.h"

#include <algorithm>

#include "logger.h"

namespace Vkm::Engine {

BehaviorRegistry& BehaviorRegistry::get() {
    static BehaviorRegistry instance;
    return instance;
}

void BehaviorRegistry::registerBehavior(std::string name, Factory factory) {
    if (m_factories.count(name)) {
        LOG_WARNING("Behavior '%s' already registered; overwriting", name.c_str());
    }
    m_factories[std::move(name)] = std::move(factory);
}

std::unique_ptr<Behavior> BehaviorRegistry::create(const std::string& name) const {
    auto it = m_factories.find(name);
    if (it == m_factories.end()) {
        LOG_ERROR("No behavior registered for '%s' (typo? unregistered behavior?)", name.c_str());
        return nullptr;
    }
    return it->second();
}

bool BehaviorRegistry::contains(const std::string& name) const {
    return m_factories.count(name) != 0;
}

std::vector<std::string> BehaviorRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(m_factories.size());
    for (const auto& [name, factory] : m_factories) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

void BehaviorRegistry::clear() {
    m_factories.clear();
}

} // namespace Vkm::Engine

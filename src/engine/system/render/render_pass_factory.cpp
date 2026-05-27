#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_pass_factory.h"

#include <utility>

#include "logger.h"

#include "system/render/render_pass.h"  // unique_ptr<RenderPass> needs a complete type

namespace Engine {

RenderPassFactory& RenderPassFactory::get() {
    static RenderPassFactory instance;
    return instance;
}

void RenderPassFactory::registerPass(std::string name, Builder builder) {
    if (!builder) {
        LOG_WARNING("RenderPassFactory: refusing to register null builder for '%s'", name.c_str());
        return;
    }
    m_builders[std::move(name)] = std::move(builder);
}

std::unique_ptr<RenderPass> RenderPassFactory::create(const std::string& name,
                                                     ResourceManager& resources) const {
    auto it = m_builders.find(name);
    if (it == m_builders.end()) {
        LOG_ERROR("RenderPassFactory: no builder registered for pass '%s'", name.c_str());
        return nullptr;
    }
    return it->second(resources);
}

std::vector<std::string> RenderPassFactory::registeredNames() const {
    std::vector<std::string> names;
    names.reserve(m_builders.size());
    for (const auto& [k, _] : m_builders) names.push_back(k);
    return names;
}

bool RenderPassFactory::has(const std::string& name) const {
    return m_builders.find(name) != m_builders.end();
}

void RenderPassFactory::clear() {
    m_builders.clear();
}

} // namespace Engine

#include "resource/resource.h"

#include <nlohmann/json.hpp>

#include "l_assert.h"

namespace Vkm::Engine {

namespace {
// Deep-copy the source descriptor so a copied Resource owns its own json
// (null stays null). Shared by the copy ctor and copy-assignment.
std::unique_ptr<nlohmann::json> cloneSource(const std::unique_ptr<nlohmann::json>& src) {
    return src ? std::make_unique<nlohmann::json>(*src) : nullptr;
}
} // namespace

// Out-of-line so the header only needs the json forward-declaration; the
// full template definition is only seen here.
Resource::Resource() = default;
Resource::~Resource() = default;

Resource::Resource(const Resource& other)
    : m_name(other.m_name)
    , m_hidden(other.m_hidden)
    , m_source(cloneSource(other.m_source))
{}


Resource::Resource(Resource && other) noexcept = default;
Resource& Resource::operator=(Resource && other) noexcept = default;

nlohmann::json& Resource::sourceJson() {
    if (!m_source) m_source = std::make_unique<nlohmann::json>();
    return *m_source;
}

const nlohmann::json& Resource::sourceJson() const {
    VKM_ASSERT(m_source != nullptr, "Resource::sourceJson() called on a resource with no source - guard with hasSource()");
    return *m_source;
}

} // namespace Vkm::Engine

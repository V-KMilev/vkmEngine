#include "resource/resource.h"

#include <nlohmann/json.hpp>

#include "l_assert.h"

namespace Engine {

// Out-of-line so the header only needs the json forward-declaration; the
// full template definition is only seen here.
Resource::Resource() = default;
Resource::~Resource() = default;

// A copy is a DUPLICATE: the caller must give it a distinct name before
// re-adding (two live assets can't share a name), which is the duplicator's job.
Resource::Resource(const Resource& other)
    : version(other.version)
    , name(other.name)
    , hidden(other.hidden)
    , source(other.source ? std::make_unique<nlohmann::json>(*other.source)
                          : nullptr)
{}

Resource& Resource::operator=(const Resource& other) {
    if (this == &other) return *this;
    version = other.version;
    name    = other.name;
    hidden  = other.hidden;
    source  = other.source ? std::make_unique<nlohmann::json>(*other.source)
                           : nullptr;
    return *this;
}

Resource::Resource(Resource && other) noexcept = default;
Resource& Resource::operator=(Resource && other) noexcept = default;

nlohmann::json& Resource::sourceJson() {
    if (!source) source = std::make_unique<nlohmann::json>();
    return *source;
}

const nlohmann::json& Resource::sourceJson() const {
    VKM_ASSERT(source != nullptr, "Resource::sourceJson() called on a resource with no source - guard with hasSource()");
    return *source;
}

} // namespace Engine

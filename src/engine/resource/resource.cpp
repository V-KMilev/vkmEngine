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

// A copy is a DUPLICATE: the caller must give it a distinct name before
// re-adding (two live assets can't share a name), which is the duplicator's job.
// The uid is deliberately not among the copied members - identity belongs to the
// instance in the manager, and add() stamps a fresh one.
Resource::Resource(const Resource& other)
    : version(other.version)
    , name(other.name)
    , hidden(other.hidden)
    , source(cloneSource(other.source))
{}

Resource& Resource::operator=(const Resource& other) {
    if (this == &other) return *this;
    version = other.version;
    name    = other.name;
    hidden  = other.hidden;
    source  = cloneSource(other.source);
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

} // namespace Vkm::Engine

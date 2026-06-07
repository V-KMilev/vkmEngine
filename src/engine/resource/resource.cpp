#include "resource/resource.h"

#include <nlohmann/json.hpp>

#include "l_assert.h"

namespace Engine {

// Out-of-line so the header only needs the json forward-declaration; the
// full template definition is only seen here.
Resource::Resource() = default;
Resource::~Resource() = default;

// assetId is deliberately NOT copied (it is on the move ctor, which is =default
// below). A move relocates the SAME asset, so it keeps its identity; a copy is a
// DUPLICATE that must get its own GUID before being re-added - two live assets
// sharing an assetId would collide in ResourceManager's idIndex. The duplicator
// assigns a fresh AssetId (or leaves it invalid) on the copy.
Resource::Resource(const Resource& other)
    : version(other.version)
    , name(other.name)
    , hidden(other.hidden)
    , pinned(other.pinned)
    , source(other.source ? std::make_unique<nlohmann::json>(*other.source)
                          : nullptr)
{}

Resource& Resource::operator=(const Resource& other) {
    if (this == &other) return *this;
    version = other.version;
    name    = other.name;
    hidden  = other.hidden;
    pinned  = other.pinned;
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

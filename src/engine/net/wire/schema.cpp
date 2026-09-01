#define VKM_LOG_CATEGORY "NET"

#include "net/wire/schema.h"

#include "logger.h"

namespace Vkm::Engine {

NetSchema& NetSchema::get() {
    static NetSchema s_schema;
    return s_schema;
}

void NetSchema::add(NetType type) {
    if (m_types.size() >= MAX_TYPES) {
        LOG_ERROR("'%s' is the %zu'th replicated type and a snapshot's mask holds %zu; "
                  "it is not registered",
                  type.name.c_str(), m_types.size() + 1, MAX_TYPES);
        return;
    }
    if (indexOf(type.name) >= 0) {
        LOG_ERROR("two component types registered as '%s'; the second is ignored",
                  type.name.c_str());
        return;
    }
    m_types.push_back(std::move(type));
}

int NetSchema::indexOf(std::string_view name) const {
    for (size_t i = 0; i < m_types.size(); ++i) {
        if (m_types[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

uint32_t NetSchema::fingerprint() const {
    // FNV-1a over each name and its policy, in order. Order participates
    // because the index is the wire identity: two ends registering the same
    // names in a different order agree on every name and on nothing else.
    uint32_t hash = 2166136261u;
    const auto mix = [&hash](uint8_t byte) {
        hash ^= byte;
        hash *= 16777619u;
    };
    for (const NetType& type : m_types) {
        for (char c : type.name) mix(static_cast<uint8_t>(c));
        mix(static_cast<uint8_t>(type.policy));
        mix(0);
    }
    return hash;
}

std::string NetSchema::describe() const {
    std::string text;
    for (size_t i = 0; i < m_types.size(); ++i) {
        if (i > 0) text += ", ";
        text += m_types[i].name;
        if (m_types[i].policy == NetPolicy::OwnerOnly) text += " (owner)";
    }
    return text.empty() ? "nothing" : text;
}

} // namespace Vkm::Engine

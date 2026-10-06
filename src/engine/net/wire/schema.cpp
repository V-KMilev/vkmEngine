#define VKM_LOG_CATEGORY "NET"

#include "net/wire/schema.h"

#include <string_view>

#include "logger.h"

#include "core/fnv1a.h"
#include "net/prediction/command.h"
#include "net/wire/protocol.h"
#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Fold in the constants a codec's bits are laid out by.
 *
 * The same list can still be laid out differently: another position step
 * decodes every coordinate at another scale.
 *
 * @param hash The fingerprint so far.
 * @return @p hash folded with the quantiser's steps and the fields' widths.
 */
uint32_t foldWireLayout(uint32_t hash) {
    const float scales[] = {
        Quantize::POSITION_STEP,
        Quantize::WORLD_EXTENT,
        Quantize::MAX_SPEED,
        NET_MAX_SCALE
    };
    const uint32_t widths[] = {
        Quantize::ROTATION_BITS,
        Quantize::VELOCITY_BITS,
        NET_AXIS_BITS,
        MAX_INPUT_ACTIONS
    };
    hash = fnv1a32(std::string_view(reinterpret_cast<const char*>(scales), sizeof(scales)), hash);
    return fnv1a32(std::string_view(reinterpret_cast<const char*>(widths), sizeof(widths)), hash);
}

} // namespace

NetSchema& NetSchema::get() {
    static NetSchema s_schema;
    return s_schema;
}

void NetSchema::add(NetType type) {
    if (m_types.size() >= MAX_TYPES) {
        LOG_ERROR(
            "'%s' is the %zu'th replicated type and a snapshot's mask holds %zu; it is not registered",
            type.name.c_str(),
            m_types.size() + 1,
            MAX_TYPES
        );
        return;
    }
    if (indexOf(type.name) >= 0) {
        LOG_ERROR("two component types registered as '%s'; the second is ignored", type.name.c_str());
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
    uint32_t hash = foldWireLayout(FNV1A32_OFFSET_BASIS);
    for (const NetType& type : m_types) {
        hash = fnv1a32(type.name, hash);
        // A terminator, so names differing only where one ends cannot collide.
        hash = fnv1a32(std::string_view("", 1), hash);
    }
    return hash;
}

std::string NetSchema::describe() const {
    std::string text;
    for (size_t i = 0; i < m_types.size(); ++i) {
        if (i > 0) text += ", ";
        text += m_types[i].name;
    }
    return text.empty() ? "nothing" : text;
}

} // namespace Vkm::Engine

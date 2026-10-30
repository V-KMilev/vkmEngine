#pragma once

#include <cstdint>
#include <typeinfo>

namespace Vkm::Engine {

/**
 * @brief Type-to-integer mapping for type-erased registries.
 *
 * Assigned on first typeId<T>() call; stable within a run, not across runs.
 * One registry keyed on the RTTI name (typeIdFromInfo), so hot-reloaded game code
 * agrees with the engine and an id outlives the module that first asked.
 */
using TypeId = uint32_t;

namespace detail {
    /**
     * @brief Stable id for @p info from the one process-wide registry in vkm_core.
     *
     * @param info Type to identify.
     * @return Its id, the same for every module.
     */
    TypeId typeIdFromInfo(const std::type_info& info);
}

template<typename T>
TypeId typeId() {
    // Cached per (module, T); the shared registry keeps the modules agreeing.
    static const TypeId s_id = detail::typeIdFromInfo(typeid(T));
    return s_id;
}

} // namespace Vkm::Engine

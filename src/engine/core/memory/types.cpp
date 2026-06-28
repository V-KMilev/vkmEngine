#include "core/memory/types.h"

#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace Engine::detail {

TypeId typeIdFromInfo(const std::type_info& info) {
    // One registry for the whole process. Keyed by std::type_index (RTTI), so a
    // type queried from the engine and from a hot-reloaded game DLL resolves to
    // the same id. The mutex guards the first lookup per type; typeId<T>()'s
    // per-module static cache means steady-state callers never reach here.
    static std::mutex mutex;
    static std::unordered_map<std::type_index, TypeId> ids;
    static TypeId next = 0;

    std::lock_guard<std::mutex> lock(mutex);
    auto [it, inserted] = ids.try_emplace(std::type_index(info), next);
    if (inserted) ++next;
    return it->second;
}

} // namespace Engine::detail

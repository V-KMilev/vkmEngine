#include "core/memory/types.h"

#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace Vkm::Engine::detail {

TypeId typeIdFromInfo(const std::type_info& info) {
    // One registry for the whole process, keyed by std::type_index (RTTI) so a
    // type queried from the engine and from a hot-reloaded game DLL resolves to
    // the same id. The mutex guards only the first lookup per type.
    static std::mutex s_mutex;
    static std::unordered_map<std::type_index, TypeId> s_ids;
    static TypeId s_next = 0;

    std::lock_guard<std::mutex> lock(s_mutex);
    auto [it, inserted] = s_ids.try_emplace(std::type_index(info), s_next);
    if (inserted) ++s_next;
    return it->second;
}

} // namespace Vkm::Engine::detail

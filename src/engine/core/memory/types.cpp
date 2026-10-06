#include "core/memory/types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Vkm::Engine::detail {

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// Past the decimal length and the identifier it counts.
size_t skipSourceName(const std::string& name, size_t at) {
    size_t length = 0;
    while (at < name.size() && isDigit(name[at])) {
        length = length * 10 + static_cast<size_t>(name[at++] - '0');
    }
    return at + length;
}

// Whether the name just past a Z or N is marked internal: an L before the entity's
// identifier, preceded only by namespaces (identifiers, cv-qualifiers, substitutions).
// A template argument's literal cannot look like it: it needs an I first.
bool opensOnInternalName(const std::string& name, size_t at) {
    while (at < name.size() && (name[at] == 'r' || name[at] == 'V' || name[at] == 'K')) ++at;
    while (at < name.size()) {
        if (isDigit(name[at])) {
            at = skipSourceName(name, at);
        } else if (name[at] == 'S' && at + 1 < name.size() && name[at + 1] == 't') {
            at += 2;
        } else if (name[at] == 'S') {
            const size_t end = name.find('_', at);
            if (end == std::string::npos) return false;
            at = end + 1;
        } else {
            break;
        }
    }
    return at + 1 < name.size() && name[at] == 'L' && isDigit(name[at + 1]);
}

// Whether two translation units can each declare a type of this name and mean two
// types: an anonymous namespace (Itanium _GLOBAL__N, MSVC `anonymous namespace'); an
// enclosing entity with internal linkage, which Itanium marks with an L (MSVC does
// not, so only GCC and Clang catch it); or Clang's unnamed-type name `$_0`.
// Walked a token at a time so identifier letters and digits are never read as markers.
bool isTranslationUnitLocal(const std::string& name) {
    if (name.find("_GLOBAL__N") != std::string::npos
        || name.find("anonymous namespace") != std::string::npos
        || name.find('$') != std::string::npos) {
        return true;
    }

    for (size_t at = 0; at < name.size();) {
        const char c = name[at];
        if (isDigit(c)) {
            at = skipSourceName(name, at);
        } else if (c == 'Z' || c == 'N') {
            if (opensOnInternalName(name, at + 1)) return true;
            ++at;
        } else if (c == 'L' && at + 1 < name.size() && name[at + 1] != '_') {
            // A literal: its type, then a value that is not an identifier's length.
            at = isDigit(name[at + 1]) ? skipSourceName(name, at + 1) : at + 2;
            while (at < name.size() && name[at] != 'E') ++at;
        } else if ((c == 'S' || c == 'T' || c == 'A' || c == '_')
            && at + 1 < name.size() && isDigit(name[at + 1])) {
            // A substitution, template parameter, array bound or discriminator: not a length.
            ++at;
            while (at < name.size() && isDigit(name[at])) ++at;
        } else {
            ++at;
        }
    }
    return false;
}

} // namespace

TypeId typeIdFromInfo(const std::type_info& info) {
    // The mutex guards only the first lookup per type.
    static std::mutex s_mutex;
    static std::unordered_map<std::string, TypeId> s_ids;
    static TypeId s_next = 0;

    // Keyed on a copy of the name, never &info: a hot reload unloads the gameplay
    // module holding a type_info while this map lives on. A TU-local name is not
    // unique, so its key adds the address too, as a number never read through.
    std::string key = info.name();
    if (isTranslationUnitLocal(key)) {
        key += '@';
        key += std::to_string(reinterpret_cast<std::uintptr_t>(&info));
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    auto [it, inserted] = s_ids.try_emplace(std::move(key), s_next);
    if (inserted) ++s_next;
    return it->second;
}

} // namespace Vkm::Engine::detail

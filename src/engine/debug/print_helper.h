#pragma once

#include <string>
#include <type_traits>

#include "logger.h"

namespace Engine {

// Generic template for enums with toString() overloads.
// The returned pointer is only valid until the next call on the same thread -
// matches printf-style log usage where the format expansion consumes the
// pointer immediately.
template<typename EnumType>
const char* enumToString(EnumType type) {
    static_assert(std::is_enum_v<EnumType>, "Template parameter must be an enum");
    thread_local std::string enumStr;
    enumStr = std::string(toString(type)) + "::" + std::to_string(static_cast<int>(type));
    return enumStr.c_str();
}

} // namespace Engine

#pragma once

#include <type_traits>

#include "logger.h"

// TODO: Make this constexpr

// Generic template for enums with toString() overloads
template<typename EnumType>
const char* enumToString(EnumType type) {
    static_assert(std::is_enum_v<EnumType>, "Template parameter must be an enum");

    // Store the string in thread_local storage to avoid dangling pointer
    // This is evaluated once per thread per unique enum value
    static thread_local std::string enumStr = std::string(toString(type)) + "::" + std::to_string(static_cast<int>(type));
    return enumStr.c_str();
}

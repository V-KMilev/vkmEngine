#pragma once

#include <type_traits>

#include "logger.h"

// Generic template for enums with toString() overloads
template<typename EnumType>
inline const char* enumToString(EnumType type) {
    if(!std::is_enum<EnumType>::value) {
        LOG_WARNING("Template parameter must be an enum");
        return "UNKNOWN";
    }

    static thread_local std::string enumStr;
    enumStr = std::string(toString(type)) + "::" + std::to_string(int(type));
    return enumStr.c_str();
}

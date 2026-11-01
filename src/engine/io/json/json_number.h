#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace Vkm::Engine::detail {

// Numbers out of JSON. Converting a number its type cannot hold is undefined, and a damaged or
// hand-edited file can hold any number, so every read of one checks first.

/// Whether @p j is a number a T holds; a fraction counts by its whole part, as the conversion takes it.
template<typename T>
bool fitsIn(const nlohmann::json& j) {
    using Limits = std::numeric_limits<T>;
    if (!j.is_number()) return false;
    if constexpr (!Limits::is_integer) {
        const double value = j.get<double>();
        return value >= -static_cast<double>(Limits::max()) && value <= static_cast<double>(Limits::max());
    } else {
        if (j.is_number_unsigned()) return j.get<uint64_t>() <= static_cast<uint64_t>(Limits::max());
        if (j.is_number_integer()) {
            const int64_t value = j.get<int64_t>();
            if constexpr (Limits::is_signed) {
                return value >= static_cast<int64_t>(Limits::lowest())
                    && value <= static_cast<int64_t>(Limits::max());
            } else {
                return value >= 0 && static_cast<uint64_t>(value) <= static_cast<uint64_t>(Limits::max());
            }
        }
        const double above = std::ldexp(1.0, Limits::digits);  // max + 1, exact in a double
        const double value = j.get<double>();
        return value < above && (Limits::is_signed ? value >= -above : value > -1.0);
    }
}

/// @p j as a T; anything but a number a T holds throws, true and false included.
template<typename T>
T jsonToNumber(const nlohmann::json& j) {
    if (!j.is_number()) {
        const std::string held = j.type_name();
        throw nlohmann::json::type_error::create(302, "type must be number, but is " + held, &j);
    }
    if (!fitsIn<T>(j)) throw nlohmann::json::out_of_range::create(406, "number out of its field's range", &j);
    return j.get<T>();
}

/// json::value for a number: @p fallback where @p key is absent, jsonToNumber where present.
template<typename T>
T numberAt(const nlohmann::json& j, const char* key, T fallback) {
    const auto it = j.find(key);
    return it == j.end() ? fallback : jsonToNumber<T>(*it);
}

} // namespace Vkm::Engine::detail

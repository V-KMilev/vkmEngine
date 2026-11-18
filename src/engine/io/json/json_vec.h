#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "io/json/json_number.h"

namespace Vkm::Engine::detail {

// GLM <-> flat JSON arrays. A value of the wrong shape - no array of that many numbers a float
// holds - throws, as a leaf of the wrong type does.

inline nlohmann::json vec2ToJson(const glm::vec2& v) { return {v.x, v.y}; }
inline nlohmann::json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }
inline nlohmann::json vec4ToJson(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }
inline nlohmann::json quatToJson(const glm::quat& q) { return {q.w, q.x, q.y, q.z}; }

/// The first @p N numbers of the array @p j, each a float; anything else throws.
template<glm::length_t N>
glm::vec<N, float> jsonToVec(const nlohmann::json& j) {
    const nlohmann::json::array_t& items = j.get_ref<const nlohmann::json::array_t&>();
    if (items.size() < N) {
        throw nlohmann::json::type_error::create(
            302,
            "expected an array of " + std::to_string(N) + " numbers",
            &j
        );
    }
    glm::vec<N, float> v;
    for (glm::length_t i = 0; i < N; ++i) v[i] = jsonToNumber<float>(items[i]);
    return v;
}

/// @p j as quatToJson writes it, w first; anything else throws.
inline glm::quat jsonToQuat(const nlohmann::json& j) {
    const glm::vec4 v = jsonToVec<4>(j);
    return glm::quat(v[0], v[1], v[2], v[3]);
}

} // namespace Vkm::Engine::detail

#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine::detail {

// Shared GLM <-> JSON conversion helpers for the IO serializers. Vectors and
// quaternions persist as flat JSON arrays. The fromJson helpers take an
// optional fallback (defaulting to a zero/identity value) and return it when
// the JSON node is missing, the wrong type, or too short - so the same call
// covers both "validated with explicit default" and "caller already guarded
// the shape" use sites.

inline nlohmann::json vec2ToJson(const glm::vec2& v) { return {v.x, v.y}; }
inline nlohmann::json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }
inline nlohmann::json vec4ToJson(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }
inline nlohmann::json quatToJson(const glm::quat& q) { return {q.w, q.x, q.y, q.z}; }

inline glm::vec2 vec2FromJson(const nlohmann::json& j, const glm::vec2& fallback = glm::vec2(0.0f)) {
    if (!j.is_array() || j.size() < 2) return fallback;
    return {j[0], j[1]};
}
inline glm::vec3 vec3FromJson(const nlohmann::json& j, const glm::vec3& fallback = glm::vec3(0.0f)) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0], j[1], j[2]};
}
inline glm::vec4 vec4FromJson(const nlohmann::json& j, const glm::vec4& fallback = glm::vec4(0.0f)) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return {j[0], j[1], j[2], j[3]};
}
inline glm::quat quatFromJson(const nlohmann::json& j, const glm::quat& fallback = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return glm::quat(static_cast<float>(j[0]), static_cast<float>(j[1]),
                     static_cast<float>(j[2]), static_cast<float>(j[3]));
}

} // namespace Engine::detail

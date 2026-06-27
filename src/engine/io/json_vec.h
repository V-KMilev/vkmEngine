#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

namespace Engine::detail {

// Shared GLM <-> JSON conversion helpers for the IO serializers. Vectors and
// quaternions persist as flat JSON arrays. The jsonTo* helpers take an optional
// fallback (defaulting to a zero/identity value): a null / absent node returns
// the fallback silently (the "field omitted, keep the default" path), while a
// node that is present but the wrong type or too short returns the fallback and
// logs a warning - genuinely malformed data worth surfacing.

inline nlohmann::json vec2ToJson(const glm::vec2& v) { return {v.x, v.y}; }
inline nlohmann::json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }
inline nlohmann::json vec4ToJson(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }
inline nlohmann::json quatToJson(const glm::quat& q) { return {q.w, q.x, q.y, q.z}; }

inline glm::vec2 jsonToVec2(const nlohmann::json& j, const glm::vec2& fallback = glm::vec2(0.0f)) {
    if (j.is_null()) return fallback;
    if (!j.is_array() || j.size() < 2) {
        LOG_WARNING("jsonToVec2: expected a 2-element array, got '%s'; using fallback", j.dump().c_str());
        return fallback;
    }
    return {j[0], j[1]};
}
inline glm::vec3 jsonToVec3(const nlohmann::json& j, const glm::vec3& fallback = glm::vec3(0.0f)) {
    if (j.is_null()) return fallback;
    if (!j.is_array() || j.size() < 3) {
        LOG_WARNING("jsonToVec3: expected a 3-element array, got '%s'; using fallback", j.dump().c_str());
        return fallback;
    }
    return {j[0], j[1], j[2]};
}
inline glm::vec4 jsonToVec4(const nlohmann::json& j, const glm::vec4& fallback = glm::vec4(0.0f)) {
    if (j.is_null()) return fallback;
    if (!j.is_array() || j.size() < 4) {
        LOG_WARNING("jsonToVec4: expected a 4-element array, got '%s'; using fallback", j.dump().c_str());
        return fallback;
    }
    return {j[0], j[1], j[2], j[3]};
}
inline glm::quat jsonToQuat(const nlohmann::json& j, const glm::quat& fallback = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
    if (j.is_null()) return fallback;
    if (!j.is_array() || j.size() < 4) {
        LOG_WARNING("jsonToQuat: expected a 4-element array, got '%s'; using fallback", j.dump().c_str());
        return fallback;
    }
    return glm::quat(static_cast<float>(j[0]), static_cast<float>(j[1]),
                     static_cast<float>(j[2]), static_cast<float>(j[3]));
}

} // namespace Engine::detail

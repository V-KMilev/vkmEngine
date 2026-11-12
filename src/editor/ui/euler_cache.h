#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief Stable Euler-angle edit cache for quaternion-backed rotations.
 *
 * Quaternion -> Euler is many-to-one and singular at +/-90 deg, so re-deriving it each
 * frame snaps typed axes and jitters. The edited Euler stays the truth, reseeded only
 * when the quaternion changes from outside. Call sync() before drawing, then:
 * `if (drawVec3Control(..., cache.degrees(), ...)) q = cache.toQuat();`
 *
 * @tparam Key Identifies the rotation source (an EntityId, a keyframe index); a new key
 *         reseeds.
 */
template<class Key>
class EulerCache {
    public:
        EulerCache() = default;
        ~EulerCache() = default;

        EulerCache(const EulerCache& other) = default;
        EulerCache& operator=(const EulerCache& other) = default;

        EulerCache(EulerCache && other) = default;
        EulerCache& operator=(EulerCache && other) = default;

    public:
        /**
         * @brief Reseed from @p q if the key changed or @p q diverged from the cache.
         *
         * @param key This frame's rotation source.
         * @param q   The stored rotation.
         */
        void sync(const Key& key, const glm::quat& q) {
            const glm::quat cached  = glm::quat(glm::radians(m_degrees));
            const bool keyChanged   = !m_haveKey || !(m_key == key);
            const bool quatDiverged = glm::abs(glm::dot(cached, q)) < 0.9999f;
            if (keyChanged || quatDiverged) {
                m_degrees = glm::degrees(glm::eulerAngles(q));
                m_key     = key;
                m_haveKey = true;
            }
        }

        float* degrees() { return &m_degrees.x; }
        glm::quat toQuat() const { return glm::normalize(glm::quat(glm::radians(m_degrees))); }

    private:
        glm::vec3 m_degrees{0.0f};
        Key       m_key{};
        bool      m_haveKey = false;
};

} // namespace Vkm::Engine

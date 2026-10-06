#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief On a key, throws a crate the way its character faces, and cleans up after itself.
 *
 * A crate that hits a character with a ragdoll knocks it down: the ragdoll goes live with the crate's
 * momentum, and the character stands again after `downFor`. Crates die on a timer so a session cannot
 * fill the world. Offline only: a crate built from code is nothing a server could tell a client to build.
 */
class LabThrower : public ReflectedBehavior<LabThrower> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        float speed    = 14.0f;  ///< Launch speed along the character's facing, m/s.
        float lifetime = 6.0f;   ///< Seconds a thrown crate survives.
        float size     = 0.25f;  ///< Half-extent of the cube thrown.
        float downFor  = 4.0f;   ///< Seconds a character a crate knocks down lies there; 0 keeps it down.

    private:
        struct Thrown {
            EntityId  entity;
            glm::vec3 momentum  = {0.0f, 0.0f, 0.0f};  ///< At launch, kg m/s
            float     remaining = 0.0f;
        };

        struct Felled {
            EntityId character;
            float    remaining = 0.0f;
        };

    private:
        void knockDown(EntityId character, const Thrown& crate, const glm::vec3& at);

    private:
        MeshHandle          m_mesh;
        MaterialHandle      m_material;
        std::vector<Thrown> m_live;
        std::vector<Felled> m_felled;
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabThrower)
    VKM_F(speed)
    VKM_F(lifetime)
    VKM_F(size)
    VKM_F(downFor)
VKM_REFLECT_END()

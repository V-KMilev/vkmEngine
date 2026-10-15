#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/random.h"
#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "system/script/reflected_behavior.h"

namespace City {

using namespace Vkm::Engine;

/**
 * @brief A grid of city blocks walked at street level: the GPU occlusion cull's load.
 *
 * Every block is ringed with buildings whose street faces carry ledges, air conditioners and
 * dishes, and its pavements lamps and parked cars - tens of thousands of objects, nearly all of
 * them hidden behind the block in front of the camera at any moment, so their vertex work is
 * what the cull (RenderSettings::occlusionCulling, on in this project) saves. The first play
 * tick builds it from a fixed seed, so captures compare; the camera drives a loop of streets.
 */
class CityBlock : public ReflectedBehavior<CityBlock> {
    public:
        /// Far enough to see down a street to the city's edge.
        static constexpr float CAMERA_FAR = 900.0f;
        /// The camera's height above the street.
        static constexpr float EYE_HEIGHT = 1.7f;

    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        // Read once, when the first play tick builds the city: a change takes a play restart.
        int   blocks          = 10;     ///< Blocks along each side of the grid.
        float blockSize       = 44.0f;  ///< A block's edge in metres, its buildings included.
        float streetWidth     = 14.0f;  ///< Kerb to kerb between two blocks.
        int   detailsPerFloor = 4;      ///< Ledges, units and dishes along a facade, per floor.
        int   carsPerSide     = 3;      ///< Cars parked along each side of a block.
        int   lampsPerSide    = 3;      ///< Lamp posts along each side of a block.

        // Read every frame.
        bool  scriptedCamera = true;  ///< Drive the loop of streets; off leaves the camera alone.
        float cameraSpeed    = 8.0f;  ///< Metres a second along the loop.

    private:
        /**
         * @brief Create a mesh entity, unnamed: there are tens of thousands.
         *
         * @param mesh     What it draws.
         * @param material What it draws with.
         * @param position World position of its centre.
         * @param scale    Its size, the meshes being unit-sized.
         * @param rotation Its turn, applied before the scale's axes reach the world.
         */
        void place(
            MeshHandle mesh,
            MaterialHandle material,
            const glm::vec3& position,
            const glm::vec3& scale,
            const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
        );

        /**
         * @brief One building of a block's ring, and its street face's details.
         *
         * @param centre   Its footprint's centre on the ground.
         * @param size     Its footprint.
         * @param outward  The street its face looks onto, a unit axis.
         */
        void buildBuilding(const glm::vec3& centre, const glm::vec2& size, const glm::vec3& outward);

        /**
         * @brief A block's buildings, lamps and cars.
         *
         * @param centre The block's centre on the ground.
         */
        void buildBlock(const glm::vec3& centre);

        /**
         * @brief Where the camera is @p distance metres round the loop.
         *
         * @param distance Metres from the loop's start.
         * @return The point on the loop, on the ground.
         */
        glm::vec3 loopPoint(float distance) const;

    private:
        MeshHandle m_cube;
        MeshHandle m_sphere;
        MeshHandle m_cylinder;

        MaterialHandle m_ground;
        MaterialHandle m_walls[3];   ///< Concrete, brick, render.
        MaterialHandle m_metal;      ///< Units, dishes, lamp posts.
        MaterialHandle m_ledge;
        MaterialHandle m_cars[4];
        MaterialHandle m_lampGlow;   ///< The lamp heads.

        EntityId m_camera;
        float    m_travelled  = 0.0f;
        float    m_loopRadius = 0.0f;  ///< Half the side of the loop of streets, set at build.
        bool     m_built      = false;

        Math::Rng m_rng;  ///< The build's placement stream, fixed seed.
};

} // namespace City

VKM_REFLECT_BEGIN(::City::CityBlock)
    VKM_F(blocks)
    VKM_F(blockSize)
    VKM_F(streetWidth)
    VKM_F(detailsPerFloor)
    VKM_F(carsPerSide)
    VKM_F(lampsPerSide)
    VKM_F(scriptedCamera)
    VKM_F(cameraSpeed)
VKM_REFLECT_END()

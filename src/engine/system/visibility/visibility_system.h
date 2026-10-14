#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/system.h"
#include "system/visibility/visibility.h"

namespace Vkm::Engine {

struct Camera;

/**
 * @brief Builds the per-frame Visibility result (every scene mesh, and which of them the camera sees).
 *
 * Runs after the Transform stage, whose world transforms it culls. Views through
 * FrameContext::hostView when offered, else the active camera; culls every Mesh
 * in parallel (frustum -> distance -> screen size) and publishes on
 * FrameContext::visibility. Every drawable mesh is also gathered, seen or not,
 * so off-screen occluders and offline captures survive frustum culling.
 */
class VisibilitySystem : public System {
    public:
        /// One in this many fixed meshes (Mesh::movable off) is worked out again each frame and held
        /// to what was kept.
        static constexpr uint32_t FIXED_CHECK_STRIDE = 64;

    public:
        VisibilitySystem() = default;
        ~VisibilitySystem() override = default;

        VisibilitySystem(const VisibilitySystem& other) = delete;
        VisibilitySystem& operator=(const VisibilitySystem& other) = delete;

        VisibilitySystem(VisibilitySystem && other) = delete;
        VisibilitySystem& operator=(VisibilitySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief What every object's columns are made from beyond its own components.
         *
         * While the world and the asset graph hold, last frame's columns are comparable index by
         * index; while all of it holds, a fixed mesh keeps its own.
         */
        struct ObjectSources {
            uint64_t sceneEpoch     = 0;
            uint64_t assetEpoch     = 0;      ///< ResourceManager::epoch().
            uint64_t meshAssets     = 0;      ///< ResourceManager::revisionOf<MeshAsset>().
            uint64_t materialAssets = 0;      ///< Of MaterialAsset while a sea reads them, else 0.
            bool     sea            = false;
            bool     throughAnEye   = false;

            bool operator==(const ObjectSources& other) const {
                return sceneEpoch == other.sceneEpoch && assetEpoch == other.assetEpoch
                    && meshAssets == other.meshAssets && materialAssets == other.materialAssets
                    && sea == other.sea && throughAnEye == other.throughAnEye;
            }

            bool operator!=(const ObjectSources& other) const { return !(*this == other); }
        };

        /**
         * @brief SparseSet::structure() of the storages a fixed mesh's entity reads beyond its Mesh.
         *
         * When one moves, each kept mesh checks its entity still has what it was worked out from.
         */
        struct ObjectMemberships {
            uint64_t transforms      = 0;
            uint64_t worldTransforms = 0;
            uint64_t lods            = 0;

            bool operator==(const ObjectMemberships& other) const {
                return transforms == other.transforms && worldTransforms == other.worldTransforms
                    && lods == other.lods;
            }

            bool operator!=(const ObjectMemberships& other) const { return !(*this == other); }
        };

    private:
        /**
         * @brief Resolve the view this frame renders through into m_result.
         *
         * A host's free view, else the scene camera it names, else the active
         * camera (cached, scanned on a miss). A camera's pose is its resolved
         * world pose, so one parented to a rig renders from the rig's place.
         *
         * @param ctx            Supplies the scene and host view.
         * @param viewportAspect Taken by a camera in auto-aspect mode.
         * @return false, with m_result.hasCamera left false, when there is
         *         nothing to render through.
         */
        bool resolveCamera(const FrameContext& ctx, float viewportAspect);

        /**
         * @brief Write one camera's matrices, position and depth of field into m_result.
         *
         * @param camera         Projection and depth-of-field parameters.
         * @param position       Eye, world space.
         * @param rotation       Eye, world space.
         * @param viewportAspect Used while camera.aspect <= 0.
         * @param entity         The scene camera, or empty for a free view.
         */
        void publishCamera(
            const Camera& camera,
            const glm::vec3& position,
            const glm::quat& rotation,
            float viewportAspect,
            EntityId entity
        );

    private:
        EntityId m_cachedCameraEntity{};
        uint64_t m_cameraEpoch = 0;  ///< Scene::epoch() of the held camera.
        Visibility m_result;
        bool m_noCameraLogged = false;  ///< The no-camera warning fires once per gap.

        std::vector<uint8_t> m_state;  ///< Per object: the cull's STATE_* bits.

        /// Per object: what is kept of a fixed mesh (Mesh::movable off), FIXED_* bits; zero for the rest.
        std::vector<uint8_t>  m_fixed;
        std::vector<uint64_t> m_stamps;  ///< Per object: SparseSet::stampAt() of the Mesh its columns are.

        ObjectSources     m_sources;      ///< Last frame's, which m_fixed and the columns are of.
        ObjectMemberships m_memberships;  ///< Last frame's.
        bool              m_sourcesValid    = false;
        uint32_t          m_checkPhase      = 0;           ///< Which residue mod FIXED_CHECK_STRIDE is checked.
        uint64_t          m_fixedMovedEpoch = UINT64_MAX;  ///< Scene::epoch() a moved fixed mesh was logged in.
};

} // namespace Vkm::Engine

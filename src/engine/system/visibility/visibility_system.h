#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "core/frame/visibility.h"

namespace Vkm::Engine {

struct Camera;
struct CameraView;

/**
 * @brief Builds the per-frame Visibility result (every scene mesh, and which of them the camera sees).
 *
 * Runs after the Transform stage, whose world transforms it culls. Views through
 * FrameProducts::hostView when offered, else the active camera; culls every Mesh
 * in parallel (frustum, then screen size, fading within Culling::FADE_BAND) and publishes on
 * FrameProducts::visibility. Every drawable mesh is also gathered, seen or not,
 * so off-screen occluders and offline captures survive frustum culling.
 */
class VisibilitySystem : public System {
    public:
        static constexpr uint32_t FIXED_CHECK_STRIDE = 64;  ///< One in this many fixed meshes (Mesh::movable
                                                            ///< off) is worked out again each frame and held
                                                            ///< to what was kept.

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
            uint64_t sceneEpoch   = 0;
            uint64_t assetEpoch   = 0;      ///< ResourceManager::epoch().
            uint64_t meshAssets   = 0;      ///< ResourceManager::revisionOf<MeshAsset>().
            bool     throughAnEye = false;

            bool operator==(const ObjectSources& other) const {
                return sceneEpoch == other.sceneEpoch && assetEpoch == other.assetEpoch
                    && meshAssets == other.meshAssets && throughAnEye == other.throughAnEye;
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
            uint64_t waters          = 0;

            bool operator==(const ObjectMemberships& other) const {
                return transforms == other.transforms && worldTransforms == other.worldTransforms
                    && lods == other.lods && waters == other.waters;
            }

            bool operator!=(const ObjectMemberships& other) const { return !(*this == other); }
        };

        /**
         * @brief A plant group's levels: the distance they are picked at, and the hand-over from the last.
         */
        struct PlantLevels {
            float reach = 0.0f;  ///< Its plants draw the levels this distance picks.
            float from  = 0.0f;  ///< The distance the levels being handed over from were picked at.
            float shown = 1.0f;  ///< How far the new levels have faded in, 0..1; 1 when none is.
        };

    private:
        /**
         * @brief Resolve the view this frame renders through into m_result.
         *
         * A host's free view, else the scene camera it names, else the active
         * camera (FrameContext::camera). A camera's pose is its resolved
         * world pose, so one parented to a rig renders from the rig's place.
         *
         * @param ctx            Supplies the scene, the host view and the active camera.
         * @param viewportAspect Taken by a camera in auto-aspect mode.
         * @return false, with m_result.hasCamera left false, when there is
         *         nothing to render through.
         */
        bool resolveCamera(const FrameContext& ctx, float viewportAspect);

        /**
         * @brief Write one camera's matrices and position into m_result.
         *
         * @param camera         Viewmodel parameters.
         * @param seen           What it sees (viewThrough).
         * @param viewportAspect Used while camera.aspect <= 0.
         * @param entity         The scene camera, or empty for a free view.
         */
        void publishCamera(
            const Camera& camera,
            const CameraView& seen,
            float viewportAspect,
            EntityId entity
        );

    private:
        Visibility m_result;
        bool       m_noCameraLogged = false;  ///< The no-camera warning fires once per gap.

        std::vector<uint8_t> m_state;   ///< Per object: the cull's STATE_* bits.
        std::vector<uint8_t> m_hidden;  ///< Per slot: the host hides it (HostChrome::hiddenInView).

        std::vector<uint8_t>  m_fixed;   ///< Per object: what is kept of a fixed mesh (Mesh::movable off),
                                         ///< FIXED_* bits; zero for the rest.
        std::vector<uint64_t> m_stamps;  ///< Per object: SparseSet::stampAt() of the Mesh its columns are.

        std::optional<ObjectSources> m_sources;  ///< Last frame's, which m_fixed and the columns are of;
                                                 ///< none when those are not to be compared with this
                                                 ///< frame's.
        ObjectMemberships m_memberships;         ///< Last frame's.
        uint32_t          m_checkPhase   = 0;    ///< Which residue mod FIXED_CHECK_STRIDE is checked.
        uint32_t          m_lastTotal    = 0;    ///< Objects last frame, the terrains' chunks among them.
        uint64_t          m_plantsRevision = 0;  ///< TerrainFrame::plantsRevision of the plants written.
        uint32_t          m_plantFirst     = 0;  ///< The index they began at.
        uint32_t          m_plantCount     = 0;
        std::vector<PlantLevels> m_plantLevels;  ///< Per plant group: its levels and their hand-over.

        uint64_t m_fixedMovedEpoch  = UINT64_MAX;  ///< Scene::epoch() a moved fixed mesh was logged in.
        bool     m_posedWaterLogged = false;       ///< Water on a posed mesh is said once.
};

} // namespace Vkm::Engine

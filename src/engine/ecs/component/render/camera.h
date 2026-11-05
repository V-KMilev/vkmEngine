#pragma once

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Enumeration of camera projection types.
 */
enum class ProjectionType {
    Perspective  = 0,
    Orthographic = 1,
    Count               ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Component representing a camera, containing projection and view parameters.
 */
struct Camera {
    ProjectionType projection  = ProjectionType::Perspective;
    float          fovY        = glm::radians(70.0f);  ///< Vertical, radians
    float          orthoHeight = 10.0f;                ///< Half-height, world units
    float          aspect      = 0.0f;                 ///< Width / height; <= 0 derives it from the viewport
                                                       ///< each frame
    float          zNear       = 0.1f;
    float          zFar        = 1000.0f;
    bool           active      = true;

    float viewmodelFovY = 0.0f;  ///< Vertical field of view the eye draws Viewmodel meshes at, radians; 0 is
                                 ///< fovY's. Arms and a weapon keep their look whatever the world's field of
                                 ///< view (Mesh::seenBy).

    static constexpr float VIEWMODEL_Z_NEAR = 0.01f;  ///< Near plane of the viewmodel's projection: a weapon
                                                      ///< held close is not cut by the world's.

    /**
     * @brief Compute the projection matrix for this camera.
     *
     * @param camera         The camera.
     * @param viewportAspect Used while camera.aspect <= 0.
     * @return The projection matrix.
     */
    static glm::mat4 computeProjection(const Camera& camera, float viewportAspect) {
        const float aspect = camera.aspect > 0.0f ? camera.aspect : viewportAspect;
        if (camera.projection == ProjectionType::Perspective) {
            return glm::perspective(camera.fovY, aspect, camera.zNear, camera.zFar);
        } else {
            const float halfWidth = camera.orthoHeight * aspect;
            return glm::ortho(
                -halfWidth,
                halfWidth,
                -camera.orthoHeight,
                camera.orthoHeight,
                camera.zNear,
                camera.zFar
            );
        }
    }

    /**
     * @brief The projection Viewmodel meshes draw through: viewmodelFovY, VIEWMODEL_Z_NEAR.
     *
     * An orthographic camera's own: a field of view means nothing to it.
     *
     * @param camera         The camera.
     * @param viewportAspect Used while camera.aspect <= 0.
     * @return The projection matrix.
     */
    static glm::mat4 computeViewmodelProjection(const Camera& camera, float viewportAspect) {
        if (camera.projection != ProjectionType::Perspective) {
            return computeProjection(camera, viewportAspect);
        }
        const float aspect = camera.aspect > 0.0f ? camera.aspect : viewportAspect;
        const float fovY   = camera.viewmodelFovY > 0.0f ? camera.viewmodelFovY : camera.fovY;
        return glm::perspective(fovY, aspect, VIEWMODEL_Z_NEAR, camera.zFar);
    }
};

/**
 * @brief The scene's active camera: the one already held, else the lowest-slot active one.
 *
 * A Transform is required too; ties break by lowest slot (see findLowestSlot).
 * A held camera stays the answer while active and posed, even once a lower-slot
 * one activates, so the view does not jump; pass {} for the unconditional rule.
 *
 * @param scene  Scene searched.
 * @param cached A previously returned entity, tested first. Stale or destroyed is safe.
 * @return The active camera entity, or {} when there is none.
 */
EntityId findActiveCamera(const Scene& scene, EntityId cached = {});

/**
 * @brief Which camera the frame looks through, held for every reader of it.
 *
 * The frame, the view a tick steers by, the ear when no listener is placed and a behavior's
 * screen() all ask the engine's (FrameContext::camera), so with two cameras active they agree. The
 * held camera stays the answer as findActiveCamera keeps it; a replaced world, whose slots name
 * other entities, starts the search over.
 */
class ActiveCamera {
    public:
        ActiveCamera()  = default;
        ~ActiveCamera() = default;

        ActiveCamera(const ActiveCamera& other) = default;
        ActiveCamera& operator=(const ActiveCamera& other) = default;

        ActiveCamera(ActiveCamera && other) = default;
        ActiveCamera& operator=(ActiveCamera && other) = default;

    public:
        /**
         * @brief The active camera of @p scene now, held for the next ask.
         *
         * @param scene The world asked about; a new Scene::epoch() forgets the held camera.
         * @return The camera, or {} when none is active.
         */
        EntityId of(const Scene& scene);

    private:
        EntityId m_held{};     ///< The last answer, kept while it stays active and posed.
        uint64_t m_epoch = 0;  ///< Scene::epoch() of m_held.
};

/**
 * @brief What a camera sees: its view and projection, from where it stands.
 */
struct CameraView {
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 position   = glm::vec3(0.0f);  ///< The eye, world space.
};

/**
 * @brief What @p lens sees from @p position, turned by @p rotation.
 *
 * @param lens     The camera.
 * @param position The eye, world space.
 * @param rotation The eye's turn, world space.
 * @param aspect   Taken while lens.aspect <= 0.
 * @return Its view.
 */
CameraView viewThrough(
    const Camera& lens,
    const glm::vec3& position,
    const glm::quat& rotation,
    float aspect
);

/**
 * @brief What @p entity's camera sees from its world pose, a rig's included.
 *
 * @param scene  Holds the camera.
 * @param entity The camera's entity.
 * @param aspect Taken while its Camera::aspect <= 0.
 * @return Its view; none without a Camera and a Transform.
 */
std::optional<CameraView> viewThrough(const Scene& scene, EntityId entity, float aspect);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::ProjectionType, "Perspective", "Orthographic")

VKM_REFLECT_BEGIN(::Vkm::Engine::Camera)
    VKM_F(projection)
    VKM_F(fovY, 0.1745f, 2.967f, "The vertical field of view")
    VKM_F(orthoHeight, 0.1f, 1000.0f, "Half the view's height, in meters")
    VKM_F(aspect, 0.0f, 10.0f, "Width over height; 0 follows the viewport")
    VKM_F(zNear, 0.001f, 100000.0f, "Nearer than this is not drawn")
    VKM_F(zFar, 0.01f, 100000.0f, "Farther than this is not drawn")
    VKM_F(active, "Whether the game may draw through it")
    VKM_F(viewmodelFovY, 0.0f, 2.967f, "The FOV arms and a weapon draw at; 0 is the camera's")
VKM_REFLECT_END()

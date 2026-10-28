#pragma once

#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/reflect.h"
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
    ProjectionType projection = ProjectionType::Perspective;
    /// Vertical, radians
    float fovY                = glm::radians(70.0f);
    float orthoHeight         = 10.0f;                          ///< Half-height, world units
    /// Width / height; <= 0 derives it from the viewport each frame
    float aspect              = 0.0f;
    float zNear               = 0.1f;
    float zFar                = 1000.0f;
    /// World distance held in sharp focus
    float focusDistance       = 10.0f;
    float dofAmount           = 0.0f;                           ///< 0 = off
    /// Widest blur radius, fraction of viewport height (0..0.05)
    float dofMaxBlur          = 0.011f;
    bool active               = true;

    /**
     * @brief Vertical field of view the eye draws Viewmodel meshes at, radians; 0 is fovY's.
     *
     * Arms and a weapon keep their look whatever the world's field of view (Mesh::seenBy).
     */
    float viewmodelFovY = 0.0f;

    /// Near plane of the viewmodel's projection: a weapon held close is not cut by the world's.
    static constexpr float VIEWMODEL_Z_NEAR = 0.01f;

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

/// What a camera sees: its view and projection, from where it stands.
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
    VKM_F(fovY)
    VKM_F(orthoHeight)
    VKM_F(aspect)
    VKM_F(zNear)
    VKM_F(zFar)
    VKM_F(focusDistance)
    VKM_F(dofAmount)
    VKM_F(dofMaxBlur)
    VKM_F(active)
    VKM_F(viewmodelFovY)
VKM_REFLECT_END()

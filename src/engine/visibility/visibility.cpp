#include "visibility.h"

#include "logger.h"
#include "thread_pool.h"

#include "resource_manager.h"
#include "scene.h"
#include "mesh.h"
#include "camera.h"
#include "transform.h"

#include "bounds_utils.h"
#include "visibility_context.h"

#include "frustum_culler.h"
#include "screen_size_culling.h"
#include "distance_culling.h"
#include "occlusion_culler.h"

namespace Engine {

namespace {

/**
 * @brief Type alias for a pair of an entity ID and a model matrix.
 */
using VisiblePair = std::pair<EntityId, glm::mat4>;

/**
 * @brief Find the first active camera and fill view, projection, and camera position.
 * @return true if an active camera was found, false otherwise.
 */
bool computeViewContext(
    glm::mat4& view,
    glm::mat4& projection,
    glm::vec3& cameraPosition,
    const Scene& scene
) {
    const auto& cameraStorage = scene.storage<Camera>();
    const auto& transformStorage = scene.storage<Transform>();

    for (EntityId id = 0; id < cameraStorage.size(); ++id) {
        if (!cameraStorage.has(id)) continue;

        const auto& camera = cameraStorage.get(id);
        if (!camera.active) continue;

        if (!transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);

        projection = Camera::computeProjection(camera);
        view = Transform::computeView(transform);
        cameraPosition = transform.position;
        return true;
    }

    return false;
}

/**
 * @brief Process a range of entities and return the visible pairs.
 * @param scene The scene to process.
 * @param resources The resource manager to use.
 * @param context The visibility context to use.
 * @param start The start index.
 * @param end The end index.
 * @return The visible pairs.
 */
std::vector<VisiblePair> processRange(
    Scene& scene,
    const ResourceManager& resources,
    const VisibilityContext& context,
    size_t start,
    size_t end
) {
    auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    std::vector<VisiblePair> result;
    result.reserve(end - start);

    for (EntityId id = start; id < end; ++id) {
        if (!meshStorage.has(id)) continue;
        if (!transformStorage.has(id)) continue;

        Mesh& mesh = meshStorage.get(id);

        if (!mesh.visible) continue;

        const auto& transform = transformStorage.get(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) continue;

        const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);

        localToWorldAABB(
            modelMatrix,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            mesh.boundsMin,
            mesh.boundsMax
        );

        if (!FrustumCuller::isVisible(mesh, context)) continue;
        if (!DistanceCuller::isVisible(mesh, context)) continue;
        if (!ScreenSizeCuller::isVisible(mesh, context)) continue;

        result.push_back(VisiblePair{id, modelMatrix});
    }

    return result;
}

} // anonymous

Visibility buildVisibility(
    Scene& scene,
    const ResourceManager& resources,
    uint32_t viewportWidth,
    uint32_t viewportHeight
) {
    Visibility result;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;

    if (!computeViewContext(view, projection, cameraPosition, scene)) {
        LOG_ERROR("No active camera found for visibility");
        return result;
    }

    const glm::mat4 viewProjection = projection * view;

    auto& meshStorage            = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    auto& threadPool = ThreadPool::get();

    const size_t numThreads = threadPool.size();
    const size_t chunkSize = (meshStorage.size() + numThreads - 1) / numThreads;

    // TODO: Move this to a config
    VisibilityContext context{
        .frustum        = extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .projection     = projection,

        .viewportWidth  = viewportWidth,
        .viewportHeight = viewportHeight,
        .minPixels      = 3.0f,
        .maxDistance    = 500.0f
    };

    std::vector<std::future<std::vector<VisiblePair>>> futures;
    futures.reserve(numThreads);

    for (size_t start = 0; start < meshStorage.size(); start += chunkSize) {
        const size_t end = std::min(start + chunkSize, meshStorage.size());

        futures.push_back(
            threadPool.push([&scene, &resources, &context, start, end]() {
                return processRange(scene, resources, context, start, end);
            })
        );
    }

    result.entities.reserve(meshStorage.size());
    result.modelMatrices.reserve(meshStorage.size());

    for (auto& f : futures) {
        const auto& chunk = f.get();
        for (const auto& p : chunk) {
            result.entities.push_back(p.first);
            result.modelMatrices.push_back(p.second);
        }
    }

    return result;
}

} // namespace Engine

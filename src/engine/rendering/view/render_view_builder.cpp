#include "render_view_builder.h"

#include <algorithm>
#include <future>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"
#include "thread_pool.h"

#include "scene.h"
#include "transform.h"
#include "camera.h"
#include "mesh.h"
#include "light.h"
#include "resource_manager.h"

namespace Engine {

namespace {

    /**
     * @brief Setup camera data in the render view.
     * @return true if camera was found and setup, false otherwise.
     */
    bool setupCamera(CameraData& cameraData, const Scene& scene) {
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        for (EntityId id = 0; id < cameraStorage.size(); ++id) {
            if (!cameraStorage.has(id)) continue;

            const auto& camera = cameraStorage.get(id);
            if (!camera.active) continue;
            if (!transformStorage.has(id)) continue;

            const auto& transform = transformStorage.get(id);

            cameraData.position = transform.position;
            const glm::vec3 forward = Transform::computeForward(transform.rotation);
            const glm::vec3 up = Transform::computeUp(transform.rotation);

            cameraData.view = glm::lookAt(
                cameraData.position,
                cameraData.position + forward,
                up
            );
            cameraData.projection = Camera::computeProjection(camera);
            cameraData.viewProjection = cameraData.projection * cameraData.view;

            return true;
        }
        return false;
    }

    /**
     * @brief Check if a mesh should be culled based on screen-space size.
     * @return true if the mesh should be culled (too small), false otherwise.
     */
    bool shouldCullByScreenSize(
        const MeshAsset& meshAsset,
        const Transform& transform,
        const glm::vec3& cameraPosition
    ) {
        const glm::vec3 diff = transform.position - cameraPosition;
        const float distSq = glm::dot(diff, diff);

        // Skip culling for very close objects
        if (distSq <= 1.0f) {
            return false;
        }

        // Estimate screen size from mesh bounds
        const glm::vec3 extents = (meshAsset.boundsMax - meshAsset.boundsMin) * 0.5f;
        const float maxScale = std::max({transform.scale.x, transform.scale.y, transform.scale.z});
        const float radius = std::max({extents.x, extents.y, extents.z}) * maxScale;

        // Cull if projected screen size is below threshold
        const float projectedSizeSq = (radius * radius) / distSq;
        return projectedSizeSq < MIN_SCREEN_SIZE_SQ;
    }

    /**
     * @brief Build a single drawable from an entity ID.
     * @return Optional DrawableData if the entity is drawable, empty otherwise.
     */
    std::optional<DrawableData> buildDrawable(
        EntityId id,
        const Scene& scene,
        const ResourceManager& resources,
        const glm::vec3& cameraPosition
    ) {
        const auto& meshStorage = scene.storage<Mesh>();
        const auto& transformStorage = scene.storage<Transform>();

        if (!meshStorage.has(id) || !transformStorage.has(id)) {
            return std::nullopt;
        }

        const auto& mesh = meshStorage.get(id);
        if (!mesh.visible) {
            return std::nullopt;
        }

        const auto& transform = transformStorage.get(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        // Perform screen-space culling
        if (shouldCullByScreenSize(meshAsset, transform, cameraPosition)) {
            return std::nullopt;
        }

        // Calculate distance for LOD selection
        const glm::vec3 diff = transform.position - cameraPosition;
        const float distSq = glm::dot(diff, diff);

        // Select appropriate mesh based on LOD
        const MeshHandle selectedMesh = mesh.hasLOD() 
            ? mesh.getMeshForDistance(distSq) 
            : mesh.mesh;

        return DrawableData{
            selectedMesh,
            mesh.material,
            Transform::computeModelMatrix(transform)
        };
    }

    /**
     * @brief Build drawables from visible entity IDs using parallel processing.
     * @return Vector of drawable data sorted by material.
     */
    std::vector<DrawableData> buildDrawables(
        const std::vector<EntityId>& visibleIds,
        const Scene& scene,
        const ResourceManager& resources,
        const glm::vec3& cameraPosition
    ) {
        constexpr size_t MAX_CHUNKS = 8;
        const size_t visibleCount = visibleIds.size();

        if (visibleCount == 0) {
            return {};
        }

        std::vector<DrawableData> drawables;
        const size_t chunkSize = (visibleCount + MAX_CHUNKS - 1) / MAX_CHUNKS;
        std::vector<std::future<std::vector<DrawableData>>> futures;

        // Submit work chunks to thread pool
        for (size_t start = 0; start < visibleCount; start += chunkSize) {
            const size_t end = std::min(start + chunkSize, visibleCount);
            futures.push_back(ThreadPool::get().push([&, start, end]() {
                std::vector<DrawableData> chunk;
                chunk.reserve(end - start);
                for (size_t i = start; i < end; ++i) {
                    if (auto drawable = buildDrawable(visibleIds[i], scene, resources, cameraPosition)) {
                        chunk.push_back(*drawable);
                    }
                }
                return chunk;
            }));
        }

        // Collect results from all chunks
        for (auto& future : futures) {
            auto chunk = future.get();
            drawables.insert(
                drawables.end(),
                std::make_move_iterator(chunk.begin()),
                std::make_move_iterator(chunk.end())
            );
        }

        // Sort by material to minimize state changes during rendering
        std::sort(drawables.begin(), drawables.end(),
            [](const DrawableData& a, const DrawableData& b) {
                return a.material.value < b.material.value;
            });

        return drawables;
    }

    /**
     * @brief Gather all enabled lights from the scene.
     * @return Vector of light data.
     */
    std::vector<LightData> gatherLights(const Scene& scene) {
        const auto& lightStorage = scene.storage<Light>();
        const auto& transformStorage = scene.storage<Transform>();

        std::vector<LightData> lights;
        lights.reserve(lightStorage.size());

        for (EntityId id = 0; id < lightStorage.size(); ++id) {
            if (!lightStorage.has(id)) continue;

            const auto& light = lightStorage.get(id);

            if (!light.enabled) continue;
            if (!transformStorage.has(id)) continue;

            const auto& transform = transformStorage.get(id);

            lights.push_back({
                light.type,
                light.color,
                light.intensity,
                light.radius,
                light.innerConeAngle,
                light.outerConeAngle,
                light.castShadows,
                transform.position,
                transform.rotation
            });
        }

        return lights;
    }

} // anonymous namespace

RenderView RenderViewBuilder::build(
    const Scene& scene,
    const ResourceManager& resources,
    const std::vector<EntityId>& visibleIds
) {
    RenderView renderView;

    if (!setupCamera(renderView.camera, scene)) {
        LOG_ERROR("No active camera found");
        return renderView;
    }

    renderView.drawables = buildDrawables(
        visibleIds,
        scene,
        resources,
        renderView.camera.position
    );

    renderView.lights = gatherLights(scene);

    return renderView;
}

} // namespace Engine

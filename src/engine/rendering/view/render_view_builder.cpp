#include "render_view_builder.h"

#include <algorithm>
#include <future>
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
#include "spatial_index.h"

namespace Engine {

namespace {
    /**
     * @brief Helper function to setup camera data in the render view.
     * @return true if camera was found and setup, false otherwise.
     */
    bool setupCamera(CameraData& cameraData, const Scene& scene) {
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        // Find the active camera using dense iteration
        for (EntityId id : cameraStorage.entities()) {
            const auto& camera = cameraStorage.get(id);
            if (!camera.active) continue;
            if (!transformStorage.has(id)) continue;

            const auto& transform = transformStorage.get(id);

            // Compute camera matrices
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
}

RenderView RenderViewBuilder::build(
    const Scene& scene,
    const ResourceManager& resources,
    const std::vector<uint32_t>& visibleIds
) {
    RenderView renderView;

    if (!setupCamera(renderView.camera, scene)) {
        LOG_ERROR("No active camera found");
        return renderView;
    }

    const auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();
    const glm::vec3 cameraPos = renderView.camera.position;

    // Screen-size culling: skip objects smaller than ~2 pixels at 1080p
    constexpr float MIN_SCREEN_SIZE_SQ = 0.002f * 0.002f;

    auto buildDrawable = [&](uint32_t id, std::vector<DrawableData>& out) {
        if (!meshStorage.has(id) || !transformStorage.has(id)) return;

        const auto& mesh = meshStorage.get(id);
        if (!mesh.visible) return;

        const auto& transform = transformStorage.get(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        const glm::vec3 diff = transform.position - cameraPos;
        const float distSq = glm::dot(diff, diff);

        // Estimate screen size from mesh bounds
        const glm::vec3 extents = (meshAsset.boundsMax - meshAsset.boundsMin) * 0.5f;
        const float maxScale = std::max({transform.scale.x, transform.scale.y, transform.scale.z});
        const float radius = std::max({extents.x, extents.y, extents.z}) * maxScale;

        if (distSq > 1.0f && (radius * radius) / distSq < MIN_SCREEN_SIZE_SQ) {
            return;
        }

        out.push_back(DrawableData{
            mesh.hasLOD() ? mesh.getMeshForDistance(distSq) : mesh.mesh,
            mesh.material,
            Transform::computeModelMatrix(transform)
        });
    };

    // Parallel processing for large visible counts
    constexpr size_t PARALLEL_THRESHOLD = 1000;
    constexpr size_t MAX_CHUNKS = 8;
    const size_t visibleCount = visibleIds.size();

    if (visibleCount > PARALLEL_THRESHOLD) {
        const size_t chunkSize = (visibleCount + MAX_CHUNKS - 1) / MAX_CHUNKS;
        std::vector<std::future<std::vector<DrawableData>>> futures;

        for (size_t start = 0; start < visibleCount; start += chunkSize) {
            const size_t end = std::min(start + chunkSize, visibleCount);
            futures.push_back(ThreadPool::get().push([&, start, end]() {
                std::vector<DrawableData> local;
                local.reserve(end - start);
                for (size_t i = start; i < end; ++i) {
                    buildDrawable(visibleIds[i], local);
                }
                return local;
            }));
        }

        for (auto& f : futures) {
            auto chunk = f.get();
            renderView.drawables.insert(
                renderView.drawables.end(),
                std::make_move_iterator(chunk.begin()),
                std::make_move_iterator(chunk.end())
            );
        }
    } else {
        renderView.drawables.reserve(visibleCount);
        for (uint32_t id : visibleIds) {
            buildDrawable(id, renderView.drawables);
        }
    }

    // Sort by material to minimize state changes
    std::sort(renderView.drawables.begin(), renderView.drawables.end(),
        [](const DrawableData& a, const DrawableData& b) {
            return a.material.value < b.material.value;
        });

    // Gather lights
    const auto& lightStorage = scene.storage<Light>();
    renderView.lights.reserve(lightStorage.count());

    for (EntityId id : lightStorage.entities()) {
        const auto& light = lightStorage.get(id);
        if (!light.enabled || !transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);
        renderView.lights.push_back({
            light.type, light.color, light.intensity, light.radius,
            light.innerConeAngle, light.outerConeAngle, light.castShadows,
            transform.position, transform.rotation
        });
    }

    return renderView;
}

} // namespace Engine
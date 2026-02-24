#include "gl_view.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "logger.h"

#include "config/gl_config.h"
#include "config/gl_texture_mapping.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_texture.h"
#include "resource/texture_asset.h"

namespace Engine {

GLView::~GLView() {
    m_meshes.clear();
    m_materials.clear();
    m_textures.clear();

    LOG_TRACE("Destroying GLView");
}

void GLView::syncMeshes(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Skip sync entirely when no resources changed and drawable count is stable
    const uint64_t currentVersion = resourceManager.getGlobalVersion();
    const size_t currentCount = renderView.drawables.size();
    if (currentVersion == m_lastSyncVersion && currentCount == m_lastDrawableCount) {
        return;
    }

    // Collect unique mesh handles via sort+unique (cache-friendly on small arrays,
    // outperforms hash-based dedup for typical scene sizes due to sequential access)
    thread_local std::vector<MeshHandle> meshHandles;
    meshHandles.clear();
    meshHandles.reserve(renderView.drawables.size());

    for (const auto& drawable : renderView.drawables) {
        if (drawable.mesh) {
            meshHandles.push_back(drawable.mesh);
        }
    }

    std::sort(meshHandles.begin(), meshHandles.end(),
        [](const MeshHandle& a, const MeshHandle& b) { return a.id() < b.id(); });
    meshHandles.erase(std::unique(meshHandles.begin(), meshHandles.end(),
        [](const MeshHandle& a, const MeshHandle& b) { return a.id() == b.id(); }), meshHandles.end());

    for (const auto& handle : meshHandles) {
        const uint32_t key = handle.id();
        const auto& asset = resourceManager.get(handle);
        const uint64_t version = asset.version;

        auto it = m_meshes.find(key);

        if (it == m_meshes.end()) {
            m_meshes[key] = std::make_unique<GLMesh>(asset);
            m_meshVersions[key] = version;
        } else if (m_meshVersions[key] != version) {
            it->second->update(asset);
            m_meshVersions[key] = version;
        }
    }

    m_lastSyncVersion = currentVersion;
    m_lastDrawableCount = currentCount;
}

void GLView::syncMaterials(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Drawables are sorted by material, so we can skip consecutive duplicates
    uint32_t lastMaterial = UINT32_MAX;

    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        const uint32_t key = drawable.material.id();
        if (key == lastMaterial) continue;
        lastMaterial = key;

        const auto& asset = resourceManager.get(drawable.material);
        const uint64_t version = asset.version;

        auto it = m_materials.find(key);

        if (it == m_materials.end()) {
            m_materials[key] = std::make_unique<GLMaterial>(asset);
            m_materialVersions[key] = version;
        } else if (m_materialVersions[key] != version) {
            it->second->update(asset);
            m_materialVersions[key] = version;
        }
    }
}

void GLView::syncTextures(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Collect unique texture handles (full handles for resourceManager lookup)
    thread_local std::vector<TextureHandle> textureHandles;
    textureHandles.clear();

    uint32_t lastMaterial = UINT32_MAX;  // Fast path for sorted drawables

    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        // Drawables are sorted by material - skip if same as last
        if (drawable.material.id() == lastMaterial) continue;
        lastMaterial = drawable.material.id();

        const auto& material = resourceManager.get(drawable.material);

        for (const auto& mapping : g_textureMappings) {
            const TextureHandle& handle = material.*mapping.handlePtr;
            if (handle) {
                textureHandles.push_back(handle);
            }
        }
    }

    // Sort by id and remove duplicates
    std::sort(textureHandles.begin(), textureHandles.end(),
        [](const TextureHandle& a, const TextureHandle& b) { return a.id() < b.id(); });
    textureHandles.erase(std::unique(textureHandles.begin(), textureHandles.end(),
        [](const TextureHandle& a, const TextureHandle& b) { return a.id() == b.id(); }), textureHandles.end());

    // Sync each referenced texture
    for (const auto& handle : textureHandles) {
        const uint32_t key = handle.id();
        const auto& asset = resourceManager.get(handle);
        const uint64_t version = asset.version;

        auto it = m_textures.find(key);

        if (it == m_textures.end()) {
            m_textures[key] = std::make_unique<GLTexture>(asset);
            m_textureVersions[key] = version;

        } else if (m_textureVersions[key] != version) {
            it->second->update(asset);
            m_textureVersions[key] = version;
        }
    }
}

void GLView::syncLights(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    m_lights.update(renderView.lights);
}

const GLMesh* GLView::getMesh(const MeshHandle& handle) const {
    auto it = m_meshes.find(handle.id());

    if (it == m_meshes.end() || !it->second) {
        LOG_WARNING("GLMesh not found for handle %u (not synced or invalid)", handle.id());
        return nullptr;
    }

    return it->second.get();
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    auto it = m_materials.find(handle.id());

    if (it == m_materials.end() || !it->second) {
        LOG_WARNING("GLMaterial not found for handle %u (not synced or invalid)", handle.id());
        return nullptr;
    }

    return it->second.get();
}

const GLTexture* GLView::getTexture(const TextureHandle& handle) const {
    auto it = m_textures.find(handle.id());

    if (it == m_textures.end() || !it->second) {
        LOG_WARNING("GLTexture not found for handle %u (not synced or invalid)", handle.id());
        return nullptr;
    }

    return it->second.get();
}

void GLView::buildInstanceBatches(const RenderView& renderView) {
    m_instanceBatcher.build(renderView.drawables);
}

GLMesh* GLView::getMutableMesh(const MeshHandle& handle) {
    auto it = m_meshes.find(handle.id());

    if (it == m_meshes.end() || !it->second) {
        return nullptr;
    }

    return it->second.get();
}

void GLView::purgeStaleResources(const RenderView& renderView) {
    // Collect active mesh and material IDs from this frame's drawables
    std::unordered_set<uint32_t> activeMeshes;
    std::unordered_set<uint32_t> activeMaterials;
    std::unordered_set<uint32_t> activeTextures;

    for (const auto& drawable : renderView.drawables) {
        if (drawable.mesh) activeMeshes.insert(drawable.mesh.id());
        if (drawable.material) activeMaterials.insert(drawable.material.id());
    }

    // Collect active texture IDs from active materials
    // (textures referenced by materials currently in use)
    for (const auto& [key, mat] : m_materials) {
        if (activeMaterials.count(key)) {
            for (const auto& binding : mat->getTextureBindings()) {
                if (binding.handle) activeTextures.insert(binding.handle.id());
            }
        }
    }

    // Erase meshes not in current frame
    for (auto it = m_meshes.begin(); it != m_meshes.end(); ) {
        if (!activeMeshes.count(it->first)) {
            m_meshVersions.erase(it->first);
            it = m_meshes.erase(it);
        } else {
            ++it;
        }
    }

    // Erase materials not in current frame
    for (auto it = m_materials.begin(); it != m_materials.end(); ) {
        if (!activeMaterials.count(it->first)) {
            m_materialVersions.erase(it->first);
            it = m_materials.erase(it);
        } else {
            ++it;
        }
    }

    // Erase textures not referenced by any active material
    for (auto it = m_textures.begin(); it != m_textures.end(); ) {
        if (!activeTextures.count(it->first)) {
            m_textureVersions.erase(it->first);
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }
}

void GLView::purgeStaleIfNeeded(const RenderView& renderView) {
    if (++m_purgeCounter >= PURGE_INTERVAL) {
        purgeStaleResources(renderView);
        m_purgeCounter = 0;
    }
}

} // namespace Engine

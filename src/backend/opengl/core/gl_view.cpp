#include "gl_view.h"

#include <algorithm>
#include <vector>

#include "logger.h"

#include "gl_config.h"
#include "gl_texture_mapping.h"
#include "resource_manager.h"
#include "render_view.h"

#include "gl_mesh.h"
#include "gl_material.h"
#include "gl_texture.h"
#include "texture_asset.h"

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
    // Collect unique mesh handles
    thread_local std::vector<uint32_t> meshHandles;
    meshHandles.clear();
    meshHandles.reserve(renderView.drawables.size());

    for (const auto& drawable : renderView.drawables) {
        if (drawable.mesh) {
            meshHandles.push_back(drawable.mesh.value);
        }
    }

    std::sort(meshHandles.begin(), meshHandles.end());
    meshHandles.erase(std::unique(meshHandles.begin(), meshHandles.end()), meshHandles.end());

    for (uint32_t key : meshHandles) {
        MeshHandle handle;
        handle.value = key;

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
}

void GLView::syncMaterials(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Drawables are sorted by material, so we can skip consecutive duplicates
    uint32_t lastMaterial = UINT32_MAX;

    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        const uint32_t key = drawable.material.value;
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
    // Track processed materials to avoid redundant work (many drawables share materials)
    thread_local std::vector<uint32_t> textureHandles;
    textureHandles.clear();

    uint32_t lastMaterial = UINT32_MAX;  // Fast path for sorted drawables

    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        // Drawables are sorted by material - skip if same as last
        if (drawable.material.value == lastMaterial) continue;
        lastMaterial = drawable.material.value;

        const auto& material = resourceManager.get(drawable.material);

        for (const auto& mapping : g_textureMappings) {
            const TextureHandle& handle = material.*mapping.handlePtr;
            if (handle.value != 0) {
                textureHandles.push_back(handle.value);
            }
        }
    }

    // Sort and remove duplicates
    std::sort(textureHandles.begin(), textureHandles.end());
    textureHandles.erase(std::unique(textureHandles.begin(), textureHandles.end()), textureHandles.end());

    // Sync each referenced texture
    for (uint32_t textureKey : textureHandles) {
        TextureHandle handle;
        handle.value = textureKey;

        const auto& asset = resourceManager.get(handle);
        const uint64_t version = asset.version;

        auto it = m_textures.find(textureKey);

        if (it == m_textures.end()) {
            m_textures[textureKey] = std::make_unique<GLTexture>(asset);
            m_textureVersions[textureKey] = version;

        } else if (m_textureVersions[textureKey] != version) {
            it->second->update(asset);
            m_textureVersions[textureKey] = version;
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
    auto it = m_meshes.find(handle.value);

    if (it == m_meshes.end() || !it->second) {
        LOG_WARNING("GLMesh not found for handle %s (not synced or invalid)", handle.value);
        return nullptr;
    }

    return it->second.get();
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    auto it = m_materials.find(handle.value);

    if (it == m_materials.end() || !it->second) {
        LOG_WARNING("GLMaterial not found for handle %s (not synced or invalid)", handle.value);
        return nullptr;
    }

    return it->second.get();
}

const GLTexture* GLView::getTexture(const TextureHandle& handle) const {
    auto it = m_textures.find(handle.value);

    if (it == m_textures.end() || !it->second) {
        LOG_WARNING("GLTexture not found for handle %s (not synced or invalid)", handle.value);
        return nullptr;
    }

    return it->second.get();
}

void GLView::buildInstanceBatches(const RenderView& renderView) {
    m_instanceBatcher.build(renderView.drawables);
}

GLMesh* GLView::getMutableMesh(const MeshHandle& handle) {
    auto it = m_meshes.find(handle.value);

    if (it == m_meshes.end() || !it->second) {
        return nullptr;
    }

    return it->second.get();
}

} // namespace Engine

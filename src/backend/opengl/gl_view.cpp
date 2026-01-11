#include "gl_view.h"

#include <unordered_set>

#include "logger.h"

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
    for (const auto& drawable : renderView.drawables) {
        // Fetch the mesh asset from the resource manager
        const uint32_t key     = drawable.mesh.value;
        const auto& asset      = resourceManager.get(drawable.mesh);
        const uint64_t version = asset.version;

        // Try to find an existing GLMesh mapped to this handle
        auto it = m_meshes.find(key);

        if (it == m_meshes.end()) {
            // No GLMesh for this mesh: create it from the asset and track its version
            m_meshes[key] = std::make_unique<GLMesh>(asset);
            m_meshVersions[key] = version;

        } else if (m_meshVersions[key] != version) {
            // The mesh asset has changed since last sync: update the GLMesh on GPU
            it->second->update(asset);
            m_meshVersions[key] = version;
        }
        // If mesh exists and version matches, nothing needs to be done
    }
}

void GLView::syncMaterials(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    for (const auto& drawable : renderView.drawables) {
        // Skip non-visible instances
        // Fetch the material asset from the resource manager
        const uint32_t key     = drawable.material.value;
        const auto& asset      = resourceManager.get(drawable.material);
        const uint64_t version = asset.version;

        // Try to find an existing GLMaterial mapped to this handle
        auto it = m_materials.find(key);

        if (it == m_materials.end()) {
            // No GLMaterial for this material: create it from the asset and track its version
            m_materials[key] = std::make_unique<GLMaterial>(asset);
            m_materialVersions[key] = version;

        } else if (m_materialVersions[key] != version) {
            // The material asset has changed since last sync: update the GLMaterial
            it->second->update(asset);
            m_materialVersions[key] = version;
        }
        // If material exists and version matches, nothing needs to be done
    }
}

const GLMesh& GLView::getMesh(const MeshHandle& handle) const {
    auto it = m_meshes.find(handle.value);

    // TODO: Handle this case
    if (it == m_meshes.end() || !it->second) {
        LOG_ERROR("Mesh not synced");
    }

    return *it->second;
}

const GLMaterial& GLView::getMaterial(const MaterialHandle& handle) const {
    auto it = m_materials.find(handle.value);

    // TODO: Handle this case
    if (it == m_materials.end() || !it->second) {
        LOG_ERROR("Material not synced");
    }

    return *it->second;
}

// Forward declare texture mapping table from gl_material.cpp
namespace {
    struct TextureMappingForSync {
        TextureHandle MaterialAsset::*handlePtr;
    };

    // Simplified table for texture syncing (only need handle pointers)
    constexpr TextureMappingForSync g_textureSyncMappings[] = {
        {&MaterialAsset::albedoTexture},
        {&MaterialAsset::normalTexture},
        {&MaterialAsset::metallicRoughnessTexture},
        {&MaterialAsset::metallicTexture},
        {&MaterialAsset::roughnessTexture},
        {&MaterialAsset::aoTexture},
        {&MaterialAsset::aoMetallicRoughnessTexture},
        {&MaterialAsset::emissionTexture},
        {&MaterialAsset::heightTexture},
        {&MaterialAsset::clearcoatTexture},
        {&MaterialAsset::transmissionTexture},
    };
}

void GLView::syncTextures(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Collect all texture handles from materials in the render view
    std::unordered_set<uint32_t> textureHandles;

    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        const auto& material = resourceManager.get(drawable.material);

        // Use texture mapping table to collect all texture handles
        for (const auto& mapping : g_textureSyncMappings) {
            const TextureHandle& handle = material.*mapping.handlePtr;
            if (handle.value != 0) {
                textureHandles.insert(handle.value);
            }
        }
    }

    // Sync each texture
    for (uint32_t textureKey : textureHandles) {
        TextureHandle handle;
        handle.value = textureKey;

        const auto& asset = resourceManager.get(handle);
        const uint64_t version = asset.version;

        auto it = m_textures.find(textureKey);

        if (it == m_textures.end()) {
            // No GLTexture for this texture: create it from the asset and track its version
            m_textures[textureKey] = std::make_unique<GLTexture>(asset);
            m_textureVersions[textureKey] = version;

        } else if (m_textureVersions[textureKey] != version) {
            // The texture asset has changed since last sync: update the GLTexture
            it->second->update(asset);
            m_textureVersions[textureKey] = version;
        }
        // If texture exists and version matches, nothing needs to be done
    }
}

void GLView::syncLights(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Update lights UBO from render view
    m_lights.update(renderView.lights);
}

const GLTexture& GLView::getTexture(const TextureHandle& handle) const {
    auto it = m_textures.find(handle.value);

    // TODO: Handle this case
    if (it == m_textures.end() || !it->second) {
        LOG_ERROR("Texture not synced");
    }

    return *it->second;
}

} // namespace Engine
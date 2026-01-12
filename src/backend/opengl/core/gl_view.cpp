#include "gl_view.h"

#include <unordered_set>

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
    for (const auto& drawable : renderView.drawables) {
        if (!drawable.mesh) continue;

        const uint32_t key = drawable.mesh.value;
        const auto& asset = resourceManager.get(drawable.mesh);
        const uint64_t version = asset.version;

        auto it = m_meshes.find(key);

        if (it == m_meshes.end()) {
            // Create new mesh
            m_meshes[key] = std::make_unique<GLMesh>(asset);
            m_meshVersions[key] = version;

        } else if (m_meshVersions[key] != version) {
            // Update existing mesh if version changed
            it->second->update(asset);
            m_meshVersions[key] = version;
        }
    }
}

void GLView::syncMaterials(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        const uint32_t key = drawable.material.value;
        const auto& asset = resourceManager.get(drawable.material);
        const uint64_t version = asset.version;

        auto it = m_materials.find(key);

        if (it == m_materials.end()) {
            // Create new material
            m_materials[key] = std::make_unique<GLMaterial>(asset);
            m_materialVersions[key] = version;

        } else if (m_materialVersions[key] != version) {
            // Update existing material if version changed
            it->second->update(asset);
            m_materialVersions[key] = version;
        }
    }
}

void GLView::syncTextures(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    // Collect all unique texture handles referenced by materials
    std::unordered_set<uint32_t> textureHandles;
    textureHandles.reserve(renderView.drawables.size() * 3);  // Estimate: ~3 textures per material

    for (const auto& drawable : renderView.drawables) {
        if (!drawable.material) continue;

        const auto& material = resourceManager.get(drawable.material);

        // Use centralized texture mapping table
        for (const auto& mapping : g_textureMappings) {
            const TextureHandle& handle = material.*mapping.handlePtr;
            if (handle.value != 0) {
                textureHandles.insert(handle.value);
            }
        }
    }

    // Sync each referenced texture
    for (uint32_t textureKey : textureHandles) {
        TextureHandle handle;
        handle.value = textureKey;

        const auto& asset = resourceManager.get(handle);
        const uint64_t version = asset.version;

        auto it = m_textures.find(textureKey);

        if (it == m_textures.end()) {
            // Create new texture
            m_textures[textureKey] = std::make_unique<GLTexture>(asset);
            m_textureVersions[textureKey] = version;

        } else if (m_textureVersions[textureKey] != version) {
            // Update existing texture if version changed
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

} // namespace Engine

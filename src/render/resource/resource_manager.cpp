#include "resource_manager.h"

namespace Engine {

static void ensure(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(msg);
}

size_t ResourceManager::idx(const MeshHandle& handle) {
    ensure(handle.value != 0, "Invalid MeshHandle");
    return static_cast<size_t>(handle.value - 1);
}
size_t ResourceManager::idx(const TextureHandle& handle) {
    ensure(handle.value != 0, "Invalid TextureHandle");
    return static_cast<size_t>(handle.value - 1);
}
size_t ResourceManager::idx(const MaterialHandle& handle) {
    ensure(handle.value != 0, "Invalid MaterialHandle");
    return static_cast<size_t>(handle.value - 1);
}

MeshHandle ResourceManager::addMesh(const MeshAsset& mesh) {
    m_meshes.push_back(std::move(mesh));
    return MeshHandle{ static_cast<uint32_t>(m_meshes.size()) }; // 1-based
}
TextureHandle ResourceManager::addTexture(const TextureAsset& tex) {
    m_textures.push_back(std::move(tex));
    return TextureHandle{ static_cast<uint32_t>(m_textures.size()) };
}
MaterialHandle ResourceManager::addMaterial(const MaterialAsset& mat) {
    m_materials.push_back(std::move(mat));
    return MaterialHandle{ static_cast<uint32_t>(m_materials.size()) };
}

const MeshAsset& ResourceManager::getMesh(const MeshHandle& handle) const { return m_meshes[idx(handle)]; }
const TextureAsset& ResourceManager::getTexture(const TextureHandle& handle) const { return m_textures[idx(handle)]; }
const MaterialAsset& ResourceManager::getMaterial(const MaterialHandle& handle) const { return m_materials[idx(handle)]; }

MeshAsset& ResourceManager::editMesh(const MeshHandle& handle) { return m_meshes[idx(handle)]; }
TextureAsset& ResourceManager::editTexture(const TextureHandle& handle) { return m_textures[idx(handle)]; }
MaterialAsset& ResourceManager::editMaterial(const MaterialHandle& handle) { return m_materials[idx(handle)]; }

void ResourceManager::commitMesh(const MeshHandle& handle) { ++m_meshes[idx(handle)].version; }
void ResourceManager::commitTexture(const TextureHandle& handle) { ++m_textures[idx(handle)].version; }
void ResourceManager::commitMaterial(const MaterialHandle& handle) { ++m_materials[idx(handle)].version; }

} // namespace Engine
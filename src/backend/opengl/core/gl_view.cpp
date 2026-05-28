#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_view.h"

#include <algorithm>
#include <type_traits>
#include <vector>

#include "logger.h"

#include "texture/gl_texture.h"  // vkmGL Core::Texture2D for the fallback placeholder

#include "debug/profiler.h"
#include "debug/shader_error_log.h"
#include "config/gl_texture_mapping.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_texture.h"
#include "resource/shader_asset.h"
#include "resource/texture_asset.h"

namespace Engine {

namespace {

template<typename HandleT>
void sortUnique(std::vector<HandleT>& handles) {
    std::sort(handles.begin(), handles.end(),
        [](const HandleT& a, const HandleT& b) { return a.id() < b.id(); });
    handles.erase(std::unique(handles.begin(), handles.end(),
        [](const HandleT& a, const HandleT& b) { return a.id() == b.id(); }), handles.end());
}

void collectMeshHandles(const RenderView& view, std::vector<MeshHandle>& out) {
    out.clear();
    out.reserve(view.drawables.size());
    for (const auto& d : view.drawables) {
        if (d.mesh) out.push_back(d.mesh);
    }
    sortUnique(out);
}

void collectMaterialHandles(const RenderView& view, std::vector<MaterialHandle>& out) {
    out.clear();
    uint32_t lastId = UINT32_MAX;
    for (const auto& d : view.drawables) {
        if (!d.material) continue;
        if (d.material.id() == lastId) continue;  // sorted: skip consecutive duplicates
        lastId = d.material.id();
        out.push_back(d.material);
    }
    sortUnique(out);
}

void collectTextureHandles(
    const std::vector<MaterialHandle>& materialHandles,
    const ResourceManager& resources,
    std::vector<TextureHandle>& out
) {
    out.clear();
    for (const auto& mh : materialHandles) {
        const auto& material = resources.get(mh);
        for (const auto& mapping : g_textureMappings) {
            const TextureHandle& th = material.*mapping.handlePtr;
            if (th) out.push_back(th);
        }
    }
    sortUnique(out);
}

} // namespace

GLView::~GLView() {
    LOG_TRACE("Destructed GLView");
}

template<typename AssetT, typename GLT>
void GLView::syncTable(
    GLResourceTable<GLT>& table,
    const std::vector<Handle<AssetT>>& handles,
    const ResourceManager& resources
) {
    for (const auto& h : handles) {
        const uint32_t id = h.id();
        const auto& asset = resources.get(h);
        const uint64_t v = asset.version;

        // Textures still being decoded asynchronously have no pixel data yet;
        // skip the GL upload entirely so we don't allocate an empty texture
        // that draws as undefined contents. The async loader bumps the
        // version once data arrives, and the next sync picks it up.
        if constexpr (std::is_same_v<AssetT, TextureAsset>) {
            if (asset.loading) continue;
        }

        if (id >= table.entries.size()) {
            table.entries.resize(id + 1);
            table.versions.resize(id + 1, 0);
        }

        if (!table.entries[id]) {
            table.entries[id] = std::make_unique<GLT>(asset);
            table.versions[id] = v;
        } else if (table.versions[id] != v) {
            table.entries[id]->update(asset);
            table.versions[id] = v;
        }
    }
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    PROFILE_SCOPE("GLView::sync");

    // A scene load swaps the whole ResourceManager. Cached GL entries keyed
    // by handle id are now stale: a new asset may sit at an id that previously
    // held a different asset with a coincidentally-equal per-asset version,
    // so the version-skip would keep the wrong GL resource alive. Detect via
    // the bumped global version and drop every cache.
    const uint64_t globalVersion = resources.getGlobalVersion();
    if (globalVersion != m_lastGlobalVersion) {
        m_meshTable     = {};
        m_materialTable = {};
        m_textureTable  = {};
        m_shaderTable   = {};

        m_lastMeshTypeVersion     = 0;
        m_lastMaterialTypeVersion = 0;
        m_lastTextureTypeVersion  = 0;
        m_lastDrawableCount       = 0;
        m_lastGlobalVersion       = globalVersion;
    }

    // Drawable-driven sync (meshes/materials/textures): early-out when neither
    // resource versions nor drawable count have changed.
    const uint64_t meshTypeVersion     = resources.getTypeVersion<MeshAsset>();
    const uint64_t materialTypeVersion = resources.getTypeVersion<MaterialAsset>();
    const uint64_t textureTypeVersion  = resources.getTypeVersion<TextureAsset>();
    const size_t drawableCount = view.drawables.size();

    const bool resourcesDirty =
           meshTypeVersion     != m_lastMeshTypeVersion
        || materialTypeVersion != m_lastMaterialTypeVersion
        || textureTypeVersion  != m_lastTextureTypeVersion
        || drawableCount       != m_lastDrawableCount;

    thread_local std::vector<MeshHandle>     meshHandles;
    thread_local std::vector<MaterialHandle> materialHandles;
    thread_local std::vector<TextureHandle>  textureHandles;

    if (resourcesDirty) {
        PROFILE_SCOPE("GLView/UploadResources");
        collectMeshHandles(view, meshHandles);
        collectMaterialHandles(view, materialHandles);
        collectTextureHandles(materialHandles, resources, textureHandles);

        syncTable<MeshAsset>    (m_meshTable,     meshHandles,     resources);
        syncTable<MaterialAsset>(m_materialTable, materialHandles, resources);
        syncTable<TextureAsset> (m_textureTable,  textureHandles,  resources);

        m_lastMeshTypeVersion     = meshTypeVersion;
        m_lastMaterialTypeVersion = materialTypeVersion;
        m_lastTextureTypeVersion  = textureTypeVersion;
        m_lastDrawableCount       = drawableCount;
    }

    // Per-frame UBOs owned by GLView. Shadow UBO is bound by GLShadowPass
    // after it populates entries - no need to bind here.
    m_camera.update(view.camera, view.environment);
    m_camera.bind();

    m_lights.update(view.lights);
    m_lights.bind();

    // Camera-visible batches for the forward/visible passes.
    m_instanceBatcher.build(view.drawables);
    // Full-scene shadow-caster batches (not camera-frustum culled) so the
    // shadow pass renders occluders that are off-screen but still cast in.
    m_shadowBatcher.build(view.shadowCasters);
}

const GLMesh* GLView::getMesh(const MeshHandle& handle) const {
    const uint32_t id = handle.id();
    if (id >= m_meshTable.entries.size() || !m_meshTable.entries[id]) {
        LOG_WARNING("GLMesh not found for handle %u (not synced or invalid)", id);
        return nullptr;
    }
    return m_meshTable.entries[id].get();
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    const uint32_t id = handle.id();
    if (id >= m_materialTable.entries.size() || !m_materialTable.entries[id]) {
        LOG_WARNING("GLMaterial not found for handle %u (not synced or invalid)", id);
        return nullptr;
    }
    return m_materialTable.entries[id].get();
}

const GLTexture* GLView::getTexture(const TextureHandle& handle) const {
    const uint32_t id = handle.id();
    if (id >= m_textureTable.entries.size() || !m_textureTable.entries[id]) {
        // No warning here: a missing entry is a normal transient state
        // when an async texture load is in flight. Callers that need a
        // bind site every frame should use getTextureOrFallback() and
        // get a 1x1 gray placeholder until the upload lands.
        return nullptr;
    }
    return m_textureTable.entries[id].get();
}

const Core::Texture2D* GLView::getTextureOrFallback(const TextureHandle& handle) const {
    const uint32_t id = handle.id();
    if (id < m_textureTable.entries.size() && m_textureTable.entries[id]) {
        return &m_textureTable.entries[id]->getTexture();
    }
    if (!m_fallbackTexture) {
        // 1x1 gray RGBA8. Cheap to upload, neutral against any shader
        // expectation - normal maps render flat (no detail), albedo
        // renders mid-gray (no colour cast), metallic/roughness end up
        // at sane mid values. Built once per GLView lifetime.
        static const uint8_t gray[4] = {128, 128, 128, 255};
        Core::Texture2DParams params;
        params.width           = 1;
        params.height          = 1;
        params.internalFormat  = GL_RGBA8;
        params.format          = GL_RGBA;
        params.type            = GL_UNSIGNED_BYTE;
        params.generateMipmaps = false;
        params.data            = gray;
        m_fallbackTexture = std::make_unique<Core::Texture2D>("async_fallback", params);
    }
    return m_fallbackTexture.get();
}

GLMesh* GLView::getMutableMesh(const MeshHandle& handle) {
    const uint32_t id = handle.id();
    if (id >= m_meshTable.entries.size()) return nullptr;
    return m_meshTable.entries[id].get();
}

// Shaders aren't referenced by entities, so we resolve them lazily - one
// call per pass per frame. syncTable handles the "build on first reference
// or rebuild on version bump" path; hot reload threads through here.
// The single-element scratch lists below are thread_local and reused (clear +
// push_back, capacity kept) instead of a fresh std::vector per call: these
// run multiple times per frame from nearly every pass, on the render thread.
// Same pattern as ensureMaterialTextures.
GLShader* GLView::resolveShader(const ShaderHandle& handle, const ResourceManager& resources) {
    if (!handle) return nullptr;
    thread_local std::vector<ShaderHandle> one;
    one.clear();
    one.push_back(handle);
    syncTable<ShaderAsset>(m_shaderTable, one, resources);
    return m_shaderTable.entries[handle.id()].get();
}

namespace {

/// Map a ShaderVariantKey to the #define tokens the PBR shader expects.
/// Order is irrelevant - the preprocessor emits one #define line per token.
std::vector<std::string> variantKeyToDefines(const GLView::ShaderVariantKey& key) {
    std::vector<std::string> defines;
    defines.push_back("MATERIAL_VARIANT");  // turns off the ubershader fallback block

    // Material features.
    const uint32_t flags = key.materialFlags;
    if (flags & toBits(MaterialFeature::Transmission)) defines.emplace_back("HAS_TRANSMISSION");
    if (flags & toBits(MaterialFeature::Volume))       defines.emplace_back("HAS_VOLUME");
    if (flags & toBits(MaterialFeature::Clearcoat))    defines.emplace_back("HAS_CLEARCOAT");
    if (flags & toBits(MaterialFeature::Anisotropy))   defines.emplace_back("HAS_ANISOTROPY");
    if (flags & toBits(MaterialFeature::Subsurface))   defines.emplace_back("HAS_SUBSURFACE");
    if (flags & toBits(MaterialFeature::Sheen))        defines.emplace_back("HAS_SHEEN");
    if (flags & toBits(MaterialFeature::Parallax))     defines.emplace_back("HAS_PARALLAX");
    if (flags & toBits(MaterialFeature::AlphaMask))    defines.emplace_back("HAS_ALPHA_MASK");

    // Light-count bucket (forward consumers can branch on these; current
    // PBR shader ignores them but the variant cache distinguishes the
    // entries so a future specialised path can land without touching
    // every call site).
    switch (key.lightCountBucket) {
        case 0: defines.emplace_back("LIGHT_BUCKET_NONE");   break;
        case 1: defines.emplace_back("LIGHT_BUCKET_SINGLE"); break;
        case 2: defines.emplace_back("LIGHT_BUCKET_FEW");    break;
        default: defines.emplace_back("LIGHT_BUCKET_MANY");  break;
    }

    // Shadow-kind mask. Bit layout matches ShaderVariantKey::shadowKindMask:
    // bit 0 = directional, bit 1 = point, bit 2 = spot.
    if (key.shadowKindMask & 0x1u) defines.emplace_back("HAS_DIRECTIONAL_SHADOWS");
    if (key.shadowKindMask & 0x2u) defines.emplace_back("HAS_POINT_SHADOWS");
    if (key.shadowKindMask & 0x4u) defines.emplace_back("HAS_SPOT_SHADOWS");

    // Weighted-Blended OIT output. The shader switches its FragColor
    // declaration to two MRT outputs (accum, revealage) when set.
    if (key.oitPass) defines.emplace_back("OIT_PASS");

    return defines;
}

} // namespace

GLShader* GLView::resolveShaderVariant(
    const ShaderHandle& handle,
    uint32_t featureFlags,
    const ResourceManager& resources)
{
    ShaderVariantKey key;
    key.materialFlags = featureFlags;
    return resolveShaderVariant(handle, key, resources);
}

GLShader* GLView::resolveShaderVariant(
    const ShaderHandle& handle,
    const ShaderVariantKey& key,
    const ResourceManager& resources)
{
    if (!handle) return nullptr;
    const ShaderAsset& asset = resources.get(handle);
    const uint32_t shaderId   = handle.id();
    const uint32_t generation = handle.key.generation;
    const uint32_t encoded    = key.encode();
    const uint64_t subkey     = variantSubkey(generation, encoded);

    auto& bucket = m_shaderVariants[shaderId];

    auto it = bucket.find(subkey);
    if (it != bucket.end()) {
        // Hot-reload safety: if the base asset version changed since we
        // built this variant, every variant for this shader is stale. The
        // nested-map shape lets us evict them in one O(N_for_shader)
        // clear() without scanning siblings.
        if (it->second.assetVersion != asset.version) {
            bucket.clear();
        } else {
            return it->second.program.get();
        }
    } else if (!bucket.empty()) {
        // Cache miss for (shaderId, subkey). Check whether the bucket
        // holds entries from a previous SlotAllocator generation: those
        // handles are unreachable so the variants are dead weight (and
        // hazards on name lookup). Drop any whose generation differs.
        for (auto bit = bucket.begin(); bit != bucket.end(); ) {
            const uint32_t entryGen = static_cast<uint32_t>(bit->first >> 32);
            if (entryGen != generation) bit = bucket.erase(bit);
            else ++bit;
        }
    }

    VariantEntry entry;
    const auto defines = variantKeyToDefines(key);
    try {
        entry.program = std::make_unique<GLShader>(asset, defines);
        entry.assetVersion = asset.version;
    } catch (const std::exception& e) {
        LOG_ERROR("Shader variant compile failed (shader '%s', key 0x%x): %s",
            asset.name.c_str(), encoded, e.what());

        std::string definesSummary;
        for (const auto& d : defines) {
            if (!definesSummary.empty()) definesSummary += ' ';
            definesSummary += d;
        }
        ShaderErrorLog::get().push(asset.name, std::move(definesSummary), e.what());
        return nullptr;
    }
    LOG_INFO("Compiled shader variant '%s' key=0x%x", asset.name.c_str(), encoded);
    GLShader* raw = entry.program.get();
    bucket.emplace(subkey, std::move(entry));
    // Capture the display name on first compile - kept across hot-reload
    // evictions so the GPU panel's variant-cache row stays labeled even
    // when the bucket has just been cleared and is empty mid-recompile.
    if (m_shaderVariantNames.find(shaderId) == m_shaderVariantNames.end()) {
        m_shaderVariantNames.emplace(shaderId, asset.name);
    }
    return raw;
}

const GLMaterial* GLView::ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources) {
    if (!handle) return nullptr;
    thread_local std::vector<MaterialHandle> one;
    one.clear();
    one.push_back(handle);
    syncTable<MaterialAsset>(m_materialTable, one, resources);
    return m_materialTable.entries[handle.id()].get();
}

GLMesh* GLView::ensureMesh(const MeshHandle& handle, const ResourceManager& resources) {
    if (!handle) return nullptr;
    thread_local std::vector<MeshHandle> one;
    one.clear();
    one.push_back(handle);
    syncTable<MeshAsset>(m_meshTable, one, resources);
    return m_meshTable.entries[handle.id()].get();
}

void GLView::ensureMaterialTextures(
    const MaterialHandle& handle,
    const ResourceManager& resources
) {
    if (!handle) return;
    const auto& material = resources.get(handle);

    thread_local std::vector<TextureHandle> texs;
    texs.clear();
    for (const auto& mapping : g_textureMappings) {
        const TextureHandle& th = material.*mapping.handlePtr;
        if (th) texs.push_back(th);
    }
    if (!texs.empty()) {
        sortUnique(texs);
        syncTable<TextureAsset>(m_textureTable, texs, resources);
    }
}

std::vector<GLView::VariantCacheStats> GLView::getVariantCacheStats() const {
    std::vector<VariantCacheStats> stats;
    stats.reserve(m_shaderVariants.size());
    for (const auto& [shaderId, bucket] : m_shaderVariants) {
        VariantCacheStats s;
        s.shaderId = shaderId;
        s.variants = bucket.size();
        auto it = m_shaderVariantNames.find(shaderId);
        if (it != m_shaderVariantNames.end()) s.name = it->second;
        stats.push_back(std::move(s));
    }
    return stats;
}

} // namespace Engine

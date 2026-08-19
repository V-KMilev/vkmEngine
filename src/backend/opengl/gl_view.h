#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"

#include "data/gl_mesh.h"
#include "data/gl_material.h"
#include "data/gl_texture.h"

namespace Vkm::GL {
    class Texture2D;
}

namespace Engine {
    struct RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Versioned, handle-indexed table of one GL resource kind.
 *
 * @tparam GLT The GL resource type stored (GLMesh, GLMaterial, GLTexture).
 */
template <typename GLT>
struct GLResourceTable {
    struct Slot {
        std::unique_ptr<GLT> gl;
        uint64_t             version    = 0;
        uint32_t             generation = 0;  ///< handle generation; mismatch == slot recycled
    };
    std::vector<Slot> slots;  ///< indexed by handle.id()
};

/**
 * @brief GPU-side mirror of the assets a frame references.
 *
 * One table per asset kind, indexed by handle.id() and version-gated: an asset
 * is uploaded the first time a frame references it and re-uploaded only when its
 * version changes. This is the only place the backend reads ResourceManager;
 * the render path resolves a drawable's handles to these GPU objects.
 *
 * The version gate is only meaningful within a single asset graph. A scene load
 * or editor play-stop restore swaps the whole graph, and the incoming one
 * restarts its handles and versions from scratch, so the gate cannot see the
 * difference. The backend detects that swap centrally and calls invalidate().
 */
class GLView {
    public:
        GLView() = default;
        ~GLView() = default;

        GLView(const GLView& other) = delete;
        GLView& operator=(const GLView& other) = delete;

        GLView(GLView && other) = delete;
        GLView& operator=(GLView && other) = delete;

    public:
        /**
         * @brief Upload / refresh every asset `view` references.
         *
         * Every array on RenderView that carries a handle is walked here, and
         * that is the whole of the rule: drawables (mesh, material and the
         * material's textures), shadow casters (mesh), decals (material and its
         * textures) and the UI's draw commands (font atlas). Nothing else on
         * RenderView holds one.
         *
         * The list matters because three of those four are gathered scene-wide
         * rather than from the visible set, so their assets need not appear
         * among the drawables at all - and every pass answers a missing GPU
         * object by silently skipping the draw.
         *
         * @param view The render view to sync.
         * @param resources The resource manager to use.
         */
        void sync(const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Drop every cached GPU object.
         *
         * Called when the asset graph is replaced: the incoming graph reuses the
         * same handle indices, generations and versions, so nothing in these
         * tables can be matched against it. The next sync() repopulates.
         */
        void invalidate();

        /**
         * @brief Resolve a handle to its synced GPU object; null if the handle is empty
         * or its asset has not been sync()'d into the table yet. The returned
         * pointer is owned by this table - do not store it across a sync().
         */
        const GLMesh*     getMesh(const MeshHandle& handle) const;
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;
        const Vkm::GL::Texture2D* getTexture(const TextureHandle& handle) const;
        const Vkm::GL::Texture2D* getFontAtlas(const FontHandle& handle) const;

        /**
         * @brief The magenta/black checkerboard bound wherever a real texture
         *        should be but isn't.
         *
         * A material that references a texture which is still streaming, failed
         * to decode, or no longer resolves would otherwise sample whatever the
         * previous draw left in that slot - so a broken asset shows up as some
         * other object's texture, which reads as a shading bug rather than a
         * missing file. Binding something deliberately, obviously wrong makes
         * the failure self-reporting.
         *
         * Built on first use, so a frame that never misses never allocates it.
         *
         * @return The placeholder texture; never null once the GL context exists.
         */
        const Vkm::GL::Texture2D& missingTexture() const;

    private:
        /**
         * @brief Upload or refresh a single asset into its table, version-gated.
         *
         * @tparam GLT The type of the GL resource.
         * @tparam AssetT The type of the asset.
         * @param table The table to ensure.
         * @param handle The handle to ensure.
         * @param resources The resource manager to use.
         */
        template <typename GLT, typename AssetT>
        void ensure(GLResourceTable<GLT>& table, const Handle<AssetT>& handle, const ResourceManager& resources);

        /**
         * @brief Warn once if @p handle's asset settled with no pixels.
         *
         * The placeholder makes a missing texture visible; this names the file,
         * which is the part the screen cannot tell you. Assets still streaming
         * are skipped - those are not failures, and they resolve on their own.
         *
         * @param handle    The texture to check.
         * @param resources The resource manager holding it.
         */
        void reportIfMissing(const TextureHandle& handle, const ResourceManager& resources);

        /**
         * @brief Upload @p handle's material and every texture it binds.
         *
         * The textures are discovered off the GLMaterial this call just synced,
         * so the material is always present before its maps are needed and no
         * second pass is required.
         *
         * @param handle    The material to upload.
         * @param resources The resource manager holding it.
         */
        void ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources);

    private:
        GLResourceTable<GLMesh>     m_meshes;
        GLResourceTable<GLMaterial> m_materials;
        GLResourceTable<GLTexture>  m_textures;
        GLResourceTable<GLTexture>  m_fontAtlases;  ///< SDF atlases keyed by FontHandle (fonts carry pixels, not TextureAssets).

        // Not part of the tables: it belongs to no asset and must survive the
        // epoch flush, since a graph swap is exactly when things are missing.
        mutable std::unique_ptr<Vkm::GL::Texture2D> m_missingTexture;

        std::unordered_set<uint32_t> m_reportedMissing;  ///< Texture ids already warned about, so the log stays one line per asset.

};

} // namespace Engine

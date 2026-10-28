#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"
#include "system/render/render_settings.h"

#include "asset/gl_mesh.h"
#include "asset/gl_material.h"
#include "asset/gl_asset_texture.h"
#include "gl_stream_ring.h"

namespace Vkm::GL {
    class ComputeShader;
    class Texture2D;
}

namespace Vkm::Engine {

struct RenderView;
class ResourceManager;

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
        uint64_t             checked    = 0;  ///< The GLView sync stamp it was last checked under.
        uint32_t             generation = 0;  ///< Handle generation; mismatch == slot recycled.
    };
    std::vector<Slot> slots;  ///< Indexed by handle.id().
};

/**
 * @brief GPU-side mirror of the assets a frame references.
 *
 * One table per asset kind, indexed by handle.id(): an asset uploads when a frame first
 * references it and again only when its version changes. The gate holds within one asset graph;
 * a new graph restarts handles and versions, so invalidate() runs on the swap (see
 * GLBackend::onWorldReplaced).
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
         * @brief Start compiling the program a sync runs (shaders/material/layer_copy).
         *
         * At startup, with the first frame's: the first sync builds the layered materials, and a
         * program asked for then would wait behind every other the driver is compiling.
         */
        void buildPrograms();

        /**
         * @brief Upload / refresh every asset `view` references.
         *
         * Every RenderView list naming a handle is walked: the camera's objects and the
         * scene-wide ones (mesh, material and its textures - every material, so an object first
         * turned to uploads nothing that frame - and a fixed object's given mesh,
         * RenderObjects::bases, which the lightmap bake draws), decals, particle textures, and the
         * UI's commands (font atlas, image).
         *
         * A missing GPU object skips a draw silently, so the walk names every RenderView member:
         * one added there fails to compile here until it has been classified.
         *
         * @param view      Its drawables, decals and overlay name the assets.
         * @param resources Resolves the handles to the assets to upload.
         */
        void sync(const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Upload @p handle's material and every texture it binds.
         *
         * The textures are found off the GLMaterial just synced, so no second pass is needed.
         *
         * @param handle    Material to upload; an empty handle does nothing.
         * @param resources Resolves the handle and its textures.
         */
        void ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources);

        /**
         * @brief Drop every cached GPU object.
         *
         * For a replaced asset graph, which reuses handle indices, generations and versions. The
         * next sync() repopulates.
         */
        void invalidate();

        /**
         * @brief Offer the frame's filtering mode and anisotropy to every synced texture.
         *
         * Sampler state, so no version gate covers it: a moved setting is pushed over the table
         * here, and a texture built later takes the remembered one in ensure(). Each texture
         * resolves it against its TextureParams::filterOverride; font atlases keep the font's own.
         *
         * @param mode          Base filter; Nearest and Bilinear pin the degree to 1.
         * @param maxAnisotropy Requested degree; clamped per texture to what the driver reports.
         */
        void setTextureFiltering(TextureFiltering mode, float maxAnisotropy);

        /**
         * @brief Upload @p handle's texture outside a sync(), for a caller that shows rather than draws it.
         *
         * sync() reaches a texture only through a material, wrong for a tool showing the library.
         *
         * @param handle Texture to upload; an empty handle does nothing.
         * @param resources Resolves the handle to its pixels.
         */
        void ensureTexture(const TextureHandle& handle, const ResourceManager& resources);

        /**
         * @brief Upload @p mesh and @p material, its maps with it, outside a sync().
         *
         * For a caller drawing them alone, whose made-up view must not stand in for the frame's:
         * sync() keeps track of the frame's objects and assets from one call to the next.
         *
         * @param mesh      Mesh to upload; an empty handle does nothing.
         * @param material  Material to upload; the same.
         * @param resources Resolves both.
         */
        void ensureDrawn(
            const MeshHandle& mesh,
            const MaterialHandle& material,
            const ResourceManager& resources
        );

        /**
         * @brief Resolve a handle to its synced GPU object.
         *
         * Null for an empty, unsynced or stale handle (a recycled slot answers only its new
         * asset's), and for a texture or font atlas until its pixels arrive. Do not store the
         * pointer across a sync().
         *
         * @param handle The asset asked for.
         * @return Its GPU object, or null.
         */
        const GLMesh*     getMesh(const MeshHandle& handle) const;
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;
        const Vkm::GL::Texture2D* getTexture(const TextureHandle& handle) const;
        const Vkm::GL::Texture2D* getFontAtlas(const FontHandle& handle) const;

        /**
         * @brief Whether the mirror of @p handle holds the pixels of its asset at @p version.
         *
         * Behind RenderBackend::holdsPixels: a slot of this generation, synced at this version,
         * whose pixels arrived.
         *
         * @param handle  The texture asked about.
         * @param version Its asset's version now.
         * @return True when that upload has happened.
         */
        bool holdsPixels(const TextureHandle& handle, uint64_t version) const;

        /**
         * @brief Which upload @p handle's pixels came from (GLTexture::uploadId).
         *
         * @param handle The texture asked about.
         * @return The upload, or 0 while getTexture() would answer null.
         */
        uint64_t textureUploadId(const TextureHandle& handle) const;

        /**
         * @brief Whether @p handle settled with no pixels: its decode failed or its file is gone.
         *
         * It stays so until the graph is invalidated, pixels that come later or not: ask
         * getTexture() first.
         *
         * @param handle The texture asked about.
         * @return False while it is still loading.
         */
        bool textureFailed(const TextureHandle& handle) const {
            return m_reportedMissing.count(handle.id()) > 0;
        }

        /**
         * @brief The magenta/black checkerboard bound wherever a real texture should be but isn't.
         *
         * Built on first use.
         *
         * @return The placeholder texture; never null once the GL context exists.
         */
        const Vkm::GL::Texture2D& missingTexture() const;

        /**
         * @brief The pool every mesh of this mirror lives in.
         *
         * Meshes the backend builds for itself live in it too.
         *
         * @return The pool; it lives as long as this mirror.
         */
        GLMeshPool& meshPool() { return m_meshPool; }

        /**
         * @brief The frame's streaming memory: per-frame lists and matrices go through it.
         *
         * Not the view's resource state, so a reader holding the view const streams too. The
         * backend ends each frame on it (StreamRing::endFrame).
         *
         * @return The ring; it lives as long as this mirror.
         */
        Vkm::GL::StreamRing& stream() const { return m_stream; }

        /**
         * @brief ResourceManager::revision() at the last sync() that walked every scene object.
         *
         * A change means the assets were checked again, so whatever was worked out from their GPU
         * uploads is to be worked out again.
         *
         * @return The revision; 0 before the first such walk.
         */
        uint64_t syncedRevision() const { return m_syncedRevision; }

        /**
         * @brief Whether @p drawn reads lightmap coordinates laid out as @p given's are, so what is
         *        baked into @p given's lightmap can be read through it.
         *
         * True of @p given itself, and of a mesh every lightmap coordinate of which is one of
         * @p given's: a decimation of it, or a terrain chunk's coarser level. A level unwrapped on
         * its own lays its charts out elsewhere. As sync() last worked it out for a fixed object
         * drawing @p drawn for @p given (RenderObjects::bases); false for a pair it has not.
         *
         * @param drawn The mesh an object draws.
         * @param given The mesh it was given.
         * @return Whether the two lay their lightmap coordinates out alike.
         */
        bool readsChartsOf(const MeshHandle& drawn, const MeshHandle& given) const;

    private:
        /// What sync() worked out of one drawn mesh and its given one, as of their uploads.
        struct ChartsCheck {
            uint64_t drawnUpload = 0;
            uint64_t givenUpload = 0;
            bool     reads       = false;
        };

    private:
        /**
         * @brief Work out readsChartsOf() for @p drawn and @p given, unless their uploads still
         *        hold the answer.
         *
         * @param drawn     The mesh an object draws.
         * @param given     The mesh it was given.
         * @param resources Resolves both to their lightmap coordinates.
         */
        void checkCharts(const MeshHandle& drawn, const MeshHandle& given, const ResourceManager& resources);

        /**
         * @brief Ready a layered material's layers - each with its maps - and build its arrays from
         *        them (GLMaterial::updateLayers).
         *
         * @param asset     The layered material.
         * @param material  Its GPU copy.
         * @param resources Resolves its layers and their maps.
         */
        void readyLayers(const MaterialAsset& asset, GLMaterial& material, const ResourceManager& resources);

        /**
         * @brief Upload or refresh a single asset into its table, version-gated.
         *
         * A recycled slot's new asset also starts at version 1, so an in-place update() needs the
         * handle's generation to match the slot's; otherwise it rebuilds. An asset checked under
         * the current sync stamp is skipped, as a scene draws few assets many times.
         *
         * @tparam GLT    The table's GPU type.
         * @tparam AssetT The asset kind @p handle names.
         * @param table     The table of that kind.
         * @param handle    The asset to check; an empty handle does nothing.
         * @param resources Resolves the handle to the asset.
         * @return True when checked now; false for an empty handle or one this stamp checked.
         */
        template <typename GLT, typename AssetT>
        bool ensure(
            GLResourceTable<GLT>& table,
            const Handle<AssetT>& handle,
            const ResourceManager& resources
        );

        /**
         * @brief The GPU object @p handle names in @p table, if it names one.
         *
         * @tparam GLT    The table's GPU type.
         * @tparam AssetT The asset kind @p handle names.
         * @param table  The table to look in.
         * @param handle The asset asked for.
         * @return The slot's object when it was built for this handle's
         *         generation; null for an empty, unsynced or stale handle.
         */
        template <typename GLT, typename AssetT>
        const GLT* find(const GLResourceTable<GLT>& table, const Handle<AssetT>& handle) const;

        /**
         * @brief Warn once if @p handle's asset settled with no pixels.
         *
         * The placeholder shows the miss; this names the file. Still-streaming assets are skipped.
         * Asked of the mirror, not the asset: pixels a host released after upload still draw.
         *
         * @param handle    The texture just synced.
         * @param resources For its name, its path and whether it is still loading.
         */
        void reportIfMissing(const TextureHandle& handle, const ResourceManager& resources);

    private:
        /// Every mesh's vertices and indices; outlives the table below.
        GLMeshPool                  m_meshPool;
        GLResourceTable<GLMesh>     m_meshes;
        GLResourceTable<GLMaterial> m_materials;
        /// Fills a layered material's arrays (GLLayerMaps); see buildPrograms.
        std::unique_ptr<Vkm::GL::ComputeShader> m_layerCopy;
        GLResourceTable<GLTexture>  m_textures;
        /// SDF atlases by FontHandle; fonts carry pixels, not TextureAssets.
        GLResourceTable<GLTexture>  m_fontAtlases;
        /// The frame's streaming memory; see stream().
        mutable Vkm::GL::StreamRing m_stream;

        // Outside the tables: it belongs to no asset, and must survive a graph swap, when things go missing.
        mutable std::unique_ptr<Vkm::GL::Texture2D> m_missingTexture;

        /// Texture ids that settled with no pixels, each warned about once.
        std::unordered_set<uint32_t> m_reportedMissing;

        /// readsChartsOf()'s answers, by the two meshes' ids.
        std::unordered_map<uint64_t, ChartsCheck> m_charts;

        /// The filtering and anisotropy every texture was last given.
        TextureFiltering m_filtering  = RenderSettings{}.textureFiltering;
        float            m_anisotropy = static_cast<float>(RenderSettings{}.textureAnisotropy);

        uint64_t m_syncStamp      = 1;  ///< Bumped by every sync(); a slot checked under it is skipped.
        uint64_t m_syncedRevision = 0;  ///< See syncedRevision().
        uint64_t m_syncedSerial   = 0;  ///< RenderObjects::serial of the last frame synced.
};

} // namespace Vkm::Engine

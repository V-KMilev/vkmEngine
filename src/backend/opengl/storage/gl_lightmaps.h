#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "frame/gl_lights.h"

namespace Vkm::GL {
    class ShaderStorageBuffer;
    class Texture2DArray;
}

namespace Vkm::Engine {

class GLView;
struct LightData;
struct RenderObjects;

/**
 * @brief The lightmaps fixed meshes read their fixed lights' shadows from: the pages, where in
 *        them each object's lightmap coordinates land, and the channel each light's shadow is in.
 *
 * A page is an RGBA8 layer, one fixed light (Light::movable off) a channel. An object with no
 * page, and a light with no channel, are lit as they would be without them (shaders/lightmap.glsl).
 */
class GLLightmaps {
    public:
        /// One object's place in the pages: std430, as shaders/lightmap.glsl reads it.
        struct ObjectLightmap {
            glm::vec4  scaleOffset{1.0f, 1.0f, 0.0f, 0.0f};  ///< Its lightmap coordinates into the page.
            glm::ivec4 page{-1, 0, 0, 0};                    ///< x: the page, -1 for none.
        };

        /// A receiver's place as baked, and what it was baked as: read only while it still is.
        struct Place {
            ObjectLightmap lightmap;
            glm::mat4      model{1.0f};  ///< Its world matrix when baked.
        };

        /// Each placed object's place, by its key (RenderObjects::keys).
        using Places = std::unordered_map<uint64_t, Place>;

        /// A light baked into a channel: the entity slot it shone from, and its shape then (shapeOf).
        struct BakedLight {
            uint32_t slot    = 0;
            int      channel = -1;
            uint64_t shape   = 0;
        };

        /// A page's side, in texels.
        static constexpr uint32_t PAGE_SIZE    = 2048;
        /// A page's mip levels, for a far read: few enough that a texel of the last still lies in
        /// one receiver's square and its gutter.
        static constexpr int      LEVELS       = 2;
        /// The most fixed lights baked: a page's channels.
        static constexpr int      MAX_CHANNELS = 4;

    public:
        GLLightmaps();
        ~GLLightmaps();

        GLLightmaps(const GLLightmaps& other) = delete;
        GLLightmaps& operator=(const GLLightmaps& other) = delete;

        GLLightmaps(GLLightmaps && other) = delete;
        GLLightmaps& operator=(GLLightmaps && other) = delete;

    public:
        /**
         * @brief Hold an entry for every object @p objects has, at its index, and upload what
         *        changed.
         *
         * An entry is the place the object's key was given while the object is as it was baked -
         * fixed, where it stood, and drawing a mesh that reads its given mesh's charts
         * (GLView::readsChartsOf) - else none; an object keeps its place when its index moves. A
         * bake's set is kept as the shown one is.
         *
         * @param objects The frame's objects.
         * @param glView  GPU mirror: which drawn meshes read their given meshes' charts.
         */
        void prepare(const RenderObjects& objects, const GLView& glView);

        /// The per-object places at SSBO_INSTANCE_LIGHTMAP.
        void bindObjects() const;

        /// The pages at LIGHTMAP_SLOT_MASK; a unit another pass shares, so the reader binds it.
        void bindPages() const;

        /// Whether any object reads a page.
        bool any() const { return m_shown.pageCount > 0; }

        /**
         * @brief What a light's baked mask depends on: its kind, where it stands and which way, how
         *        far it reaches and how wide its source is - not its color or brightness.
         *
         * @param light One of the frame's lights.
         * @return A hash of those.
         */
        static uint64_t shapeOf(const LightData& light);

        /**
         * @brief The lights a bake takes, a channel each in this order: fixed (Light::movable off),
         *        casting shadows and of a kind that casts them, the first MAX_CHANNELS by entity slot.
         *
         * @param lights The frame's lights.
         * @param baked  Filled with their indices in @p lights.
         */
        static void bakedLights(const std::vector<LightData>& lights, std::vector<uint32_t>& baked);

        /**
         * @brief Each of @p lights' mask channels, in their order: a baked light's, -1 for the rest.
         *
         * A light reads a channel only while bakedLights names it and it has the shape it was baked
         * with: one that stopped casting, another in a gone one's slot, or one moved, shines live
         * until a bake of it lands.
         *
         * @param lights The frame's lights.
         * @return A view of them, valid until the next call.
         */
        LightSlots channels(const std::vector<LightData>& lights);

        /**
         * @brief Start a bake's set beside the one shown: @p pageCount pages, lit until it writes
         *        them, each object's place in them, and each baked light's channel.
         *
         * The frames keep reading the shown set until finishBake() trades this one in.
         *
         * @param objects       The frame's objects, whose entries the places fill at once.
         * @param glView        GPU mirror, as prepare() reads it.
         * @param places        By object key; an object with none is on no page.
         * @param pageCount     Pages the places name.
         * @param lightChannels Each baked light's slot, channel and shape.
         */
        void beginBake(
            const RenderObjects& objects,
            const GLView& glView,
            Places places,
            uint32_t pageCount,
            std::vector<BakedLight> lightChannels
        );

        /// Whether a bake's set is open.
        bool baking() const { return m_baking != nullptr; }

        /**
         * @brief Bind at SSBO_INSTANCE_LIGHTMAP, for the bake's own draws, each object's place by its
         *        key alone, whatever it draws now: they draw the given meshes the places were sized
         *        from.
         *
         * @param objects The frame's objects, whose indices the draws name.
         */
        void bindBakingObjects(const RenderObjects& objects);

        /// The bake's page array, which it writes into.
        const Vkm::GL::Texture2DArray& bakingPages() const { return *m_baking->pages; }

        /// Make the bake's pages' further levels from their first, and show its set.
        void finishBake();

        /// Close the bake's set unshown.
        void dropBake();

        /// Show no lightmaps, and drop any bake: as for a replaced world.
        void clear();

    private:
        /// One set of lightmaps: its pages, where each object lies in them and the lights' channels.
        struct Set {
            Places                                        places;
            std::vector<BakedLight>                       lightChannels;  ///< The lights baked.
            std::unique_ptr<Vkm::GL::Texture2DArray>      pages;  ///< A 1x1 layer, lit, for none.
            uint32_t                                      pageCount = 0;  ///< Pages an object reads.
            std::vector<ObjectLightmap>                   objects;  ///< By object index, from places.
            std::unique_ptr<Vkm::GL::ShaderStorageBuffer> buffer;
            bool                                          dirty    = true;
            bool                                          seen     = false;  ///< Whether serial holds one.
            uint64_t                                      serial   = 0;      ///< The objects' serial then.
            uint64_t                                      revision = 0;      ///< GLView::syncedRevision then.
        };

    private:
        /// Make @p set's page array: @p count pages of PAGE_SIZE, lit; one lit texel for none.
        static void makePages(Set& set, uint32_t count);

        /// Hold an entry in @p set for every object, as prepare() describes.
        static void prepareSet(Set& set, const RenderObjects& objects, const GLView& glView);

        /// Upload @p entries into @p buffer, never empty.
        static void upload(
            const std::vector<ObjectLightmap>& entries,
            std::unique_ptr<Vkm::GL::ShaderStorageBuffer>& buffer
        );

        /// Set object @p o's entry in @p set from its key's place, as prepare() says; whether it changed.
        static bool place(Set& set, const RenderObjects& objects, const GLView& glView, uint32_t o);

        /// Set every object's entry in @p set and remember @p objects and @p glView's revision as seen.
        static void placeAll(Set& set, const RenderObjects& objects, const GLView& glView);

    private:
        Set                   m_shown;
        std::unique_ptr<Set>  m_baking;    ///< The bake under way's; null for none.
        std::vector<int>      m_channels;  ///< The last channels() answer.
        std::vector<uint32_t> m_baked;     ///< channels()'s bakedLights.

        /// bindBakingObjects()'s entries, by object index, and their buffer.
        std::vector<ObjectLightmap>                   m_planned;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_plannedBuffer;
};

} // namespace Vkm::Engine

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "storage/light/gl_light_slots.h"

namespace Vkm::OpenGL {
    class ShaderStorageBuffer;
    class Texture2DArray;
}

namespace Vkm::Engine {

class GLAssetMirror;
struct LightData;
struct RenderObjects;

/**
 * @brief The lightmaps fixed meshes read their fixed lights' shadows and the sky they see from:
 *        the pages, where in them each object's lightmap coordinates land, and the channel each
 *        light's shadow is in.
 *
 * A page is an RGBA8 layer, one fixed light (Light::movable off) a channel, and a layer of the
 * same place in the sky array: the share of the sky each texel sees past the fixed meshes. An
 * object with no page, and a light with no channel, are lit as they would be without them
 * (shaders/opengl/include/lightmap.glsl).
 */
class GLLightmaps {
    public:
        /**
         * @brief One object's place in the pages: std430, as shaders/opengl/include/lightmap.glsl reads it.
         */
        struct ObjectLightmap {
            glm::vec4  scaleOffset{1.0f, 1.0f, 0.0f, 0.0f};  ///< Its lightmap coordinates into the page: xy
                                                             ///< scale, zw offset (pageUV).
            glm::ivec4 page{-1, 0, 0, 0};                    ///< x: the page, -1 for none.
        };

        /**
         * @brief A receiver's place as baked, and what it was baked as: read only while it still is.
         */
        struct Place {
            ObjectLightmap lightmap;
            glm::mat4      model{1.0f};  ///< Its world matrix when baked.
        };

        /**
         * @brief Each placed object's place, by its key (RenderObjects::keys).
         */
        using Places = std::unordered_map<uint64_t, Place>;

        /**
         * @brief A light baked into a channel: the entity slot it shone from, and its shape then (shapeOf).
         */
        struct BakedLight {
            uint32_t slot    = 0;
            int      channel = -1;
            uint64_t shape   = 0;
        };

        static constexpr uint32_t PAGE_SIZE     = 2048;  ///< A page's side, in texels.
        static constexpr int      LEVELS        = 2;     ///< A page's mip levels, for a far read: few enough
                                                         ///< that a texel of the last still lies in one
                                                         ///< receiver's square and its gutter.
        static constexpr int      MAX_CHANNELS  = 4;     ///< The most fixed lights baked: a page's channels.
        static constexpr uint32_t RETIRE_FRAMES = 4;     ///< Frames a set let go of is kept before it is
                                                         ///< freed: freed while frames in flight still draw
                                                         ///< with it, a large one stalls the frame that frees
                                                         ///< it.

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
         * (GLAssetMirror::readsChartsOf) - else none; an object keeps its place when its index moves. A
         * bake's set is kept as the shown one is.
         *
         * @param objects The frame's objects.
         * @param mirror  GPU mirror: which drawn meshes read their given meshes' charts.
         */
        void prepare(const RenderObjects& objects, const GLAssetMirror& mirror);

        /// The per-object places at SSBO_INSTANCE_LIGHTMAP.
        void bindObjects() const;

        /// The pages at LIGHTMAP_SLOT_MASK and the sky at LIGHTMAP_SLOT_SKY; units other passes
        /// share, so the reader binds them.
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
         * @param mirror        GPU mirror, as prepare() reads it.
         * @param places        By object key; an object with none is on no page.
         * @param pageCount     Pages the places name.
         * @param lightChannels Each baked light's slot, channel and shape.
         */
        void beginBake(
            const RenderObjects& objects,
            const GLAssetMirror& mirror,
            Places places,
            uint32_t pageCount,
            std::vector<BakedLight> lightChannels
        );

        /**
         * @brief Start a bake's set over the shown one's places and pages, for some lights' channels
         *        alone: what the rest hold is copied in (GLLightmapBaker) before those are baked.
         *
         * @param objects       The frame's objects, as beginBake() takes them.
         * @param mirror        GPU mirror, as prepare() reads it.
         * @param lightChannels Each baked light's slot, channel and shape: the shown set's, the
         *                      lights baked again where they stand now.
         */
        void beginPartialBake(
            const RenderObjects& objects,
            const GLAssetMirror& mirror,
            std::vector<BakedLight> lightChannels
        );

        /// Whether a bake's set is open.
        bool baking() const { return m_baking != nullptr; }

        /// Whether the shown set holds channel @p channel baked for the light of entity slot @p slot
        /// standing as @p shape says (shapeOf).
        bool holdsChannel(uint32_t slot, int channel, uint64_t shape) const;

        /// The lights the shown set holds, each with its channel and the shape it was baked at.
        const std::vector<BakedLight>& shownChannels() const { return m_shown.lightChannels; }

        /// The shown set's page array, which a partial bake copies from.
        const Vkm::OpenGL::Texture2DArray& shownPages() const { return *m_shown.pages; }

        /// The shown set's sky array, which a partial bake copies from.
        const Vkm::OpenGL::Texture2DArray& shownSky() const { return *m_shown.sky; }

        /**
         * @brief Bind at SSBO_INSTANCE_LIGHTMAP, for the bake's own draws, each object's place by its
         *        key alone, whatever it draws now: they draw the given meshes the places were sized
         *        from.
         *
         * @param objects The frame's objects, whose indices the draws name.
         */
        void bindBakingObjects(const RenderObjects& objects);

        /// The bake's page array, which it writes into.
        const Vkm::OpenGL::Texture2DArray& bakingPages() const { return *m_baking->pages; }

        /// The bake's sky array, which it writes into.
        const Vkm::OpenGL::Texture2DArray& bakingSky() const { return *m_baking->sky; }

        /// Make the bake's pages' further levels from their first, and show its set.
        void finishBake();

        /// Close the bake's set unshown; it is freed RETIRE_FRAMES frames on.
        void dropBake();

        /// Show no lightmaps, and drop any bake: as for a replaced world.
        void clear();

        /**
         * @brief Keep the share of the sky each probe center saw in the last bake, by entity slot
         *        (GLLightmapBaker): what probeSky() answers.
         *
         * @param slots Each probe's entity slot.
         * @param sky Its share of the sky, as a texel's (shaders/opengl/include/lightmap.glsl), in the same
         * order.
         */
        void setProbeSky(const std::vector<uint32_t>& slots, const std::vector<float>& sky);

        /**
         * @brief The share of the sky the probe of entity slot @p slot saw in the last bake.
         *
         * @param slot A probe's entity slot (ProbeData::entitySlot).
         * @return Its share, 0 for a probe the bake did not see: one that sees no sky, whose
         *         capture holds what shuts the sky out, as a probe added since must be taken to.
         */
        float probeSky(uint32_t slot) const;

    private:
        /**
         * @brief One set of lightmaps: its pages, where each object lies in them and the lights' channels.
         */
        struct Set {
            Places                                            places;
            std::vector<BakedLight>                           lightChannels;  ///< The lights baked.
            std::unique_ptr<Vkm::OpenGL::Texture2DArray>      pages;          ///< A 1x1 layer, lit, for none.
            std::unique_ptr<Vkm::OpenGL::Texture2DArray>      sky;            ///< R8, a layer a page; 1 all
                                                                              ///< sky.
            uint32_t                                          pageCount = 0;  ///< Pages an object reads.
            std::vector<ObjectLightmap>                       objects;        ///< By object index, from
                                                                              ///< places.
            std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> buffer;

            bool     dirty    = true;
            bool     seen     = false;  ///< Whether serial holds one.
            uint64_t serial   = 0;      ///< The objects' serial then.
            uint64_t revision = 0;      ///< The mirror's revision then.
        };

        /**
         * @brief A set let go of, kept while frames in flight may still draw with it.
         */
        struct Retired {
            std::unique_ptr<Set> set;
            uint32_t             framesLeft = 0;  ///< prepare()s before it is freed.
        };

    private:
        /// Make @p set's page and sky arrays: @p count pages of PAGE_SIZE, lit and open to the sky;
        /// one texel for none.
        static void makePages(Set& set, uint32_t count);

        /// Hold an entry in @p set for every object, as prepare() describes.
        static void prepareSet(Set& set, const RenderObjects& objects, const GLAssetMirror& mirror);

        /// Upload @p entries into @p buffer, never empty.
        static void upload(
            const std::vector<ObjectLightmap>& entries,
            std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer>& buffer
        );

        /// Set object @p o's entry in @p set from its key's place, as prepare() says; whether it changed.
        static bool place(Set& set, const RenderObjects& objects, const GLAssetMirror& mirror, uint32_t o);

        /// Set every object's entry in @p set and remember @p objects and @p mirror's revision as seen.
        static void placeAll(Set& set, const RenderObjects& objects, const GLAssetMirror& mirror);

    private:
        Set                                 m_shown;
        std::unique_ptr<Set>                m_baking;        ///< The bake under way's; null for none.
        std::vector<Retired>                m_retired;       ///< Sets let go of, each freed its frames on.
        std::vector<int>                    m_channels;      ///< The last channels() answer.
        std::unordered_map<uint32_t, float> m_probeSky;      ///< setProbeSky()'s, by entity slot.
        std::vector<uint32_t>               m_baked;         ///< channels()'s bakedLights.

        std::vector<ObjectLightmap> m_planned;
            ///< bindBakingObjects()'s entries, by object index, and their buffer.
        std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> m_plannedBuffer;
};

} // namespace Vkm::Engine

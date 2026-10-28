#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "gl_frame_buffer.h"

#include "core/math/bounds.h"
#include "frame/gl_shadow_data.h"
#include "offline/gl_caster_depth.h"

namespace Vkm::GL {
    class Context;
    class Sampler;
    class Texture2D;
    class UniformBuffer;
}

namespace Vkm::Engine {

class GLLights;
class GLObjectBuffer;
class GLView;
struct RenderView;

/**
 * @brief The shadowed sun's depth over a capture's region, kept for the regions captures return to.
 *
 * GLSceneCapture's: the frame's atlas fits the camera, and without a map of its own the sun lights
 * a room's floor through its roof. The sun is the one the frame shadows
 * (lowestSlotDirectionalCaster); other lights capture unshadowed. A map is kept per ShadowKey - the
 * sun, its direction, its biases, the region - in one of SHADOW_SLOTS, so a relight whose small
 * lights move every frame does not draw its casters every frame, and a caster moving under a still
 * sun keeps the shadow it cast, as the captured surfaces keep their place.
 */
class GLCaptureShadow {
    public:
        /**
         * @brief Compile the depth programs; the maps are made on first use.
         */
        GLCaptureShadow();
        ~GLCaptureShadow();

        GLCaptureShadow(const GLCaptureShadow& other) = delete;
        GLCaptureShadow& operator=(const GLCaptureShadow& other) = delete;

        GLCaptureShadow(GLCaptureShadow && other) = delete;
        GLCaptureShadow& operator=(GLCaptureShadow && other) = delete;

    public:
        /**
         * @brief Bind the map kept for @p view's sun over @p region, and @p lights with the sun on it.
         *
         * @param view   Supplies the lights.
         * @param region World box the map covers.
         * @param lights The capture's lights, uploaded with the sun on the map's slot.
         * @return False when no map is kept for them, and nothing is bound.
         */
        bool useKept(const RenderView& view, const Math::AABB& region, GLLights& lights);

        /**
         * @brief Draw the sun's map over @p region from @p casters and bind it, as useKept() does.
         *
         * Into the slot drawn for the same key before, or else the one bound longest ago. With no
         * sun casting, binds the lights alone.
         *
         * @param gl      Live GL context the map is drawn on.
         * @param view    Supplies the lights and the casters' boxes.
         * @param glView  GPU resource mirror the casters draw through.
         * @param objects The frame's transforms the casters index.
         * @param casters The scene-wide casters, by object.
         * @param region  World box the map covers.
         * @param lights  The capture's lights.
         */
        void draw(
            Vkm::GL::Context& gl,
            const RenderView& view,
            const GLView& glView,
            const GLObjectBuffer& objects,
            const std::vector<uint32_t>& casters,
            const Math::AABB& region,
            GLLights& lights
        );

    private:
        /// What a map was drawn for; drawn again when any of it changes.
        struct ShadowKey {
            Math::AABB region{};
            bool       casts      = false;
            uint32_t   light      = 0;  ///< The sun's entity slot.
            glm::vec3  direction{0.0f};
            float      bias       = 0.0f;
            float      normalBias = 0.0f;

            bool operator==(const ShadowKey& other) const {
                return region.min == other.region.min && region.max == other.region.max
                    && casts == other.casts && light == other.light && direction == other.direction
                    && bias == other.bias && normalBias == other.normalBias;
            }
        };

        /// The sun's depth over one region, and the ShadowBlock that reads it.
        struct ShadowSlot {
            std::optional<ShadowKey>                key;            ///< What it was drawn for; none yet.
            bool                                    casts = false;
            std::unique_ptr<Vkm::GL::Texture2D>     map;            ///< Made on first use.
            std::unique_ptr<Vkm::GL::UniformBuffer> ubo;
            ShadowUBOData                           last{};         ///< What ubo holds.
            uint64_t                                used = 0;       ///< When it was last bound.
        };

        /// Maps kept: one for the irradiance volume a relight lights and one for the probe being
        /// captured, so neither draws the other's away every frame both run.
        static constexpr int SHADOW_SLOTS = 2;

        /**
         * @brief What @p view's sun and @p region would draw a map for.
         *
         * @param view   Supplies the lights.
         * @param region World box the map covers.
         * @return The key.
         */
        static ShadowKey keyFor(const RenderView& view, const Math::AABB& region);

        /**
         * @brief Bind slot @p slot's map and ShadowBlock while it casts, and @p lights with the sun on it.
         *
         * @param slot   Which of m_slots.
         * @param view   Supplies the lights.
         * @param lights The capture's lights.
         */
        void use(int slot, const RenderView& view, GLLights& lights);

    private:
        GLCasterDepth m_casters;  ///< Draws the sun's depth.

        Vkm::GL::FrameBuffer              m_fbo;
        ShadowSlot                        m_slots[SHADOW_SLOTS];
        std::unique_ptr<Vkm::GL::Sampler> m_sampler;    ///< Compares, as the atlas's does.
        uint64_t                          m_clock = 0;  ///< Counts binds, for ShadowSlot::used.

        std::vector<uint32_t> m_overRegion;  ///< The casters over the map's region, which it draws.
        std::vector<int>      m_lightSlots;  ///< Per light: 0 for the sun while it casts, else -1.
};

} // namespace Vkm::Engine

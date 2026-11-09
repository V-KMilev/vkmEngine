#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/texture/texture.h"

namespace Vkm::Engine {

/**
 * @brief How far each point of a square of water ahead of the eye is from anything breaking it, in
 *        meters: what water's waterline foam reads, at one level a frame (GLFrameContext::shore).
 *
 * The water pass captures it (GLWaterPass): the scene drawn looking straight down, only what lies
 * under the water, the topmost of it a texel, and again only what lies over it, the lowest, against
 * the water's height on a coarse grid
 * (shaders/opengl/scene/water/shore, and shore/water); then a distance transform over it, a row and a column
 * at a time (shore/rows, shore/columns). Drawn in the world, not the screen, it rings what the
 * camera cannot see as well as what it can. Water near anything is a waterline, and foams. bind()
 * hands it to a draw on its unit (WaterTextureSlots::SHORE), beside the swell and the ripples
 * (GLWaveFields).
 */
class GLWaterShore {
    public:
        GLWaterShore() = default;
        ~GLWaterShore() = default;

        GLWaterShore(const GLWaterShore& other) = delete;
        GLWaterShore& operator=(const GLWaterShore& other) = delete;

        GLWaterShore(GLWaterShore && other) = delete;
        GLWaterShore& operator=(GLWaterShore && other) = delete;

    public:
        /// Forget the shore: no water foams at a waterline now, and what is held would be stale when
        /// one does.
        void forget() { m_shored = false; }

        /// Bind the shore; with none held it leaves the unit alone.
        void bind() const;

        /// Whether a shore is held: captured since the last forget().
        bool held() const { return m_shored; }

        /**
         * @brief Make the map a capture writes, @p size texels a side, unless it is made.
         *
         * @param size Texels on an edge.
         */
        void allocate(uint32_t size);

        /**
         * @brief Bind the map as an image, for the capture that writes it.
         *
         * @param unit   Image unit to bind on.
         * @param access GL_WRITE_ONLY for the capture.
         */
        void bindMapImage(uint32_t unit, GLenum access) const;

        /// Hold what a capture wrote, which lies over @p area (see area()).
        void hold(const glm::vec4& area) {
            m_area   = area;
            m_shored = true;
        }

    public:
        /// Where the shore lies, as last captured: xy its corner on the XZ plane (least X, most Z), z its
        /// edge in meters; w how far it looks round a point, in meters - past that it says
        /// nothing - or 0 when there is none to read.
        glm::vec4 area() const { return m_shored ? m_area : glm::vec4(0.0f); }

    private:
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_map;  ///< R16F: meters to what breaks the water.
        glm::vec4                               m_area   = glm::vec4(0.0f);
        bool                                    m_shored = false;
};

} // namespace Vkm::Engine

#pragma once

#include <cstdint>
#include <memory>

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

struct RenderObjects;

/**
 * @brief Every object's model matrix, and first bone when the frame posed, for instanced draws.
 *
 * The whole set goes up each frame at each object's own index, so a draw carries only object
 * indices through the instance attribute. Whole, since there is no record of what changed to
 * trust (engine.md, dirty flags).
 */
class GLObjectBuffer {
    public:
        GLObjectBuffer();
        ~GLObjectBuffer();

        GLObjectBuffer(const GLObjectBuffer& other) = delete;
        GLObjectBuffer& operator=(const GLObjectBuffer& other) = delete;

        GLObjectBuffer(GLObjectBuffer && other) = delete;
        GLObjectBuffer& operator=(GLObjectBuffer && other) = delete;

    public:
        /**
         * @brief Upload model matrices, and first bones when @p posed, growing the buffers as needed.
         *
         * @param objects The objects this frame's index lists address.
         * @param posed   Whether the frame has a bone palette; without one no first bone is read.
         */
        void upload(const RenderObjects& objects, bool posed);

        /**
         * @brief Bind the buffers to their storage points.
         *
         * Before a pass's first instanced draw: another GLObjectBuffer may hold the same points.
         */
        void bind() const;

    private:
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_models;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_skinFirst;

        uint32_t m_modelCapacity     = 0;  ///< Bytes allocated for the models.
        uint32_t m_skinFirstCapacity = 0;  ///< Bytes allocated for the first bones.
        bool     m_posed             = false;  ///< The last upload carried first bones.
};

} // namespace Vkm::Engine

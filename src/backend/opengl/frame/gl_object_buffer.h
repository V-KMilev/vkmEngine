#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

struct RenderObjects;

/// An object's bit in its INSTANCE_FLAGS word: drawn through the viewmodel projection (ObjectDraw).
inline constexpr uint32_t INSTANCE_FLAG_VIEWMODEL = 1u;

/**
 * @brief The share of the depth range nearest the eye a viewmodel object's depth is squashed into.
 *
 * Only what lies within about this share of the near plane beyond it can draw over it
 * (shaders/viewmodel.glsl); GTAO reads the band as sky (shaders/gtao/prefilter).
 */
inline constexpr float VIEWMODEL_DEPTH_SHARE = 0.05f;

/**
 * @brief Every object's model matrix, its flags, and first bone when the frame posed, for instanced draws.
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
         * @brief Upload model matrices and flags, and first bones when @p posed, growing the buffers.
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
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_flags;  ///< An INSTANCE_FLAG_* word an object.

        std::vector<uint32_t> m_flagScratch;  ///< The flags as uploaded; capacity kept across frames.

        uint32_t m_modelCapacity     = 0;  ///< Bytes allocated for the models.
        uint32_t m_skinFirstCapacity = 0;  ///< Bytes allocated for the first bones.
        uint32_t m_flagCapacity      = 0;  ///< Bytes allocated for the flags.
        bool     m_posed             = false;  ///< The last upload carried first bones.
};

} // namespace Vkm::Engine

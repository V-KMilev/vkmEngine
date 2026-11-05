#pragma once

#include <cstdint>
#include <vector>

#include "vkm_opengl/buffer/stream_ring.h"

#include "asset/gl_mesh.h"

namespace Vkm::Engine {

inline constexpr uint32_t INSTANCE_OBJECT_BITS = 24;        ///< An instance (GLDrawList::instances) is its
                                                            ///< object's index in the bits under this, and
                                                            ///< above them how the eye's draw fades it
                                                            ///< (MeshOf::Eye,
                                                            ///< shaders/opengl/scene/forward/fade.glsl); none
                                                            ///< above is drawn whole. Objects number under
                                                            ///< two to its power, which
                                                            ///< GLObjectBuffer::upload asserts.
inline constexpr uint32_t INSTANCE_OBJECT_MASK = (1u << INSTANCE_OBJECT_BITS) - 1u;
inline constexpr uint32_t INSTANCE_FADE_STEPS  = 128;       ///< The steps of an instance's fade, the share of
                                                            ///< its samples it gives up, in the bits above
                                                            ///< its object.
inline constexpr uint32_t INSTANCE_FADES_IN    = 1u << 31;  ///< The top bit: the instance is the level fading
                                                            ///< in, drawn on the samples the other gives up.
static_assert(
    (INSTANCE_FADE_STEPS << INSTANCE_OBJECT_BITS) == INSTANCE_FADES_IN,
    "a fade fills the bits between the object and the top"
);

/**
 * @brief The two lists a multi-draw reads: instances, and the commands that slice them.
 *
 * A GLMesh::command per run of instances sharing a mesh, its baseInstance where the run starts.
 * draw() submits commands of one vertex layout as one glMultiDrawElementsIndirect: a draw costs
 * the validation after a state change, paid once per multi-draw, not per mesh. Both lists go into
 * the frame's stream ring, the commands bound to the indirect target.
 */
class GLDrawList {
    public:
        GLDrawList();
        ~GLDrawList();

        GLDrawList(const GLDrawList& other) = delete;
        GLDrawList& operator=(const GLDrawList& other) = delete;

        GLDrawList(GLDrawList && other) = delete;
        GLDrawList& operator=(GLDrawList && other) = delete;

    public:
        /// Empty both lists, keeping their capacity.
        void clear();

        /**
         * @brief Write both lists into @p stream.
         *
         * Once per fill, before the first draw(), in the frame it draws in.
         *
         * @param stream The frame's streaming memory (GLAssetMirror::stream).
         */
        void upload(Vkm::OpenGL::StreamRing& stream);

        /**
         * @brief Submit @p count commands from @p first as one multi-draw.
         *
         * @param mesh  Any of the commands' meshes; all share its pool and vertex layout.
         * @param first First command.
         * @param count Commands to draw.
         */
        void draw(const GLMesh& mesh, uint32_t first, uint32_t count) const;

        /// One instance (INSTANCE_OBJECT_BITS) per draw of an object, in the order the commands read them.
        std::vector<uint32_t>& instances() { return m_instances; }
        const std::vector<uint32_t>& instances() const { return m_instances; }

        /// The instances where upload() wrote them; empty before the first.
        const Vkm::OpenGL::BufferSpan& instanceSpan() const { return m_instanceSpan; }

        /// One command per run of instances sharing a mesh.
        std::vector<Vkm::OpenGL::DrawElementsIndirectCommand>& commands() { return m_commands; }
        const std::vector<Vkm::OpenGL::DrawElementsIndirectCommand>& commands() const { return m_commands; }

    private:
        std::vector<uint32_t>                                 m_instances;
        std::vector<Vkm::OpenGL::DrawElementsIndirectCommand> m_commands;

        Vkm::OpenGL::BufferSpan m_instanceSpan;
        Vkm::OpenGL::BufferSpan m_commandSpan;
};

} // namespace Vkm::Engine

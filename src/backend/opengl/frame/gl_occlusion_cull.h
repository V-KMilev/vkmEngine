#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gl_compute_shader.h"
#include "gl_texture.h"
#include "gl_buffer.h"
#include "gl_shader_storage_buffer.h"

#include "asset/gl_mesh.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLInstanceBatcher;
class GLTarget;
struct InstanceDraw;
struct RenderObjects;

/**
 * @brief GPU occlusion culling of the opaque batch, in two phases (RenderSettings::occlusionCulling).
 *
 * The depth prepass draws first what the last frame's cull showed (Set::Seen), then the farthest
 * depth of what it drew is reduced into a pyramid, and every instance's bounds are tested against
 * it: what they show that was hidden before is drawn next (Set::Revealed), and the forward pass
 * draws what they show (Set::Shown). So the frame is exact for itself - the last frame's verdicts
 * only choose what is drawn first, and what they miss the second draw adds - with nothing carried
 * between frames but one verdict an object (the two-phase cull of Haar and Aaltonen, *GPU-Driven
 * Rendering Pipelines*, 2015).
 *
 * A set is the batch's own commands with the instances the cull gives each, counted in by the GPU,
 * so the CPU builds and sorts the batch exactly as without it and reads nothing back. The GPU reads
 * each instance's object from the batch's own list and its world box from the frame's
 * (GLObjectBuffer): the CPU cull's, posed for a posed character and reaching as far as the sea moves
 * a wave. The instances arrive in any order within a run, which an opaque draw does not mind. A
 * viewmodel is always shown: it draws through another projection.
 *
 * Saves the vertex work of what is hidden; the depth test already rejected its pixels. Run
 * prepare() after the batch is built, the phases from the depth prepass. When it pays:
 * docs/guides/engine.md section 4.
 */
class GLOcclusionCull {
    public:
        /// The draw lists the cull fills, a list of the batch's commands each.
        enum class Set : uint32_t {
            Seen     = 0,  ///< What the last cull showed: the prepass's first draw.
            Revealed = 1,  ///< What this one shows that the last did not: the prepass's second.
            Shown    = 2,  ///< Everything this one shows: the forward pass's.
            Count          ///< Sentinel; keep last. The shader reads each through the prelude.
        };

        /**
         * @brief Compile the pyramid and cull programs.
         */
        GLOcclusionCull();
        ~GLOcclusionCull();

        GLOcclusionCull(const GLOcclusionCull& other) = delete;
        GLOcclusionCull& operator=(const GLOcclusionCull& other) = delete;

        GLOcclusionCull(GLOcclusionCull && other) = delete;
        GLOcclusionCull& operator=(GLOcclusionCull && other) = delete;

    public:
        /**
         * @brief Upload this frame's empty sets for @p batch.
         *
         * @param batch   The opaque batch, built and uploaded this frame.
         * @param objects What its instances index.
         * @return Whether there is anything to cull; false leaves the sets unusable this frame.
         */
        bool prepare(const GLInstanceBatcher& batch, const RenderObjects& objects);

        /**
         * @brief Fill Set::Seen: the instances the last cull showed.
         */
        void cullSeen();

        /**
         * @brief Reduce @p scene's depth into the pyramid, then fill Set::Revealed and Set::Shown.
         *
         * @param scene  The target the prepass drew into, its depth now holding Set::Seen.
         * @param width  Its width in pixels.
         * @param height Its height in pixels.
         */
        void cullAgainst(const GLTarget& scene, uint32_t width, uint32_t height);

        /**
         * @brief Submit one of the batch's draws from set @p set, as GLInstanceBatcher::draw would.
         *
         * @param gl   Context the winding goes through.
         * @param draw One of the batch's draws.
         * @param set  Which list.
         */
        void draw(Vkm::GL::Context& gl, const InstanceDraw& draw, Set set) const;

        /**
         * @brief How many instances the last cull gave set @p set: a synchronising read, for tests.
         *
         * @param set Which list.
         * @return Its instances, over every command; 0 before a cull.
         */
        uint32_t countIn(Set set) const;

    private:
        /**
         * @brief Size the pyramid for a scene of @p width by @p height.
         *
         * @param width  Scene width in pixels.
         * @param height Scene height in pixels.
         */
        void ensurePyramid(uint32_t width, uint32_t height);

        /**
         * @brief Reduce @p scene's depth into every level of the pyramid.
         *
         * @param scene The target the prepass drew into.
         */
        void buildPyramid(const GLTarget& scene);

        /// Bind the batch's instances, the verdicts and the sets for a cull dispatch.
        void bindBuffers() const;

    private:
        Vkm::GL::ComputeShader m_pyramidProgram;  ///< Depth -> the farthest-depth pyramid.
        Vkm::GL::ComputeShader m_cullProgram;     ///< Records -> the sets.

        std::vector<DrawCommand> m_commands;  ///< The batch's commands, emptied, once per set.

        /// The batch's instances, where it wrote them.
        Vkm::GL::BufferSpan                           m_source;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_commandBuffer;   ///< Every set's commands.
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_instanceBuffer;  ///< Every set's instances.
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_visibleBuffer;   ///< Per object: the last verdict.

        uint32_t m_instanceCount = 0;  ///< The batch's this frame: each set's share of the instances.
        uint32_t m_commandCount  = 0;  ///< Its commands: each set's share of the commands.

        std::unique_ptr<Vkm::GL::Texture2D> m_pyramid;  ///< Farthest depth, half the scene, every level.
};

} // namespace Vkm::Engine

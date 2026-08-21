#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_pass.h"
#include "data/gl_instance_buffer.h"

#include "data/gl_shadow_data.h"

namespace Vkm::GL {
    class Shader;
}

namespace Vkm::Engine {

/**
 * @brief Renders all shadow depth maps for the frame, ahead of the forward pass.
 *
 * Consumes the plan in GLShadowData (built by the backend): directional cascades
 * and spot maps go into the 2D depth atlas, point lights into cube maps. A
 * single depth-only draw of every castShadows drawable feeds each tile / face.
 * A no-op when the frame has no shadow casters.
 *
 * Skinned casters take a second pair of programs and draw one at a time. The
 * camera batch finds an instance's bones through a storage buffer indexed by the
 * instance slot; that is not available here, because this pass takes its
 * transforms as attributes and gl_InstanceID does not include baseInstance
 * before GL 4.6 - so the palette base arrives as a uniform, and a uniform can
 * only describe one draw. It costs N draws per tile and per cube face for
 * skinned casters alone.
 *
 * A frame that posed nothing never reaches any of that: with no palette the
 * skinned programs are not bound, not given a tile matrix, and not consulted
 * per run, so the pass is the one 1.5 had.
 */
class GLShadowPass : public GLPass {
    public:
        GLShadowPass();
        ~GLShadowPass() override;

        GLShadowPass(const GLShadowPass& other) = delete;
        GLShadowPass& operator=(const GLShadowPass& other) = delete;

        GLShadowPass(GLShadowPass && other) = delete;
        GLShadowPass& operator=(GLShadowPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Fill the 2D depth atlas for directional cascades and spots.
         *
         * Clears the atlas once, then fills one tile per job with the
         * projected-depth shader. A no-op when the frame has no 2D casters.
         */
        void render2D(GLFrameContext& ctx);

        /**
         * @brief Fill each point light's depth cube.
         *
         * Six faces per light, rendered with the distance-depth shader so the
         * forward pass can sample the cube by direction.
         */
        void renderCube(GLFrameContext& ctx);

        /**
         * @brief Draw one tile's or face's pre-culled shadow casters.
         *
         * Submission only: the culling against the light's clip space and the
         * mesh sort both happened on the thread pool (GLShadowData::cullCasters),
         * so this flattens the batch's models into one upload and issues one
         * instanced depth draw per mesh run - one draw per caster on a skinned
         * run. The caller has already given each program that will draw this
         * tile's matrices; which of them is bound is decided here, per run.
         *
         * @param ctx     The frame context, for the GL view and the caster list.
         * @param batch   The tile's / face's surviving caster indices, mesh-sorted.
         * @param program The static depth program for this tile / face.
         * @param skinned The skinned one, for casters the frame posed, or null
         *                when it posed none - then every run is instanced.
         */
        void renderCasters(GLFrameContext& ctx, const ShadowCasterBatch& batch,
                           Vkm::GL::Shader& program, Vkm::GL::Shader* skinned);

        /**
         * @brief Make @p program current, unless it already is.
         *
         * A program has to be bound to be given a uniform, and this pass hands
         * matrices to one for every atlas tile and every cube face - eighteen
         * of them in a lit scene, each followed by draws. Binding per tile the
         * way that reads most naturally makes glUseProgram the largest thing
         * the pass does when only one program is ever used, so what is current
         * is tracked across the whole pass instead of assumed per tile.
         *
         * @param program The program to make current.
         */
        void bindProgram(Vkm::GL::Shader& program);

    private:
        std::unique_ptr<Vkm::GL::Shader> m_depth2D;            ///< Projected depth (cascades + spots).
        std::unique_ptr<Vkm::GL::Shader> m_depthCube;          ///< Linear distance depth (point faces).
        std::unique_ptr<Vkm::GL::Shader> m_depth2DSkinned;     ///< The same, posed.
        std::unique_ptr<Vkm::GL::Shader> m_depthCubeSkinned;   ///< The same, posed.

        const Vkm::GL::Shader*   m_bound = nullptr;  ///< The program that is current, reset each execute().
        Vkm::GL::InstanceBuffer  m_instances;  ///< Per-caster model matrices (loc 4-7).
        std::vector<glm::mat4>   m_models;     ///< Flattened models of every surviving caster this tile/face.
};

} // namespace Vkm::Engine

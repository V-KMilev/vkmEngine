#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"

#include "frame/draw/gl_draw_list.h"
#include "frame/light/gl_shadow_plan.h"

namespace Vkm::Engine {

class GLMaterial;
class GLAssetMirror;

/**
 * @brief Renders all shadow depth maps for the frame, ahead of the forward pass.
 *
 * Executes GLShadowPlan's plan: what moves under the sun and spots into the 2D atlas, point lights
 * into cubes, and what a held layer is to show of the fixed casters into the layer.
 * A no-op with no shadowed light; a tile or face not held is cleared and drawn, casters or none.
 *
 * Each tile's culled objects go into one shared GLDrawList, a command per run sharing a mesh.
 * Runs come grouped by program and layout (ShadowRun::key), so a tile is one multi-draw per
 * both. A frame whose tiles are all held uploads nothing. A caster whose material cuts its
 * shadow (GLMaterial::cutsItsShadow) draws per material through the masked program; a surface
 * that should cast none turns casting off on its Mesh.
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
         * @brief Where one tile's or face's draws sit in m_draws; `first` is HELD for one not drawn.
         */
        struct TileDraws {
            uint32_t first = 0;
            uint32_t end   = 0;
        };

        /**
         * @brief One multi-draw of a tile: runs sharing a program, a cut material and a vertex layout.
         */
        struct ShadowDraw {
            const GLMesh*     mesh     = nullptr;  ///< The first run's mesh; every run's shares its vertex
                                                   ///< layout.
            const GLMaterial* material = nullptr;  ///< The material every run is cut by
                                                   ///< (GLMaterial::cutsItsShadow); null for solid casters.
            uint32_t          program  = 0;        ///< Index into m_programs.
            uint32_t          first    = 0;        ///< First command in m_list.
            uint32_t          count    = 0;        ///< Commands.
        };

        /**
         * @brief One of the pass's depth programs, and the tile it last received a matrix for.
         */
        struct DepthProgram     {
            Vkm::OpenGL::Shader shader;
            bool                cuts = false;  ///< Cuts casters by their alpha, so takes the dither's shift.
            uint64_t            tile = 0;      ///< The beginTile() whose matrix and shift it holds.
        };

    private:
        /**
         * @brief Fill the 2D depth atlas: what moves under the sun, and spots.
         *
         * One tile per job not held; what the sun lights is drawn with its depth clamped.
         *
         * @param ctx For the shadow plan, its batches and the atlas.
         */
        void render2D(GLFrameContext& ctx);

        /**
         * @brief Draw the rectangles of the held layers the plan asks for, depth clamped.
         *
         * @param ctx For the shadow plan, its batches and the atlas.
         */
        void renderHeld(GLFrameContext& ctx);

        /**
         * @brief Fill each point light's depth cube.
         *
         * Six ordinary perspective depth maps per light, drawn by the atlas's programs; a reader
         * rebuilds a face's depth from the major axis of its direction (shaders/opengl/include/shadows.glsl).
         *
         * @param ctx For the shadow plan, its batches and the atlas.
         */
        void renderCube(GLFrameContext& ctx);

        /**
         * @brief Start drawing a tile or face against @p lightVP.
         *
         * Only recorded: a program gets it when a run of this tile first binds it, so a tile with
         * no caster cut by its alpha never binds the masked program.
         *
         * @param lightVP     World to the light's clip space for this tile or face.
         * @param ditherShift From the tile's texels to the light's fixed grid, which a cut
         *                    caster's dither is keyed to (Shadow2DJob::ditherOrigin); zero for a
         *                    face.
         */
        void beginTile(const glm::mat4& lightVP, const glm::vec2& ditherShift);

        /**
         * @brief Write and upload the drawn tiles' commands, and bind the objects they index.
         *
         * @param ctx For the plan and the caster list.
         * @return False when every tile and face is held.
         */
        bool uploadCasters(GLFrameContext& ctx);

        /**
         * @brief Turn one tile's or face's pre-culled casters into commands and draws.
         *
         * Culling and grouping ran on the thread pool (GLShadowPlan::cullCasters), so this is one
         * command per run. The program (posed or not, cut or not) is chosen here from key and
         * material; consecutive runs agreeing on program, cut material and layout are one draw.
         *
         * @param ctx   For the asset mirror, the frame's draws and the skin palette.
         * @param batch The surviving caster indices, grouped.
         * @param first Where the batch's indices start in the instance list.
         * @return Where the tile's draws sit in m_draws.
         */
        TileDraws addCasters(const GLFrameContext& ctx, const ShadowCasterBatch& batch, uint32_t first);

        /**
         * @brief Submit one tile's or face's draws against the matrix beginTile() recorded.
         *
         * @param mirror For a cut material's maps.
         * @param tile   Where its draws sit in m_draws.
         */
        void drawTile(const GLAssetMirror& mirror, const TileDraws& tile);

        /**
         * @brief Make @p program current, and give it this tile's matrix, and a cutting one the
         *        dither's shift, if it lacks them.
         *
         * Matrices go out per tile and face, so the current program is tracked across the pass
         * rather than rebound per tile.
         *
         * @param program The program to make current.
         */
        void useProgram(DepthProgram& program);

    private:
        DepthProgram m_programs[4];  ///< Indexed cut * 2 + posed.

        const Vkm::OpenGL::Shader* m_bound = nullptr;    ///< The program that is current, reset each
                                                         ///< execute().
        glm::mat4                  m_lightVP{1.0f};      ///< The tile being drawn's, as it rasterizes.
        glm::vec2                  m_ditherShift{0.0f};  ///< Its beginTile() dither shift.

        uint64_t m_tile       = 0;  ///< Counts beginTile() calls; never reset, so no program holds a stale
                                    ///< one.
        uint32_t m_tilesDrawn = 0;  ///< Tiles and faces drawn this frame; the rest were held.

        GLDrawList              m_list;       ///< The drawn tiles' and faces' caster lists, end to end, and a
                                              ///< command per run.
        std::vector<ShadowDraw> m_draws;      ///< Every drawn tile's draws, tile after tile.
        std::vector<TileDraws>  m_tiles2D;    ///< Each 2D job's draws.
        std::vector<TileDraws>  m_tilesCube;  ///< Each cube face's draws, six per job.
        std::vector<TileDraws>  m_rectsHeld;  ///< Each held job's draws.
};

} // namespace Vkm::Engine

#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gl_shader.h"

#include "gl_pass.h"

#include "frame/gl_draw_list.h"
#include "frame/gl_shadow_data.h"

namespace Vkm::Engine {

class GLMaterial;
class GLView;

/**
 * @brief Renders all shadow depth maps for the frame, ahead of the forward pass.
 *
 * Executes GLShadowData's plan: cascades and spots into the 2D atlas, point lights into cubes.
 * A no-op with no shadowed light; a tile or face not held is cleared and drawn, casters or none.
 *
 * Each tile's culled objects go into one shared GLDrawList, a command per run sharing a mesh.
 * Runs come grouped by program and layout (ShadowRun::key), so a tile is one multi-draw per
 * both. A frame whose tiles are all held uploads nothing. A masked caster's runs draw per
 * material through a cutting program; a transparent caster draws solid, and a surface that
 * should cast none turns casting off on its Mesh.
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
         * @brief One multi-draw of a tile: runs sharing a program, a cutout and a vertex layout.
         */
        struct ShadowDraw {
            /// The first run's mesh; every run's shares its vertex layout.
            const GLMesh*     mesh     = nullptr;
            /// The cutout every run is cut by; null for solid casters.
            const GLMaterial* material = nullptr;
            uint32_t          program  = 0;        ///< Index into m_programs.
            uint32_t          first    = 0;        ///< First command in m_list.
            uint32_t          count    = 0;        ///< Commands.
        };

        /**
         * @brief One of the pass's depth programs, and the tile it last received a matrix for.
         */
        struct DepthProgram {
            Vkm::GL::Shader shader;
            uint64_t        tile = 0;  ///< The beginTile() whose matrix it holds.
        };

    private:
        /**
         * @brief Fill the 2D depth atlas for directional cascades and spots.
         *
         * One tile per job not held; a cascade is drawn with its depth clamped.
         *
         * @param ctx For the shadow plan, its batches and the atlas.
         */
        void render2D(GLFrameContext& ctx);

        /**
         * @brief Fill each point light's depth cube.
         *
         * Six ordinary perspective depth maps per light, drawn by the atlas's programs; a reader
         * rebuilds a face's depth from the major axis of its direction (shaders/shadows.glsl).
         *
         * @param ctx For the shadow plan, its batches and the atlas.
         */
        void renderCube(GLFrameContext& ctx);

        /**
         * @brief Start drawing a tile or face against @p lightVP.
         *
         * Only recorded: a program gets it when a run of this tile first binds it, so a tile with
         * no cutout never binds the masked program.
         *
         * @param lightVP World to the light's clip space for this tile or face.
         */
        void beginTile(const glm::mat4& lightVP);

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
         * Culling and grouping ran on the thread pool (GLShadowData::cullCasters), so this is one
         * command per run. The program (posed or not, cut or not) is chosen here from key and
         * material; consecutive runs agreeing on program, cutout and layout are one draw.
         *
         * @param ctx   For the GL view and the caster list.
         * @param batch The surviving caster indices, grouped.
         * @param first Where the batch's indices start in the instance list.
         * @return Where the tile's draws sit in m_draws.
         */
        TileDraws addCasters(const GLFrameContext& ctx, const ShadowCasterBatch& batch, uint32_t first);

        /**
         * @brief Submit one tile's or face's draws against the matrix beginTile() recorded.
         *
         * @param glView For a cutout's maps.
         * @param tile   Where its draws sit in m_draws.
         */
        void drawTile(const GLView& glView, const TileDraws& tile);

        /**
         * @brief Make @p program current, and give it this tile's matrix if it lacks it.
         *
         * A program must be bound to take a uniform, and matrices go out per tile and face, so the
         * current program is tracked across the pass rather than rebound per tile.
         *
         * @param program The program to make current.
         */
        void useProgram(DepthProgram& program);

    private:
        DepthProgram m_programs[4];  ///< Indexed masked * 2 + posed.

        const Vkm::GL::Shader* m_bound = nullptr;  ///< The program that is current, reset each execute().
        glm::mat4              m_lightVP{1.0f};    ///< The tile being drawn's matrix.
        /// Counts beginTile() calls; never reset, so no program holds a stale one.
        uint64_t               m_tile = 0;
        uint32_t               m_tilesDrawn = 0;   ///< Tiles and faces drawn this frame; the rest were held.

        /// The drawn tiles' and faces' caster lists, end to end, and a command per run.
        GLDrawList              m_list;
        std::vector<ShadowDraw> m_draws;      ///< Every drawn tile's draws, tile after tile.
        std::vector<TileDraws>  m_tiles2D;    ///< Each 2D job's draws.
        std::vector<TileDraws>  m_tilesCube;  ///< Each cube face's draws, six per job.
};

} // namespace Vkm::Engine

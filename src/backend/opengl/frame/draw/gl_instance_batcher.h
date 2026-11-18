#pragma once

#include <cstdint>
#include <vector>

#include <GL/glew.h>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

#include "frame/draw/gl_draw_list.h"

namespace Vkm::OpenGL {
    class Context;
}

namespace Vkm::Engine {

class GLAssetMirror;
struct RenderObjects;

/**
 * @brief Turn the winding for a draw that is mirrored.
 *
 * @param gl       The context, whose winding is turned.
 * @param mirrored Whether the draw's instances are mirrored; false turns nothing.
 * @return The winding to put back after the draw.
 */
GLenum turnForMirror(Vkm::OpenGL::Context& gl, bool mirrored);

/**
 * @brief Which of an object's meshes a build draws.
 */
enum class MeshOf {
    Level,  ///< The level of detail the cull picked for the eye (ObjectDraw::mesh), whole.
    /// As the eye sees it (RenderObjects::fades): Level on the share of its samples its fade leaves
    /// it, and across a hand-over the next level on the rest, an instance each.
    Eye,
    Base    ///< The mesh it was given (RenderObjects::bases), as what is baked from its shape draws it.
};

/**
 * @brief One multi-draw: consecutive runs sharing a program, material and vertex layout.
 *
 * A run is the instances of one (skinned, water, mirrored, material, mesh) key, whatever their fades;
 * `count` runs' commands follow from `first` in the batcher's list.
 */
struct InstanceDraw {
    const GLMesh*  mesh     = nullptr;  ///< The first run's mesh; every run's shares its vertex layout.
    MaterialHandle material;
    uint32_t       first    = 0;
    uint32_t       count    = 0;

    bool           skinned  = false;  ///< Whether this draw goes through the skinned program; leads the sort
                                      ///< key, so a batch switches program once. Not just a skin stream: the
                                      ///< instance needs a pose this frame too, and one outside any rig draws
                                      ///< statically, in bind pose.

    bool           mirrored = false;  ///< Whether its instances' transforms mirror them (a negative
                                      ///< determinant). Mirroring reverses a triangle's winding, so these
                                      ///< draw with clockwise front faces, or back-face culling would show
                                      ///< their inside.

    bool           water    = false;  ///< Whether it draws as water (ObjectDraw::water), through the water
                                      ///< programs (GLForwardPrograms::water); a posed run never does.
};

/**
 * @brief Groups a list of objects into instanced draws.
 *
 * build*() lays object indices out in run order, writes a command per run, and returns the
 * draws, runs merged wherever nothing need be bound between them. Transforms come from the
 * frame's GLObjectBuffer, which the caller binds, then material and program per draw.
 * buildGrouped suits order-independent buckets; buildSequential keeps input order for
 * back-to-front transparents. The draw list is valid only until the next build*().
 */
class GLInstanceBatcher {
    public:
        GLInstanceBatcher()  = default;
        ~GLInstanceBatcher() = default;

        GLInstanceBatcher(const GLInstanceBatcher& other) = delete;
        GLInstanceBatcher& operator=(const GLInstanceBatcher& other) = delete;

        GLInstanceBatcher(GLInstanceBatcher && other) = delete;
        GLInstanceBatcher& operator=(GLInstanceBatcher && other) = delete;

    public:
        /**
         * @brief Sort by program, winding, material and mesh, and merge identical instances.
         *
         * A program is the skinned bit and the kind, plain, full or layered by the material, or
         * water by the object, so a pass that picks its program by them binds each once.
         *
         * @param list    Indices of the objects to batch.
         * @param objects The objects @p list indexes.
         * @param mirror  GPU mirror the mesh handles resolve against.
         * @param bones   Matrices in the frame's palette. Zero means nothing posed: no mesh is
         *                resolved for the program bit, so a scene without characters pays nothing.
         * @param meshes  Which of each object's meshes is drawn; Eye and Base only for objects the
         *                cull wrote, which fills RenderObjects::fades and RenderObjects::bases.
         * @return The draws, in batch order.
         */
        const std::vector<InstanceDraw>& buildGrouped(
            const std::vector<uint32_t>& list,
            const RenderObjects& objects,
            const GLAssetMirror& mirror,
            uint32_t bones,
            MeshOf meshes = MeshOf::Level
        );

        /**
         * @brief One instance per run, input order preserved.
         *
         * @param list    Indices of the objects to batch, already in draw order.
         * @param objects The objects @p list indexes.
         * @param mirror  GPU mirror the mesh handles resolve against.
         * @param bones   Matrices in the frame's palette; see buildGrouped().
         * @param meshes  Which of each object's meshes is drawn: Level or Eye, whose next level
         *                follows the one it fades from.
         * @return The draws, in input order.
         */
        const std::vector<InstanceDraw>& buildSequential(
            const std::vector<uint32_t>& list,
            const RenderObjects& objects,
            const GLAssetMirror& mirror,
            uint32_t bones,
            MeshOf meshes = MeshOf::Level
        );

        /**
         * @brief Submit one draw: one multi-draw over its runs' commands.
         *
         * A mirrored draw turns the front face clockwise for itself and back, so a caller's
         * winding state is what it set.
         *
         * @param gl   Context the winding goes through.
         * @param draw The draw to submit.
         */
        void draw(Vkm::OpenGL::Context& gl, const InstanceDraw& draw) const;

        /**
         * @brief The draws from the most recent build*(), for a consumer that did not build them.
         *
         * Lets a batch built once per frame be drawn by several passes; invalidated by the next
         * build*().
         *
         * @return The current draw list.
         */
        const std::vector<InstanceDraw>& draws() const { return m_draws; }

        /// The instances and commands the draws slice, from the most recent build*().
        const GLDrawList& list() const { return m_list; }

    private:
        /**
         * @brief One instance's place in the sort: its key, and the instance.
         *
         * The sort compares adjacent keys rather than following indices into the objects. The
         * key is the whole order (program, material, mesh), so equal keys are identical draws.
         */
        struct SortKey {
            uint64_t key;
            uint32_t instance;  ///< Its object, and how it fades (INSTANCE_OBJECT_BITS).

            bool operator==(const SortKey& other) const {
                return key == other.key && instance == other.instance;
            }
        };

        /**
         * @brief Start a build: drop the last one.
         *
         * Shared by both modes, so neither carries the last build's draws over.
         *
         * @param list Indices of the objects about to be batched.
         * @return False when @p list is empty and there is nothing to build.
         */
        bool beginBuild(const std::vector<uint32_t>& list);

        /**
         * @brief Close a run: write its command, and join it to the last draw or start the next.
         *
         * @param mesh     The run's mesh.
         * @param material The run's material.
         * @param skinned  Whether the run draws through the skinned program.
         * @param mirrored Whether its instances' transforms mirror them.
         * @param water    Whether it draws as water.
         * @param first    Where its instances start in the list.
         * @param count    How many it has.
         */
        void addRun(
            const GLMesh& mesh,
            const MaterialHandle& material,
            bool skinned,
            bool mirrored,
            bool water,
            uint32_t first,
            uint32_t count
        );

    private:
        std::vector<InstanceDraw> m_draws;
        std::vector<SortKey>      m_keys;    ///< The sort itself, one entry per input.
        std::vector<SortKey>      m_listed;  ///< The last build's keys in its list's order: a build keyed
                                             ///< alike keeps m_keys as sorted.
        std::vector<SortKey>      m_keying;  ///< This build's keys in its list's order.
        GLDrawList                m_list;    ///< Every instance's object index, in run order, and a command
                                             ///< per run.
};

} // namespace Vkm::Engine

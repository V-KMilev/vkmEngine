#pragma once

#include <cstdint>
#include <vector>

#include "resource/asset/material_asset.h"

#include "frame/gl_draw_list.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLView;
struct RenderObjects;

/**
 * @brief One multi-draw: consecutive runs sharing a program, material and vertex layout.
 *
 * A run is the instances of one (skinned, mirrored, material, mesh) key; `count` runs' commands follow
 * from `first` in the batcher's list.
 */
struct InstanceDraw {
    const GLMesh*  mesh     = nullptr;  ///< The first run's mesh; every run's shares its vertex layout.
    MaterialHandle material;
    uint32_t       first    = 0;
    uint32_t       count    = 0;

    /**
     * @brief Whether this draw goes through the skinned program.
     *
     * Leads the sort key, so a batch switches program once. Not just "the mesh has a skin
     * stream": the instance needs a pose this frame too, and one outside any rig draws
     * statically, in bind pose.
     */
    bool           skinned  = false;

    /**
     * @brief Whether its instances' transforms mirror them (a negative determinant).
     *
     * Mirroring reverses a triangle's winding, so these draw with clockwise front faces, or
     * back-face culling would show their inside.
     */
    bool           mirrored = false;
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
         * @brief Sort by (skinned, mirrored, material, mesh) and merge identical instances.
         *
         * @param list    Indices of the objects to batch.
         * @param objects The objects @p list indexes.
         * @param view    GPU mirror the mesh handles resolve against.
         * @param bones   Matrices in the frame's palette. Zero means nothing posed: no mesh is
         *                resolved for the program bit, so a scene without characters pays nothing.
         * @return The draws, in batch order.
         */
        const std::vector<InstanceDraw>& buildGrouped(
            const std::vector<uint32_t>& list,
            const RenderObjects& objects,
            const GLView& view,
            uint32_t bones
        );

        /**
         * @brief One instance per run, input order preserved.
         *
         * @param list    Indices of the objects to batch, already in draw order.
         * @param objects The objects @p list indexes.
         * @param view    GPU mirror the mesh handles resolve against.
         * @param bones   Matrices in the frame's palette; see buildGrouped().
         * @return The draws, in input order.
         */
        const std::vector<InstanceDraw>& buildSequential(
            const std::vector<uint32_t>& list,
            const RenderObjects& objects,
            const GLView& view,
            uint32_t bones
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
        void draw(Vkm::GL::Context& gl, const InstanceDraw& draw) const;

        /**
         * @brief The draws from the most recent build*(), for a consumer that did not build them.
         *
         * Lets a batch built once per frame be drawn by several passes; invalidated by the next
         * build*().
         *
         * @return The current draw list.
         */
        const std::vector<InstanceDraw>& draws() const { return m_draws; }

    private:
        /**
         * @brief One object's place in the sort: its key, and its index.
         *
         * The sort compares adjacent keys rather than following indices into the objects. The
         * key is the whole order (program, material, mesh), so equal keys are identical draws.
         */
        struct SortKey {
            uint64_t key;
            uint32_t object;
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
         * @brief Whether an object draws through the skinned program this frame.
         *
         * The GPU layout must carry a skin stream and the frame must have posed the object. Asked
         * only when the frame has a palette.
         *
         * @param object  Index of the object.
         * @param objects The objects it indexes.
         * @param view    GPU mirror the mesh handle resolves against.
         * @return True when the skinned program draws it.
         */
        bool drawsSkinned(uint32_t object, const RenderObjects& objects, const GLView& view) const;

        /**
         * @brief Close a run: write its command, and join it to the last draw or start the next.
         *
         * @param mesh     The run's mesh.
         * @param material The run's material.
         * @param skinned  Whether the run draws through the skinned program.
         * @param mirrored Whether its instances' transforms mirror them.
         * @param first    Where its instances start in the list.
         * @param count    How many it has.
         */
        void addRun(
            const GLMesh& mesh,
            const MaterialHandle& material,
            bool skinned,
            bool mirrored,
            uint32_t first,
            uint32_t count
        );

    private:
        std::vector<InstanceDraw> m_draws;
        std::vector<SortKey>      m_keys;  ///< The sort itself, one entry per input.
        /// Every instance's object index, in run order, and a command per run.
        GLDrawList                m_list;
};

} // namespace Vkm::Engine

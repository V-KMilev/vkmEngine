#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief What the server's slots name on this end, so a client can make entities of its own.
 *
 * A slot is an entity's name on the wire: the server's slot, always. The world both ends loaded
 * is the same entities at the same slots, which adoptWorld records as each naming itself.
 * Whatever the server names that this end has not heard of yet is made where there is room - at
 * that same slot when it is free here, which is the common case and keeps the ends' slot order
 * alike, and anywhere else when an entity of this end's own is standing in it. So an effect, a
 * menu or a marker a client makes for itself is never written over by the server.
 */
class NetNames {
    public:
        NetNames() = default;
        ~NetNames() = default;

        NetNames(const NetNames& other) = default;
        NetNames& operator=(const NetNames& other) = default;

        NetNames(NetNames && other) = default;
        NetNames& operator=(NetNames && other) = default;

    public:
        /**
         * @brief The world as it was loaded, before anything ran: every live entity names its slot.
         *
         * @param scene The world both ends loaded from one file, or built alike.
         */
        void adoptWorld(const Scene& scene);

        /**
         * @brief What the server's @p slot names here, if this end has it.
         *
         * @param scene The world it is in.
         * @param slot  The server's slot.
         * @return The entity, or a null id.
         */
        EntityId find(const Scene& scene, uint32_t slot) const;

        /// Whether a slot of the server's names @p entity here: if not, it is this end's own.
        bool names(const Scene& scene, EntityId entity) const {
            return entity && find(scene, entity.slot()) == entity;
        }

        /**
         * @brief What the server's @p slot names here, made if this end has nothing for it yet.
         *
         * @param scene The world it goes in.
         * @param slot  The server's slot.
         * @return The entity, or a null id for a slot past what a peer may name.
         */
        EntityId claim(Scene& scene, uint32_t slot);

        /**
         * @brief The server says @p slot is gone: destroy what it named here, and its subtree.
         *
         * @param scene The world it is in.
         * @param slot  The server's slot.
         */
        void forget(Scene& scene, uint32_t slot);

        /**
         * @brief Forget what this session named; the world as loaded is kept for the next.
         *
         * What the session made is kept for sweepEnded to destroy: a session's entities leave
         * with it, or a client joining again would find them standing in its slots and build
         * the server's a second time beside them.
         */
        void endSession();

        /**
         * @brief Destroy what the session last ended had made, and their subtrees, if @p scene is
         *        still the world they were made in; a world replaced since keeps nothing of them.
         *
         * @param scene The world.
         */
        void sweepEnded(Scene& scene);

        /// Forget everything, the world as loaded too: it has been replaced.
        void clear();

    private:
        std::unordered_map<uint32_t, EntityId> m_world;    ///< The world as loaded, by slot.
        std::unordered_map<uint32_t, EntityId> m_claimed;  ///< What the server made since, by its slot.
        std::vector<EntityId>                  m_ended;    ///< What the last session made, to sweep.
        uint64_t                               m_epoch = 0;  ///< The world all of it names entities of.
};

} // namespace Vkm::Engine

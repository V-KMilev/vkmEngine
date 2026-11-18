#pragma once

#include <cstdint>
#include <vector>

#include "ecs/component/scene/transform.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class NetSession;
class Scene;

/**
 * @brief Draws every moving body where it is now, not where the last tick left it.
 *
 * Bodies move on ticks, and a frame drawn between two would show each where the last left it: against
 * a camera that moves every frame they step. Engine::run carries them forward before the frame's
 * stages and puts them back after, so the frame - behaviors, cameras, sound, the renderer - sees each
 * carried by its velocities, and every tick simulates from the pose the last one left. A bone socket
 * goes back too: the frame places it on the pose drawn for its moment (BoneSocketSystem), and a tick
 * reads it where the last tick's pose put it. A tick is not waited for, so nothing pays latency
 * (docs/reference/physics.md).
 */
class DrawnAhead {
    public:
        DrawnAhead();
        ~DrawnAhead();

        DrawnAhead(const DrawnAhead& other) = delete;
        DrawnAhead& operator=(const DrawnAhead& other) = delete;

        DrawnAhead(DrawnAhead && other) = delete;
        DrawnAhead& operator=(DrawnAhead && other) = delete;

    public:
        /**
         * @brief Carry each moving body @p ahead seconds past its last tick, by its velocities, and
         *        keep where the last tick placed each bone socket.
         *
         * A body this end simulates, awake and moving, with no parent: a child moves with what it
         * hangs from, and one that is not simulated here is placed by NetSession::interpolate.
         *
         * @param scene The world about to be drawn.
         * @param net   Says which bodies this end simulates.
         * @param ahead Seconds since the last tick; zero carries nothing.
         */
        void carry(Scene& scene, const NetSession& net, float ahead);

        /**
         * @brief Put every carried body back as its last tick left it, but for what the frame wrote,
         *        and every bone socket where the last tick placed it.
         *
         * A position or a rotation the frame wrote over the carried one - a teleport, a drag in the
         * editor - is kept, and the next tick moves the body from there. A socket's Transform is
         * BoneSocketSystem's output, so what the frame wrote over it goes, unless the frame took its
         * BoneSocket away or hung it from another parent: a dropped item stays where it was dropped.
         *
         * @param scene The world carry() carried.
         */
        void putBack(Scene& scene);

    private:
        struct Carried {
            EntityId  entity;
            Transform tick;   ///< As the last tick left it.
            Transform drawn;  ///< As carry() left it.
        };

        struct Placed {
            EntityId  entity;
            EntityId  parent;  ///< What it hung from as carry() ran.
            Transform tick;    ///< Where the last tick's pose put it.
        };

    private:
        std::vector<Carried> m_carried;
        std::vector<Placed>  m_sockets;
        uint64_t             m_epoch = 0;  ///< Scene::epoch() as carry() ran.
};

} // namespace Vkm::Engine

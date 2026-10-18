#pragma once

#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {
    class NetSession;
    class Scene;
}

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief The characters this scene was authored with, in a fixed order.
 *
 * Players are handed one of these, not one built at join time: a character
 * built on the server after load would exist there only, and the client would
 * draw nothing. Authored ones sit at the same slots on both ends.
 *
 * @param scene Scene to search; a character has a CharacterController and a ScriptComponent.
 * @return The characters, sorted by slot so every machine and run agrees.
 */
std::vector<EntityId> authoredCharacters(Scene& scene);

/**
 * @brief Whether @p entity is the character the player drives when there is no session.
 *
 * Offline, isMine() and isSimulated() are true for every entity, so without a
 * seat every character would move as one. The seat is the first authored one.
 *
 * @param scene Scene holding the characters.
 * @param entity Entity to ask about.
 * @return True for the first authored character only.
 */
bool isOfflineSeat(Scene& scene, EntityId entity);

/**
 * @brief Whether @p entity is a character nobody plays here: offline, every one but the seat.
 *
 * @param scene  Scene holding the characters.
 * @param net    The session; in one, the session says who plays what.
 * @param entity Entity to ask about.
 * @return True offline for every entity but the seat; false in a session.
 */
bool sitsOutOffline(Scene& scene, NetSession& net, EntityId entity);

/**
 * @brief The character this end's player drives: the offline seat, or the session's own entity.
 *
 * @param scene Scene holding the characters.
 * @param net   The session, offline or not.
 * @return The character, or a null id on a server or before a client is seated.
 */
EntityId localCharacter(Scene& scene, NetSession& net);

/**
 * @brief A player takes @p character: it starts a game of its own, with a fresh LabScore.
 *
 * The authority's: called as a player joins, and offline for the seat as play starts.
 *
 * @param scene     Scene holding the character.
 * @param character The character taken.
 */
void seatPlayer(Scene& scene, EntityId character);

} // namespace Lab

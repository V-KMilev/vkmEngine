#pragma once

#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

#include "vkm_network/bit_stream.h"

#include "base/math/axes.h"
#include "ecs/entity.h"

namespace Vkm::Engine {
    class Scene;
}

namespace Lab {

/**
 * @brief How a shot ended, for every end to show it the same way.
 */
enum class LabShot : uint8_t {
    Miss,    ///< Nothing within range.
    World,   ///< Scenery, which keeps a scorch mark.
    Prop,    ///< Something that moves, which keeps none.
    Player,  ///< A character, which it knocks down.
};

/**
 * @brief Something the server counts and every end shows: a switch's presses, a jump pad's launches.
 *
 * The authority adds it, at the onStart of the behavior that counts, and alone writes it; the rest
 * are told. Each end acts on the count rising as it sees it (CountWatch).
 */
struct LabTally {
    uint32_t count = 0;  ///< Times it happened.
};

/**
 * @brief One player's game, kept on their character: their shots and which tokens they found.
 *
 * The authority gives a character a fresh one when a player takes it (seatPlayer) and alone writes
 * it. The last shot travels as it landed, since only the server's rewound world knows where that was.
 */
struct LabScore {
    uint32_t  shots      = 0;                            ///< Shots fired; a rise is a shot to show.
    glm::vec3 lastHit{0.0f};                             ///< Where the last shot struck, or where it ran out.
    glm::vec3 lastNormal = Vkm::Engine::Math::WORLD_UP;  ///< The surface it struck, facing the shooter.
    LabShot   lastShot   = LabShot::Miss;

    uint32_t tokens = 0;  ///< A bit for each LabToken found, by its `index`.
};

inline constexpr int TOKEN_BITS = std::numeric_limits<decltype(LabScore::tokens)>::digits;
    ///< The bits LabScore::tokens has room for: how many LabTokens a scene can hold.

void netEncode(const LabScore& value, Vkm::Network::BitWriter& out);
void netDecode(LabScore& value, Vkm::Network::BitReader& in);

/**
 * @brief Add the lab's own components to what replicates; called from vkmSetupNetwork.
 */
void registerLabNetTypes();

/**
 * @brief Report, as an authoring error, an entity whose components never reach a client.
 *
 * A silenced entity is never described (netSilence), so a LabTally on it would count on the server
 * alone. A body authored Kinematic is described; a Static one is not.
 *
 * @param scene  World holding the entity.
 * @param entity Entity whose count every end shows.
 * @param who    The behavior that counts on it, named in the report.
 */
void reportIfSilenced(Vkm::Engine::Scene& scene, Vkm::Engine::EntityId entity, const char* who);

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabTally)
    VKM_F(count)
VKM_REFLECT_END()

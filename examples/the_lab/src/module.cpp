// The module's entries (system/script/module/module_entry.h): the lab's behaviors register through their
// VKM_BEHAVIOR blocks, and its players through vkmSetupNetwork.
#include <vector>

#include "system/script/behavior_api.h"

#include "lab_seat.h"
#include "lab_state.h"

VKM_GAME_MODULE()

// Besides the bodies the engine replicates, the lab's own: what the server counts (LabTally) and
// each player's score (LabScore).
VKM_MODULE_ENTRY
void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    using namespace Vkm::Engine;

    Lab::registerLabNetTypes();

    // The session holds these callbacks, so the reference they keep to it holds too. A character a
    // player leaves stays, score and all: it is scene content, and seatPlayer starts the next afresh.
    session.onSpawn(
        [&session](Scene& scene, ResourceManager&, PlayerId id) -> EntityId {
            const std::vector<EntityId> characters = Lab::authoredCharacters(scene);
            for (EntityId character : characters) {
                if (session.isDriven(character)) continue;
                Lab::seatPlayer(scene, character);
                const glm::vec3 at = scene.get<Transform>(character).position;
                LOG_INFO("Player %u takes the character at %.1f, %.1f, %.1f", id, at.x, at.y, at.z);
                return character;
            }
            LOG_INFO(
                "Player %u has nowhere to play: this scene holds %zu characters and all of them are taken",
                id,
                characters.size()
            );
            return EntityId{};
        },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
}

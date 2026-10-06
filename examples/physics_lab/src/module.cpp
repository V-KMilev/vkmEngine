#define VKM_LOG_CATEGORY "LAB"

#include <memory>
#include <set>

#include "logger.h"

#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "system/script/behavior_registry.h"

#include "system/script/module_entry.h"

#include "lab_pause_menu.h"
#include "lab_thrower.h"
#include "lab_walker.h"

// What each entry is for is documented in system/script/module_entry.h.
VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

// No vkmBuildScene: the world is authored, and project.json's entryScene names it.
VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    using namespace Lab;
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<LabWalker, LabThrower, LabPauseMenu>();
}

// No component types to register: only transforms and bodies replicate, which the
// engine registers itself.
VKM_MODULE_ENTRY
void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    using namespace Vkm::Engine;

    auto taken = std::make_shared<std::set<uint32_t>>();

    session.onSpawn(
        [taken](Scene& scene, ResourceManager&, PlayerId id) -> EntityId {
            for (EntityId character : Lab::authoredCharacters(scene)) {
                if (taken->count(character.slot())) continue;
                taken->insert(character.slot());
                const glm::vec3 at = scene.get<Transform>(character).position;
                LOG_INFO("Player %u takes the character at %.1f, %.1f, %.1f", id, at.x, at.y, at.z);
                return character;
            }
            LOG_INFO(
                "Player %u has nowhere to play: this scene holds %zu characters and all of them are taken",
                id,
                Lab::authoredCharacters(scene).size()
            );
            return EntityId{};
        },
        [taken](Scene&, ResourceManager&, PlayerId, EntityId entity) {
            // The character stays: it is scene content, and the next player takes this seat.
            taken->erase(entity.slot());
        }
    );
}

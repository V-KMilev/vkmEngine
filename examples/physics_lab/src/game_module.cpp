#define VKM_LOG_CATEGORY "LAB"

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "logger.h"

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "system/script/script_component.h"
#include "system/script/behavior_registry.h"

#include "system/script/module_entry.h"

#include "lab_walker.h"

namespace {

/**
 * @brief The characters this scene was authored with, in a fixed order.
 *
 * Players are handed one of these rather than given one built at join time,
 * and that is the whole reason two people see each other. A character here is
 * twenty-seven entities - a skeleton, its joints, two meshes - and only a
 * transform and a body are on the wire. A character built on the server after
 * the scene loaded would exist there and nowhere else: the client would be told
 * where an entity is without ever having been told what it is, and would draw
 * nothing at all.
 *
 * Authored into the scene, both ends have all four before either connects, at
 * the same slots, because both loaded the same file. Nothing is spawned, so
 * nothing can disagree.
 *
 * Sorted by slot so the order is the same on every machine and on every run.
 */
std::vector<Vkm::Engine::EntityId> authoredCharacters(Vkm::Engine::Scene& scene) {
    using namespace Vkm::Engine;
    std::vector<EntityId> characters;
    scene.forEach<CharacterController, ScriptComponent>(
            [&](EntityId id, CharacterController&, ScriptComponent&) {
        characters.push_back(id);
    });
    std::sort(characters.begin(), characters.end(),
              [](EntityId a, EntityId b) { return a.slot() < b.slot(); });
    return characters;
}

} // namespace

// Entries a host resolves after loading this module.
//
// Reported back to the host at load. It refuses a module built against a
// different engine rather than letting a layout mismatch surface later as a
// crash somewhere unrelated. VKM_ENGINE_VERSION comes from the engine this
// module linked, so rebuilding against a new SDK is all it ever needs.
VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

// The only required entry: it populates the host's BehaviorRegistry so the
// scene can name this project's behaviors.
//
// There is no vkmBuildScene here, unlike the other examples. This project's
// world is authored and saved rather than generated at play time, so
// project.json's entryScene names it and the host loads it - which is the path
// a project made in the editor takes, and the one nothing else here exercised.
VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    using Vkm::Engine::BehaviorRegistry;
    using Vkm::Engine::LabWalker;
    BehaviorRegistry::get().registerBehavior<LabWalker>();
}

// What a player is, for the hosts that take them.
//
// Called wherever the module is loaded, including the editor, which uses it to
// show an author what the game puts on the wire. Nothing here registers a
// component type: this project replicates transforms and bodies, which the
// engine registers itself.
VKM_MODULE_ENTRY
void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    using namespace Vkm::Engine;

    // Which characters are taken. Held here rather than on a component, so
    // handing one back is as reliable as handing one out and nothing in the
    // world has to be edited to say a seat is free.
    auto taken = std::make_shared<std::set<uint32_t>>();

    session.onSpawn(
        [taken](Scene& scene, ResourceManager&, PlayerId id) -> EntityId {
            for (EntityId character : authoredCharacters(scene)) {
                if (taken->count(character.slot())) continue;
                taken->insert(character.slot());
                const glm::vec3 at = scene.get<Transform>(character).position;
                LOG_INFO("Player %u takes the character at %.1f, %.1f, %.1f",
                         id, at.x, at.y, at.z);
                return character;
            }
            LOG_INFO("Player %u has nowhere to play: this scene holds %zu characters "
                     "and all of them are taken",
                     id, authoredCharacters(scene).size());
            return EntityId{};
        },
        [taken](Scene& scene, ResourceManager&, PlayerId, EntityId entity) {
            // The character stays in the world - it is the scene's own content,
            // and destroying it would leave the seat with nothing in it for the
            // next player. Only the claim is released.
            taken->erase(entity.slot());
            if (scene.isAlive(entity) && scene.has<CharacterController>(entity)) {
                // Left standing rather than mid-stride, so an abandoned body
                // does not keep walking into a wall.
                scene.get<CharacterController>(entity).moveInput = glm::vec3(0.0f);
            }
        });
}

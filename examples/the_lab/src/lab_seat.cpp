#include "lab_seat.h"

#include <algorithm>

#include "ecs/component/physics/character_controller.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "system/script/script_component.h"

#include "lab_state.h"

namespace Lab {

namespace {

// The authored character at the lowest slot, found without building the list.
EntityId firstCharacter(Scene& scene) {
    EntityId first{};
    scene.forEach<CharacterController, ScriptComponent>(
        [&](EntityId id, CharacterController&, ScriptComponent&) {
            if (!first || id.slot() < first.slot()) first = id;
        }
    );
    return first;
}

} // namespace

std::vector<EntityId> authoredCharacters(Scene& scene) {
    std::vector<EntityId> characters;
    scene.forEach<CharacterController, ScriptComponent>(
        [&](EntityId id, CharacterController&, ScriptComponent&) {
            characters.push_back(id);
        }
    );
    std::sort(
        characters.begin(),
        characters.end(),
        [](EntityId a, EntityId b) { return a.slot() < b.slot(); }
    );
    return characters;
}

bool isOfflineSeat(Scene& scene, EntityId entity) {
    return entity && firstCharacter(scene) == entity;
}

bool sitsOutOffline(Scene& scene, NetSession& net, EntityId entity) {
    return net.isOffline() && !isOfflineSeat(scene, entity);
}

EntityId localCharacter(Scene& scene, NetSession& net) {
    return net.isOffline() ? firstCharacter(scene) : net.localEntity();
}

void seatPlayer(Scene& scene, EntityId character) {
    if (LabScore* score = scene.tryGet<LabScore>(character)) {
        *score = LabScore{};
        return;
    }
    scene.add(character, LabScore{});
}

} // namespace Lab

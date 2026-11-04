// What a server's snapshot says of a world, read from what it holds for its client until the client
// confirms it.

#include <cstdint>
#include <vector>

#include "ecs/component/physics/ragdoll.h"
#include "net/replication/silence.h"
#include "net/replication/snapshot.h"
#include "net/wire/codecs.h"
#include "net/wire/schema.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "vkm_network/bit_stream.h"

#include "animation/rig.h"
#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

TEST("an inactive ragdoll's bones stay off the wire, and an active one's go on it") {
    Game game;
    Scene& scene = game.scene();
    const SkeletonHandle rig = addRig(game.resources());
    const EntityId walker = addCharacter(scene, rig, addTurn(game.resources(), "test:turn", 0.0f, 90.0f));
    REQUIRE(buildRagdoll(scene, walker, game.resources().get(rig)) == 2);

    NetSchema schema;
    registerEngineNetTypes(schema);
    const int transform = schema.indexOf("Transform");
    REQUIRE(transform >= 0);

    for (const bool active : {false, true}) {
        scene.get<Ragdoll>(walker).active = active;
        NetSilenceMap silence;
        silence.build(scene);
        NetBaseline told;
        std::vector<uint8_t> bytes;
        const NetBudget budget;
        Vkm::Network::BitWriter out(bytes, budget.bytes);
        writeSnapshot(scene, schema, silence, 1, budget, told, out);
        told.confirm(1);

        CHECK(told.find(walker, static_cast<uint32_t>(transform)) != nullptr);
        for (const RagdollBone& bone : scene.get<Ragdoll>(walker).bones) {
            CHECK((told.find(bone.body, static_cast<uint32_t>(transform)) != nullptr) == active);
        }
    }
}

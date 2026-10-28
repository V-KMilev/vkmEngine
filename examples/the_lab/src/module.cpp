#define VKM_LOG_CATEGORY "LAB"

#include <vector>

#include "logger.h"

#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "system/script/behavior_registry.h"

#include "system/script/module_entry.h"

#include "lab_area.h"
#include "lab_clip_reel.h"
#include "lab_crate.h"
#include "lab_follow_camera.h"
#include "lab_hud.h"
#include "lab_jump_pad.h"
#include "lab_mover.h"
#include "lab_night_light.h"
#include "lab_orbit.h"
#include "lab_pause_menu.h"
#include "lab_range.h"
#include "lab_seat.h"
#include "lab_shooter.h"
#include "lab_sign.h"
#include "lab_state.h"
#include "lab_step_pad.h"
#include "lab_switch.h"
#include "lab_thrower.h"
#include "lab_token.h"
#include "lab_walker.h"
#include "lab_wind.h"

// What each entry is for is documented in system/script/module_entry.h.
VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

// No vkmBuildScene: the world is authored, and project.json's entryScene names it.
VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    using namespace Lab;
    Vkm::Engine::BehaviorRegistry& registry = Vkm::Engine::BehaviorRegistry::get();
    registry.registerBehaviors<
        LabWalker,
        LabShooter,
        LabThrower,
        LabFollowCamera,
        LabPauseMenu,
        LabSign,
        LabClipReel,
        LabArea,
        LabCrate,
        LabHud,
        LabJumpPad,
        LabMover,
        LabNightLight,
        LabOrbit,
        LabStepPad,
        LabSwitch,
        LabToken,
        LabWind,
        LabRange
    >();
}

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

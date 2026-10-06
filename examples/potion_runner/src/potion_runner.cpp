#define VKM_LOG_CATEGORY "POTION"

#include "potion_runner.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/math/axes.h"
#include "core/math/easing.h"
#include "core/math/random.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/hierarchy_operations.h"
#include "platform/input/input_command.h"
#include "platform/input/input_map.h"
#include "platform/input/input_handle.h"
#include "platform/window/glfw_include.h"
#include "proc_audio.h"
#include "resource/generate/mesh_generators.h"
#include "resource/resource_manager.h"
#include "runner_rig.h"
#include "system/animation/animation_events.h"
#include "system/audio/audio_events.h"
#include "system/physics/physics_events.h"
#include "system/ui/ui_events.h"

namespace Potion {

namespace {

// The world scrolls along -Z past a player at z = 0; pooled props wrap WRAP units
// back to the far end once they pass behind the camera.
constexpr int   OBSTACLE_COUNT = 9;
constexpr int   COIN_COUNT     = 44;
constexpr int   TIE_COUNT      = 56;   // denser reads as faster
constexpr int   PILLAR_COUNT   = 16;   // per side; the arches share this lattice

// Arch and pillar height. A jump from a roof puts the head near 5.6 and the camera
// sits near 4.9, so the lattice clears both.
constexpr float ARCH_Y          = 6.6f;

constexpr float SPAWN_Z         = 140.0f;
constexpr float DESPAWN_Z       = -20.0f;  // just behind the camera
constexpr float WRAP            = SPAWN_Z - DESPAWN_Z;
constexpr float OBS_SPACING     = WRAP / OBSTACLE_COUNT;   // ~17.8, wider than the longest train
constexpr float COIN_SPACING    = WRAP / COIN_COUNT;
constexpr float TIE_SPACING     = WRAP / TIE_COUNT;
constexpr float PILLAR_SPACING  = WRAP / PILLAR_COUNT;
constexpr float INITIAL_AHEAD   = 32.0f;   // first obstacle's head start
constexpr float COIN_AHEAD      = 18.0f;
constexpr float GROUND_LEN      = 175.0f;
constexpr float GROUND_CENTER_Z = (SPAWN_Z + DESPAWN_Z) * 0.5f;

constexpr float PLAYER_HALF_X = 0.42f;
constexpr float PLAYER_HALF_Z = 0.42f;
constexpr float PLAYER_HALF_Y = 0.7f;
// Fully crouched, the head drops from 1.4 to 0.9, under an overhead gantry.
constexpr float CROUCH_HALF_Y = 0.45f;

// Above the jump apex: from the ground you board by the nose ramps only.
constexpr float TRAIN_TOP = 2.5f;

// Ground run of a boarding ramp: TRAIN_TOP over this is ~31 degrees, a runnable grade.
constexpr float RAMP_RUN = 4.2f;

// The how-to-play box, in the canvas's reference pixels.
constexpr float HELP_TOP    = 296.0f;   // from the start panel's top edge
constexpr float HELP_HEIGHT = 66.0f;    // shorter than the paragraph, so it scrolls
constexpr float HELP_BAR_X  = 303.0f;
constexpr float HELP_BAR_W  = 5.0f;

constexpr const char* ACTION_LEFT    = "Move/Left";
constexpr const char* ACTION_RIGHT   = "Move/Right";
constexpr const char* ACTION_JUMP    = "Jump";
constexpr const char* ACTION_CROUCH  = "Crouch";
constexpr const char* ACTION_RESTART = "Restart";

void installRunnerBindings(InputMap& map) {
    const auto key = [](int code) { return InputBinding{InputSource::Key, code, 1.0f}; };
    map.define(ACTION_LEFT,    { key(GLFW_KEY_A),     key(GLFW_KEY_LEFT) });
    map.define(ACTION_RIGHT,   { key(GLFW_KEY_D),     key(GLFW_KEY_RIGHT) });
    map.define(ACTION_JUMP,    { key(GLFW_KEY_SPACE), key(GLFW_KEY_W), key(GLFW_KEY_UP) });
    map.define(ACTION_CROUCH,  { key(GLFW_KEY_S),     key(GLFW_KEY_DOWN), key(GLFW_KEY_LEFT_CONTROL) });
    map.define(ACTION_RESTART, { key(GLFW_KEY_R),     key(GLFW_KEY_ENTER) });
}

// Every run reseeds with this, so a death is retryable against the same track.
constexpr uint64_t RUN_SEED = 0x9E3779B9u;

// Grounded grace after support ends: a jump just past a roof's end was meant for the roof.
constexpr float COYOTE_TIME = 0.1f;

// Closing rates, per second.
constexpr float LANE_EASE   = 14.0f;
constexpr float CROUCH_EASE = 16.0f;

// The ear rides the camera, 8.5 m behind, so full volume must reach past that.
constexpr float FOOTSTEP_VOLUME = 0.55f;
constexpr float COIN_VOLUME     = 0.42f;
constexpr float HEARING_NEAR    = 12.0f;
constexpr float HEARING_FAR     = 60.0f;

// Per-playback spread: enough to break the repeat, not enough to change the boot.
constexpr float SPREAD_VOLUME_MIN = 0.86f;
constexpr float SPREAD_PITCH_MIN  = 0.93f;
constexpr float SPREAD_PITCH_MAX  = 1.08f;

/**
 * @brief A coin's idle motion: a full revolution about Y plus a soft scale pulse.
 *
 * Keys 120 degrees apart, so each slerp takes the short way round.
 *
 * @param phase Start offset in seconds, so a row does not spin in lockstep.
 * @return A playing, looping Animation.
 */
Animation makeCoinSpin(float phase) {
    constexpr float PERIOD = 1.4f;
    Animation anim;
    anim.rotationTrack.setEasing(Easing::Linear);
    for (int k = 0; k <= 3; ++k) {
        anim.rotationTrack.addKeyframe(
            PERIOD * static_cast<float>(k) / 3.0f,
            glm::angleAxis(glm::two_pi<float>() * static_cast<float>(k) / 3.0f, Math::WORLD_AXIS_Y)
        );
    }
    anim.scaleTrack.setEasing(Easing::EaseInOutSine);
    anim.scaleTrack.addKeyframe(0.0f,           {0.70f, 0.70f, 0.12f});
    anim.scaleTrack.addKeyframe(PERIOD * 0.5f,  {0.80f, 0.80f, 0.16f});
    anim.scaleTrack.addKeyframe(PERIOD,         {0.70f, 0.70f, 0.12f});
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

/**
 * @brief Where an exponential ease from @p from toward @p to stands after @p seconds.
 *
 * Closed form, so a frame drawn between ticks sits on the next tick's path.
 *
 * @param from Value at the last tick.
 * @param to Value it closes on.
 * @param rate Closing rate, per second.
 * @param seconds Time past the last tick.
 * @return The eased value.
 */
float easeToward(float from, float to, float rate, float seconds) {
    return to + (from - to) * std::exp(-rate * seconds);
}

/**
 * @brief The runner's body half-height at a crouch amount.
 *
 * @param crouch 0 standing, 1 fully crouched.
 * @return Half-height, from PLAYER_HALF_Y down to CROUCH_HALF_Y.
 */
float bodyHalfHeight(float crouch) {
    return PLAYER_HALF_Y - crouch * (PLAYER_HALF_Y - CROUCH_HALF_Y);
}

} // namespace

void PotionRunner::onStart() {
    installRunnerBindings(input());

    // The world builds once; listeners register every time, since a session end
    // drops the subscriptions but keeps the world.
    if (!m_built) {
        buildWorld();   // sets m_built
        buildUI();
    }
    subscribe([this](const UIClickEvent& e) {
        if (e.eventId == "potion:start") m_started = true;
        else if (e.eventId == "potion:restart" && !m_alive) m_restartAsked = true;
    });
    // The crash rule: a contact with an obstacle hull whose normal is not mostly
    // up. `normal` points a -> b. Every non-ended tick of a contact counts; an
    // ended one has no normal to judge.
    subscribe([this](const CollisionEvent& e) {
        if (e.phase == ContactPhase::Ended) return;
        const EntityId player = m_player;
        const bool playerIsA = (e.a == player);
        if (!playerIsA && e.b != player) return;

        if (!m_alive) return;   // dead: the ragdoll IS the crash feedback

        const float    up    = playerIsA ? -e.normal.y : e.normal.y;
        const EntityId other = playerIsA ? e.b : e.a;
        for (const auto& o : m_obstacles) {
            if (other != o.entity) continue;
            // Feet near the roof line are graced (ramp-to-roof seam, short hops),
            // except on gantries, where it can mean a head inside the bar.
            const bool grace = o.bottom <= 0.0f && m_height >= o.top - 0.6f;
            if (up < 0.7f && !grace) die();
            return;
        }
    });
    // Not in mid-air, and not for the ragdoll.
    subscribe([this](const AnimationEvent& e) {
        if (e.entity != m_player || e.marker != RUNNER_MARKER_FOOTSTEP) return;
        if (!m_alive || !m_grounded) return;
        playAt(m_footstep, scene().get<Transform>(m_player).position, FOOTSTEP_VOLUME);
    });
    subscribe([this](const TriggerEvent& e) {
        if (e.phase != ContactPhase::Began) return;
        if (!m_alive || e.other != m_player) return;
        for (auto& c : m_coins) {
            if (e.trigger != c.entity) continue;
            if (!c.active) return;
            c.active = false;
            scene().get<Mesh>(c.entity).visible = false;
            ++m_coinCount;
            playAt(m_coinChime, scene().get<Transform>(c.entity).position, COIN_VOLUME);
            if (c.y > 1.5f) m_bonusScore += coinValue;
            if (m_coinCount % 10 == 0) LOG_INFO("Coins: %d", m_coinCount);
            return;
        }
    });
}

void PotionRunner::onFixedUpdate(float dt) {
    if (!m_built) return;

    // Undo a frame's drawn-ahead pose; simulation reads the last tick's.
    if (m_drawnAhead) {
        scene().get<Transform>(m_player) = m_tickBody;
        m_drawnAhead = false;
    }

    readInput();

    if (!m_started) {
        m_started = m_edgeJump || m_edgeLeft || m_edgeRight;
    } else if (m_alive) {
        m_speed = std::min(maxSpeed, m_speed + acceleration * dt);
        m_distance += m_speed * dt;

        // The track moves first, so the runner meets each ramp where physics will
        // find it. Crashes and coins arrive as physics events (see onStart).
        scrollWorld(dt);
        updatePlayer(dt);

        if (m_milestoneTimer > 0.0f) m_milestoneTimer -= dt;
        if (m_distance >= m_nextDistanceLog) {
            LOG_INFO("Distance %d  (coins %d)", static_cast<int>(m_distance), m_coinCount);
            if (UIText* flash = scene().tryGet<UIText>(m_uiMilestone)) {
                flash->text = std::to_string(static_cast<int>(m_nextDistanceLog)) + " m";
            }
            m_milestoneTimer   = 2.2f;
            m_nextDistanceLog += 500.0f;
        }
    } else if (m_edgeRestart || m_restartAsked) {
        resetGame();
    }

    // Physics runs next and collides with the tick pose, not the last drawn one.
    placeWorld(0.0f);
}

void PotionRunner::onUpdate(float dt) {
    if (!m_built) return;

    // Carry the last tick forward to this frame; a frozen run draws as the tick left it.
    if (m_started && m_alive && !clock().isPaused()) {
        const float ahead = clock().getFixedAlpha() * clock().getFixedStep();
        drawRunner(ahead);
        placeWorld(ahead);
    }

    updateCamera(dt);
    refreshUI();
}

MaterialHandle PotionRunner::makeMaterial(MaterialAsset material, const char* name) {
    // No texture handles: GLMaterial then falls back to the scalars.
    return resources().add(std::move(material), name);
}

void PotionRunner::playAt(AudioClipHandle clip, const glm::vec3& position, float volume) {
    // Not m_rng: it deals the track, which must not depend on how often feet land.
    const float spread = Math::Random::range(SPREAD_VOLUME_MIN, 1.0f);
    PlaySoundEvent sound = PlaySoundEvent::at(clip, position, volume * spread);
    sound.params.pitch       = Math::Random::range(SPREAD_PITCH_MIN, SPREAD_PITCH_MAX);
    sound.params.minDistance = HEARING_NEAR;
    sound.params.maxDistance = HEARING_FAR;
    events().emit(sound);
}

EntityId PotionRunner::spawnBox(MeshHandle mesh, MaterialHandle material, const char* name) {
    const EntityId entity = spawn(name);
    scene().add(entity, Mesh{mesh, material});
    scene().add(entity, Transform{});
    return entity;
}

void PotionRunner::buildMaterials() {
    MaterialAsset ground;
    ground.albedo    = {0.030f, 0.032f, 0.037f, 1.0f};
    ground.roughness = 0.96f;
    m_matGround = makeMaterial(std::move(ground), "potion:ground");

    MaterialAsset ballast;    // coarse gravel bed
    ballast.albedo    = {0.050f, 0.050f, 0.056f, 1.0f};
    ballast.roughness = 0.96f;
    m_matBallast = makeMaterial(std::move(ballast), "potion:ballast");

    MaterialAsset rail;       // brushed steel
    rail.albedo    = {0.52f,  0.54f,  0.58f, 1.0f};
    rail.metallic  = 1.0f;
    rail.roughness = 0.55f;
    m_matRail = makeMaterial(std::move(rail), "potion:rail");

    MaterialAsset tie;        // creosote sleeper
    tie.albedo    = {0.085f, 0.062f, 0.042f, 1.0f};
    tie.roughness = 0.95f;
    m_matTie = makeMaterial(std::move(tie), "potion:tie");

    MaterialAsset wall;       // concrete
    wall.albedo    = {0.055f, 0.058f, 0.070f, 1.0f};
    wall.roughness = 0.95f;
    m_matWall = makeMaterial(std::move(wall), "potion:wall");

    MaterialAsset pillar;     // concrete column
    pillar.albedo    = {0.070f, 0.073f, 0.085f, 1.0f};
    pillar.roughness = 0.93f;
    m_matPillar = makeMaterial(std::move(pillar), "potion:pillar");

    MaterialAsset player;
    player.albedo    = {0.05f,  0.45f,  0.62f, 1.0f};
    player.metallic  = 0.5f;
    player.roughness = 0.42f;
    player.emission  = {0.00f, 0.18f, 0.28f};
    player.emissiveStrength = 1.0f;
    m_matPlayer = makeMaterial(std::move(player), "potion:player");

    MaterialAsset playerGlow;
    playerGlow.type      = MaterialType::Unlit;
    playerGlow.albedo    = {0.40f,  0.95f,  1.00f, 1.0f};
    playerGlow.roughness = 0.40f;
    playerGlow.emission  = {0.20f, 0.85f, 1.00f};
    playerGlow.emissiveStrength = 1.8f;
    m_matPlayerGlow = makeMaterial(std::move(playerGlow), "potion:player_glow");

    MaterialAsset train;
    train.albedo    = {0.10f,  0.12f,  0.18f, 1.0f};
    train.metallic  = 1.00f;
    train.roughness = 0.70f;
    train.emission  = {0.00f, 0.14f, 0.30f};
    train.emissiveStrength = 0.5f;
    m_matTrain = makeMaterial(std::move(train), "potion:train");

    MaterialAsset trainB;
    trainB.albedo    = {0.06f,  0.16f,  0.15f, 1.0f};
    trainB.metallic  = 1.00f;
    trainB.roughness = 0.70f;
    trainB.emission  = {0.00f, 0.18f, 0.14f};
    trainB.emissiveStrength = 0.4f;
    m_matTrainB = makeMaterial(std::move(trainB), "potion:train_b");

    MaterialAsset trainC;
    trainC.albedo    = {0.11f,  0.11f,  0.13f, 1.0f};
    trainC.metallic  = 1.00f;
    trainC.roughness = 0.72f;
    trainC.emission  = {0.10f, 0.10f, 0.16f};
    trainC.emissiveStrength = 0.35f;
    m_matTrainC = makeMaterial(std::move(trainC), "potion:train_c");

    MaterialAsset window;
    window.type      = MaterialType::Unlit;
    window.albedo    = {0.70f,  0.90f,  1.00f, 1.0f};
    window.roughness = 0.30f;
    window.emission  = {0.55f, 0.85f, 1.00f};
    window.emissiveStrength = 1.8f;
    m_matWindow = makeMaterial(std::move(window), "potion:window");
    MaterialAsset headlamp;
    headlamp.type      = MaterialType::Unlit;
    headlamp.albedo    = {1.00f,  0.95f,  0.80f, 1.0f};
    headlamp.roughness = 0.40f;
    headlamp.emission  = {1.00f, 0.92f, 0.72f};
    headlamp.emissiveStrength = 1.8f;
    m_matHeadlamp = makeMaterial(std::move(headlamp), "potion:headlamp");

    // Emission only keeps them off pure black at distance.
    MaterialAsset barrier;
    barrier.albedo    = {0.42f,  0.05f,  0.04f, 1.0f};
    barrier.roughness = 0.75f;
    barrier.emission  = {0.85f, 0.05f, 0.03f};
    barrier.emissiveStrength = 0.25f;
    m_matBarrier = makeMaterial(std::move(barrier), "potion:barrier");

    MaterialAsset stripe;
    stripe.albedo    = {0.82f,  0.84f,  0.87f, 1.0f};
    stripe.roughness = 0.60f;
    stripe.emission  = {0.60f, 0.63f, 0.70f};
    stripe.emissiveStrength = 0.30f;
    m_matStripe = makeMaterial(std::move(stripe), "potion:stripe");

    MaterialAsset signalRed;
    signalRed.type      = MaterialType::Unlit;
    signalRed.albedo    = {1.00f, 0.12f, 0.08f, 1.0f};
    signalRed.roughness = 0.5f;
    signalRed.emission  = {1.00f, 0.08f, 0.05f};
    signalRed.emissiveStrength = 1.1f;
    m_matSignalRed = makeMaterial(std::move(signalRed), "potion:signal_red");

    MaterialAsset signalGreen;
    signalGreen.type      = MaterialType::Unlit;
    signalGreen.albedo    = {0.20f, 1.00f, 0.40f, 1.0f};
    signalGreen.roughness = 0.5f;
    signalGreen.emission  = {0.10f, 0.90f, 0.30f};
    signalGreen.emissiveStrength = 1.1f;
    m_matSignalGreen = makeMaterial(std::move(signalGreen), "potion:signal_green");

    MaterialAsset coin;
    coin.albedo    = {1.00f,  0.78f,  0.28f, 1.0f};
    coin.metallic  = 1.0f;
    coin.roughness = 0.08f;
    coin.emission  = {1.00f, 0.6f, 0.0f};
    coin.emissiveStrength = 2.0f;
    m_matCoin = makeMaterial(std::move(coin), "potion:coin");

    // Neon fixtures: bright enough to read as sources, dim beside the Lights they carry.
    MaterialAsset arch;
    arch.type      = MaterialType::Unlit;
    arch.albedo    = {0.85f,  0.92f,  1.00f, 1.0f};
    arch.roughness = 0.40f;
    arch.emission  = {0.45f, 0.70f, 1.00f};
    arch.emissiveStrength = 1.3f;
    m_matArch = makeMaterial(std::move(arch), "potion:arch");

    MaterialAsset trim;
    trim.type      = MaterialType::Unlit;
    trim.albedo    = {0.90f,  0.35f,  1.00f, 1.0f};
    trim.roughness = 0.40f;
    trim.emission  = {0.80f, 0.18f, 1.00f};
    trim.emissiveStrength = 1.3f;
    m_matTrim = makeMaterial(std::move(trim), "potion:trim");
}

void PotionRunner::buildEnvironment() {
    scene().environment().sky.intensity  = SKY_INTENSITY;
    scene().environment().sky.showSkybox = false;   // underground: no sky, just the tunnel
    // No directional light means no cascades, so the headlights get the whole 2D atlas.
    scene().forEach<Light>([](EntityId, Light& light) {
        if (light.type == LightType::Directional) light.enabled = false;
    });
    scene().physics().gravity = {0.0f, -gravity, 0.0f};

    m_cubeMesh = resources().add(generateCube(), "potion:cube");

    // The scene's own camera (see module.cpp).
    m_camera = findActiveCamera(scene());
}

// Uniform along Z, so none of it scrolls.
void PotionRunner::buildTunnel() {
    // m_wallX is a member: the scrolling pools and the camera place against it.
    const float trackWidth = laneWidth * 3.0f + 4.0f;
    m_wallX = laneWidth * 1.5f + 0.5f;

    {
        EntityId ground = spawnBox(m_cubeMesh, m_matGround, "Ground");
        Transform& t = scene().get<Transform>(ground);
        t.position = {0.0f, -0.2f, GROUND_CENTER_Z};   // top face sits at y = 0
        t.scale    = {trackWidth, 0.4f, GROUND_LEN};
        scene().get<Mesh>(ground).castShadows = false;

        // Static floor for the ragdoll. The solver ignores Transform scale, so the
        // collider carries world half-extents.
        Rigidbody rb;
        rb.motion = RigidbodyMotion::Static;
        scene().add(ground, std::move(rb));
        Collider col;
        col.parts = {
            ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, {trackWidth * 0.5f, 0.2f, GROUND_LEN * 0.5f}}
        };
        scene().add(ground, std::move(col));
    }
    for (int side = -1; side <= 1; side += 2) {
        EntityId wall = spawnBox(m_cubeMesh, m_matWall, "Wall");
        Transform& t = scene().get<Transform>(wall);
        t.position = {static_cast<float>(side) * (m_wallX + 0.3f), 3.2f, GROUND_CENTER_Z};
        t.scale    = {0.4f, 7.7f, GROUND_LEN};   // below grade up to the raised ceiling, no gap
        scene().get<Mesh>(wall).castShadows = false;

        for (float pipeY : {2.05f, 4.35f}) {
            EntityId pipe = spawnBox(m_cubeMesh, m_matPillar, "Wall Pipe");
            Transform& pt = scene().get<Transform>(pipe);
            pt.position = {static_cast<float>(side) * (m_wallX + 0.04f), pipeY, GROUND_CENTER_Z};
            pt.scale    = {0.13f, 0.13f, GROUND_LEN};
            scene().get<Mesh>(pipe).castShadows = false;
        }
    }

    // Only obstacles and the runner cast shadows.
    {
        EntityId ceiling = spawnBox(m_cubeMesh, m_matWall, "Ceiling");
        Transform& t = scene().get<Transform>(ceiling);
        t.position = {0.0f, ARCH_Y + 0.42f, GROUND_CENTER_Z};
        t.scale    = {2.0f * m_wallX + 1.4f, 0.3f, GROUND_LEN};
        scene().get<Mesh>(ceiling).castShadows = false;
    }

    for (int lane = 0; lane < 3; ++lane) {
        EntityId bed = spawnBox(m_cubeMesh, m_matBallast, "Ballast Bed");
        Transform& t = scene().get<Transform>(bed);
        t.position = {laneX(lane), 0.015f, GROUND_CENTER_Z};   // top face just proud of the ground
        t.scale    = {laneWidth * 0.88f, 0.07f, GROUND_LEN};
        scene().get<Mesh>(bed).castShadows = false;
    }

    constexpr float RAIL_GAUGE_HALF = 0.50f;
    constexpr float RAIL_W = 0.12f, RAIL_H = 0.14f;
    for (int lane = 0; lane < 3; ++lane) {
        for (int s = -1; s <= 1; s += 2) {
            EntityId rail = spawnBox(m_cubeMesh, m_matRail, "Rail");
            Transform& t = scene().get<Transform>(rail);
            t.position = {
                laneX(lane) + static_cast<float>(s) * RAIL_GAUGE_HALF,
                0.04f + RAIL_H * 0.5f,
                GROUND_CENTER_Z
            };
            t.scale    = {RAIL_W, RAIL_H, GROUND_LEN};
            scene().get<Mesh>(rail).castShadows = false;
        }
    }

    // Just inside the pillars, so the columns pass in front of it.
    for (int side = -1; side <= 1; side += 2) {
        EntityId trim = spawnBox(m_cubeMesh, m_matTrim, "Wall Trim");
        Transform& t = scene().get<Transform>(trim);
        t.position = {static_cast<float>(side) * (m_wallX - 0.25f), 3.1f, GROUND_CENTER_Z};
        t.scale    = {0.10f, 0.18f, GROUND_LEN};
        scene().get<Mesh>(trim).castShadows = false;
    }
}

void PotionRunner::buildPlayer() {
    // An invisible root at scale 1, so parts keep their own scale. Its origin is
    // the body centre, PLAYER_HALF_Y above the feet.
    m_player = spawn("Player");
    scene().add(m_player, Transform{});
    // Always dynamic: freezeRotation keeps it upright, and death unfreezes it into the ragdoll.
    {
        Rigidbody rb;
        rb.mass           = 1.0f;
        rb.restitution    = 0.0f;
        rb.friction       = 0.2f;
        rb.freezeRotation = true;
        rb.canSleep       = false;   // a dozing character eats jump inputs and ignores ramps
        scene().add(m_player, std::move(rb));
        const glm::vec3 halfExtents{PLAYER_HALF_X, PLAYER_HALF_Y, PLAYER_HALF_Z};
        Collider col;
        col.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, halfExtents}};
        scene().add(m_player, std::move(col));
    }
    m_playerParts.clear();
    auto addPart = [&](
        const char* name,
        MaterialHandle mat,
        const glm::vec3& scale,
        const glm::vec3& offset,
        EntityId parent
    ) {
        EntityId e = spawnBox(m_cubeMesh, mat, name);
        Transform& t = scene().get<Transform>(e);
        t.position = offset;
        t.scale    = scale;
        HierarchyOperations::setParent(scene(), e, parent);
        m_playerParts.emplace_back(e, mat);
    };
    addPart("Torso",   m_matPlayer,     {0.74f, 0.78f, 0.52f}, { 0.00f,  0.08f,  0.00f}, m_player);
    addPart("Head",    m_matPlayer,     {0.46f, 0.42f, 0.46f}, { 0.00f,  0.66f,  0.00f}, m_player);
    addPart("Visor",   m_matPlayerGlow, {0.50f, 0.12f, 0.50f}, { 0.00f,  0.74f,  0.00f}, m_player);
    // On the back, toward the camera.
    addPart("Pack",    m_matPlayerGlow, {0.46f, 0.62f, 0.18f}, { 0.00f,  0.10f, -0.36f}, m_player);
    {
        Animator stride;
        stride.skeleton = resources().add(makeRunnerSkeleton(), RUNNER_RIG_NAME);
        stride.clip     = resources().add(makeRunnerStride(),   RUNNER_CLIP_NAME);
        scene().add(m_player, std::move(stride));
    }
    // The socket sits at the joint, so the limb turns about it, not its own centre.
    // It must be a direct child of the Animator's entity.
    auto addLimb = [&](const char* bone, const glm::vec3& scale) {
        EntityId socket = spawn(bone, m_player);
        scene().add(socket, Transform{});
        BoneSocket attach;
        attach.bone = bone;
        scene().add(socket, std::move(attach));
        addPart(bone, m_matPlayer, scale, {0.0f, -scale.y * 0.5f + 0.04f, 0.0f}, socket);
    };
    addLimb(RUNNER_BONE_ARM_L, {0.15f, 0.56f, 0.28f});
    addLimb(RUNNER_BONE_ARM_R, {0.15f, 0.56f, 0.28f});
    addLimb(RUNNER_BONE_LEG_L, {0.20f, 0.46f, 0.30f});
    addLimb(RUNNER_BONE_LEG_R, {0.20f, 0.46f, 0.30f});

    m_footstep  = resources().add(makeFootstepSound(), "potion:footstep");
    m_coinChime = resources().add(makeCoinChime(), "potion:coin");
}

// Recycled, so the track is endless at a fixed entity count.
void PotionRunner::buildScenery() {
    auto makeScenery = [&](
        std::vector<Scenery>& pool,
        int count,
        MaterialHandle mat,
        const glm::vec3& scale,
        const char* name
    ) {
        pool.resize(count);
        for (auto& s : pool) {
            s.entity = spawnBox(m_cubeMesh, mat, name);
            scene().get<Transform>(s.entity).scale = scale;
            scene().get<Mesh>(s.entity).castShadows = false;
        }
    };
    const float archSpan = 2.0f * m_wallX + 0.4f;
    for (auto& lanePool : m_ties)
        makeScenery(lanePool, TIE_COUNT, m_matTie, {laneWidth * 0.62f, 0.10f, 0.30f}, "Tie");
    makeScenery(m_pillarsL, PILLAR_COUNT, m_matPillar, {0.40f, ARCH_Y, 0.40f},           "Pillar");
    makeScenery(m_pillarsR, PILLAR_COUNT, m_matPillar, {0.40f, ARCH_Y, 0.40f},           "Pillar");
    // On each pillar's inner face; same z lattice, so they stay on their pillar.
    makeScenery(m_signalsL, PILLAR_COUNT, m_matSignalRed,   {0.12f, 0.26f, 0.12f}, "Signal");
    makeScenery(m_signalsR, PILLAR_COUNT, m_matSignalGreen, {0.12f, 0.26f, 0.12f}, "Signal");
    // All pools scroll and wrap alike, so a spacing of 40 (a multiple of the 10-unit
    // pillar lattice) with a half-bay phase keeps each platform between pillars.
    makeScenery(m_platformsL, 4, m_matPillar, {0.90f, 1.00f, 7.0f}, "Platform");
    makeScenery(m_platformsR, 4, m_matPillar, {0.90f, 1.00f, 7.0f}, "Platform");
    makeScenery(m_platEdgesL, 4, m_matStripe, {0.90f, 0.05f, 7.0f}, "Platform Edge");
    makeScenery(m_platEdgesR, 4, m_matStripe, {0.90f, 0.05f, 7.0f}, "Platform Edge");
    makeScenery(m_arches, PILLAR_COUNT, m_matPillar, {archSpan, 0.55f, 0.75f}, "Arch Beam");
    // A point light, not a Rect: only a point light takes a cube shadow.
    makeScenery(m_archLights, PILLAR_COUNT, m_matArch, {archSpan * 0.88f, 0.16f, 0.45f}, "Arch Light");
    for (auto& strip : m_archLights) {
        Light wash;
        wash.type       = LightType::Point;
        wash.color      = {0.45f, 0.70f, 1.00f};   // matches the strip's emissive
        wash.intensity  = 60.0f;
        // Under the 10-unit arch spacing, or the pools merge into one flat wash.
        wash.radius     = 8.0f;
        // placeWorld enables it for the fixtures nearest the player, within
        // Config::MAX_SHADOW_CASTERS_CUBE.
        wash.castShadows = false;
        scene().add(strip.entity, std::move(wash));
    }

    // The accent is a sibling, not a child, so the obstacle's per-recycle scale
    // never distorts it.
    m_obstacles.resize(OBSTACLE_COUNT);
    for (auto& o : m_obstacles) {
        o.entity = spawnBox(m_cubeMesh, m_matTrain,  "Obstacle");
        o.accent = spawnBox(m_cubeMesh, m_matWindow, "Obstacle Accent");
        o.auxA   = spawnBox(m_cubeMesh, m_matPillar, "Obstacle Detail");
        o.auxB   = spawnBox(m_cubeMesh, m_matPillar, "Obstacle Detail");
        scene().get<Mesh>(o.accent).castShadows = false;
        Light beam;
        beam.type           = LightType::Spot;
        beam.color          = {1.00f, 0.92f, 0.72f};
        beam.intensity      = 70.0f;
        beam.radius         = 20.0f;
        beam.innerConeAngle = 0.28f;
        beam.outerConeAngle = 0.55f;
        beam.enabled        = false;
        beam.castShadows    = true;
        o.lamp = spawn("Train Headlight");
        scene().add(o.lamp, std::move(beam));
        // Unrotated: forward is -Z, toward the oncoming player.
        scene().add(
            o.lamp,
            Transform{{0.0f, 1.05f, SPAWN_Z}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)}
        );
        // Kinematic: the player lands on its roof by solver contact. Extents
        // follow each recycle.
        {
            Rigidbody rb;
            rb.motion = RigidbodyMotion::Kinematic;
            scene().add(o.entity, std::move(rb));
            Collider col;
            col.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
            scene().add(o.entity, std::move(col));
        }
        // Hidden until a recycle makes a steady train. The collider is thicker than
        // the slab so a fast fall cannot tunnel through.
        o.ramp = spawnBox(m_cubeMesh, m_matStripe, "Boarding Ramp");
        scene().get<Mesh>(o.ramp).visible = false;
        {
            Rigidbody rb;
            rb.motion = RigidbodyMotion::Kinematic;
            scene().add(o.ramp, std::move(rb));
            Collider col;
            col.parts = {ColliderPart{ColliderShape::Box, {0.0f, -0.30f, 0.0f}, {1.0f, 0.35f, 1.0f}}};
            scene().add(o.ramp, std::move(col));
        }
        o.skirt   = spawnBox(m_cubeMesh, m_matGround,    "Train Skirt");
        o.tail    = spawnBox(m_cubeMesh, m_matSignalRed, "Train Tail Light");
        o.plow    = spawnBox(m_cubeMesh, m_matPillar,    "Train Plow");
        o.lampBar = spawnBox(m_cubeMesh, m_matHeadlamp,  "Train Lamp Bar");
        scene().get<Mesh>(o.skirt).visible   = false;
        scene().get<Mesh>(o.tail).visible    = false;
        scene().get<Mesh>(o.plow).visible    = false;
        scene().get<Mesh>(o.lampBar).visible = false;
        // Detail: the hull already throws the silhouette.
        scene().get<Mesh>(o.tail).castShadows    = false;
        scene().get<Mesh>(o.lampBar).castShadows = false;
        // Plow and lamp bar never change shape, so scale and tilt are set once.
        {
            Transform& plowT = scene().get<Transform>(o.plow);
            plowT.scale    = {obstacleHalfX() * 1.7f, 0.50f, 0.12f};
            plowT.rotation = glm::angleAxis(-0.7f, Math::WORLD_AXIS_X);
            scene().get<Transform>(o.lampBar).scale =
                {obstacleHalfX() * 2.0f * 0.55f, 0.16f, 0.08f};
        }
    }
    m_coins.resize(COIN_COUNT);
    for (int i = 0; i < COIN_COUNT; ++i) {
        Coin& c = m_coins[i];
        c.entity = spawnBox(m_cubeMesh, m_matCoin, "Coin");
        scene().get<Transform>(c.entity).scale = {0.7f, 0.7f, 0.12f};
        scene().get<Mesh>(c.entity).castShadows = false;
        // Animation owns rotation and scale; placeWorld writes only position.
        scene().add(c.entity, makeCoinSpin(static_cast<float>(i) * 0.11f));
        // A trigger large enough that a fast player cannot pass it between ticks.
        {
            Rigidbody rb;
            rb.motion = RigidbodyMotion::Kinematic;
            scene().add(c.entity, std::move(rb));
            Collider col;
            col.isTrigger = true;
            col.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, {1.0f, 1.2f, 0.5f}}};
            scene().add(c.entity, std::move(col));
        }
    }
}

void PotionRunner::buildWorld() {
    buildEnvironment();
    buildMaterials();
    buildTunnel();
    buildPlayer();
    buildScenery();

    m_built = true;
    resetGame();

    LOG_INFO(
        "Potion Runner ready - A/D switch lane, Space/W jump, S/Down slide, "
        "run up the white ramps to ride the trains, R restart."
    );
}

void PotionRunner::randomizeObstacle(Obstacle& o) {
    o.lane = randLane();
    float r = frand();
    // Recycles are consecutive down the track, so a convoy owed takes this one.
    const bool convoyCar = m_convoyLeft > 0;
    if (convoyCar) {
        --m_convoyLeft;
        o.lane = m_convoyLane;
        r = 0.5f;                                  // the train branch
    }
    const float halfX = obstacleHalfX();
    o.bottom     = 0.0f;
    o.auxVisible = false;
    o.hasRamp    = false;
    o.isTrain    = false;

    // A faster train can share a z window with the one ahead. A middle blocker
    // beside an outer one walls the player in that outer lane off from the free one.
    const auto noLaneIsCutOff = [](int a, int b) {
        return a == b || (a + b == 2 && a != 1);   // same lane, or the {0, 2} pair
    };
    if (r >= 0.78f && m_prevRel > 0.0f && !noLaneIsCutOff(o.lane, m_prevLane)) {
        r = 0.30f;   // gantry instead
    }
    MaterialHandle mat;
    MaterialHandle accentMat;
    MaterialHandle auxAMat;
    MaterialHandle auxBMat;
    glm::vec3      auxAScale{0.0f};
    glm::vec3      auxBScale{0.0f};
    if (r < 0.22f) {                 // hurdle - hop over it
        o.top = 0.7f; o.length = 1.2f; o.rideable = true; o.relFactor = 0.0f;
        mat = m_matBarrier;
        // A white band along the leading top edge.
        accentMat      = m_matStripe;
        o.accentScale  = {halfX * 2.0f * 1.04f, 0.14f, 0.14f};
        o.accentOffset = {0.0f, o.top * 0.5f - 0.06f, -o.length * 0.5f - 0.04f};
    } else if (r < 0.40f) {          // overhead gantry - crouch/slide under it
        o.top = 2.8f; o.bottom = 1.05f; o.length = 1.4f; o.rideable = false; o.relFactor = 0.0f;
        mat = m_matBarrier;
        // A white band on the underside leading edge.
        accentMat      = m_matStripe;
        o.accentScale  = {halfX * 2.0f * 1.05f, 0.26f, 0.14f};
        o.accentOffset = {0.0f, o.bottom + 0.10f - (o.bottom + o.top) * 0.5f, -o.length * 0.5f - 0.05f};
        // Legs span 0..bottom, just inside each end of the bar.
        o.auxVisible = true;
        auxAMat      = m_matPillar;
        auxBMat      = m_matPillar;
        auxAScale    = {0.20f, o.bottom, 0.32f};
        auxBScale    = auxAScale;
        o.auxAOffset = {-(halfX - 0.10f), -o.top * 0.5f, 0.0f};
        o.auxBOffset = { (halfX - 0.10f), -o.top * 0.5f, 0.0f};
    } else if (r < 0.78f) {          // train - ride the roof or dodge the lane
        o.top = TRAIN_TOP; o.length = 10.0f + frand() * 5.0f; o.rideable = true;
        // Most move with the track, some faster; none slower, which reads as drifting backwards.
        o.relFactor = (frand() < 0.4f) ? 0.20f + frand() * 0.30f : 0.0f;
        if (convoyCar) {
            // Long and steady, so the roof line stays hoppable.
            o.length    = 12.0f + frand() * 3.0f;
            o.relFactor = 0.0f;
        } else if (frand() < 0.35f) {
            // Opens a convoy of the next 2-3 recycles.
            m_convoyLeft = 2 + (frand() < 0.4f ? 1 : 0);
            m_convoyLane = o.lane;
        }
        // The same guard: a fast train that would cut a lane off runs steady instead.
        if (o.relFactor > 0.0f && m_prevBlocking && !noLaneIsCutOff(o.lane, m_prevLane)) {
            o.relFactor = 0.0f;
        }
        // Steady trains only; a convoy follower's ramp would poke into the car ahead.
        o.hasRamp = (o.relFactor == 0.0f) && !convoyCar;
        if (o.hasRamp) {
            const float slopeLen = std::sqrt(o.top * o.top + RAMP_RUN * RAMP_RUN);
            Transform& rt = scene().get<Transform>(o.ramp);
            rt.scale    = {halfX * 2.0f * 0.9f, 0.14f, slopeLen};
            // Pitched so the +Z end meets the roof and the -Z end the rails.
            rt.rotation = glm::angleAxis(-std::atan2(o.top, RAMP_RUN), Math::WORLD_AXIS_X);
        }
        o.isTrain = true;
        // A convoy reuses its leader's colour, so the chain reads as one train.
        const int style = convoyCar ? m_convoyStyle : m_rng.nextInt(0, 2);
        m_convoyStyle = style;
        const MaterialHandle hulls[3] = {m_matTrain, m_matTrainB, m_matTrainC};
        mat = hulls[style];
        // Dressing scales; placeWorld positions them.
        scene().get<Transform>(o.skirt).scale  = {halfX * 2.0f * 1.04f, 0.42f, o.length * 1.01f};
        scene().get<Transform>(o.tail).scale   = {halfX * 2.0f * 0.80f, 0.30f, 0.10f};
        // A lit windscreen across the leading face.
        accentMat      = m_matWindow;
        o.accentScale  = {halfX * 2.0f * 0.82f, 0.46f, 0.10f};
        o.accentOffset = {0.0f, o.top * 0.18f, -o.length * 0.5f - 0.06f};
        // A roof walk line, and one window band wider than the hull so it shows on both flanks.
        o.auxVisible = true;
        auxAMat      = m_matStripe;
        auxAScale    = {halfX * 2.0f * 0.34f, 0.06f, o.length * 0.86f};
        o.auxAOffset = {0.0f, (o.top - o.bottom) * 0.5f + 0.03f, 0.0f};
        auxBMat      = m_matWindow;
        auxBScale    = {halfX * 2.0f * 1.07f, 0.34f, o.length * 0.78f};
        o.auxBOffset = {0.0f, 0.25f, 0.0f};
    } else {                         // barrier - too tall to clear, must switch lane
        o.top = 3.0f; o.length = 1.4f; o.rideable = false; o.relFactor = 0.0f;
        mat = m_matBarrier;
        accentMat      = m_matStripe;
        o.accentScale  = {halfX * 2.0f * 1.06f, 0.30f, 0.14f};
        o.accentOffset = {0.0f, 0.0f, -o.length * 0.5f - 0.05f};
        o.auxVisible = true;
        auxAMat      = m_matStripe;
        auxBMat      = m_matStripe;
        auxAScale    = {halfX * 2.0f * 1.06f, 0.30f, 0.14f};
        auxBScale    = auxAScale;
        o.auxAOffset = {0.0f,  0.95f, -o.length * 0.5f - 0.05f};
        o.auxBOffset = {0.0f, -0.95f, -o.length * 0.5f - 0.05f};
    }
    Transform& t = scene().get<Transform>(o.entity);
    t.scale    = {halfX * 2.0f, o.top - o.bottom, o.length};   // placeWorld sets the centre
    t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    scene().get<Mesh>(o.entity).material = mat;

    // Colliders carry real half-extents: the solver ignores Transform scale.
    scene().get<Collider>(o.entity).parts[0].halfExtents =
        {halfX, (o.top - o.bottom) * 0.5f, o.length * 0.5f};
    scene().get<Collider>(o.ramp).enabled = o.hasRamp;
    if (o.hasRamp) {
        // Extended past the slab's toe and thick, so the toe's end face sits below
        // grade and the first contact is always the walkable top face.
        const float slopeLen = std::sqrt(o.top * o.top + RAMP_RUN * RAMP_RUN);
        ColliderPart& rampBox = scene().get<Collider>(o.ramp).parts[0];
        rampBox.halfExtents  = {halfX * 0.9f, 0.35f, slopeLen * 0.5f + 0.35f};
        rampBox.center       = {0.0f, -0.30f, -0.35f};
    }

    scene().get<Light>(o.lamp).enabled = o.isTrain;

    scene().get<Transform>(o.accent).scale = o.accentScale;
    scene().get<Mesh>(o.accent).material   = accentMat;

    scene().get<Mesh>(o.ramp).visible    = o.hasRamp;
    scene().get<Mesh>(o.skirt).visible   = o.isTrain;
    scene().get<Mesh>(o.tail).visible    = o.isTrain;
    scene().get<Mesh>(o.plow).visible    = o.isTrain;
    scene().get<Mesh>(o.lampBar).visible = o.isTrain;
    scene().get<Mesh>(o.auxA).visible = o.auxVisible;
    scene().get<Mesh>(o.auxB).visible = o.auxVisible;
    if (o.auxVisible) {
        scene().get<Transform>(o.auxA).scale = auxAScale;
        scene().get<Transform>(o.auxB).scale = auxBScale;
        scene().get<Mesh>(o.auxA).material   = auxAMat;
        scene().get<Mesh>(o.auxB).material   = auxBMat;
    }

    // For the next recycle's solvability guards.
    m_prevLane     = o.lane;
    m_prevRel      = o.isTrain ? o.relFactor : 0.0f;
    m_prevBlocking = o.isTrain || (!o.rideable && o.bottom <= 0.0f);   // hull or barrier
}

void PotionRunner::resetGame() {
    m_alive    = true;
    m_restartAsked = false;
    m_lane     = 1;
    m_playerX  = 0.0f;
    m_height   = 0.0f;
    m_grounded = true;
    m_coyoteTimer = COYOTE_TIME;
    m_climbRamp   = -1;
    m_crouch     = 0.0f;
    m_crouchHeld = false;
    m_speed    = startSpeed;
    m_distance = 0.0f;
    m_coinCount = 0;
    m_bonusScore = 0;
    m_convoyLeft = 0;
    m_convoyLane = 1;
    m_prevLane     = 1;
    m_prevRel      = 0.0f;
    m_prevBlocking = false;
    m_camFollow = {0.0f, CAMERA_REST_Y};
    m_camX     = 0.0f;
    m_camY     = CAMERA_REST_Y;
    m_sceneryScroll  = 0.0f;
    m_milestoneTimer = 0.0f;
    m_nextDistanceLog = 500.0f;
    m_rng.seed(RUN_SEED);

    // Back from ragdoll: same body, re-frozen, still, and upright.
    {
        Rigidbody& rb = scene().get<Rigidbody>(m_player);
        rb.freezeRotation  = true;
        rb.linearVelocity  = {0.0f, 0.0f, 0.0f};
        rb.angularVelocity = {0.0f, 0.0f, 0.0f};
        rb.restitution     = 0.0f;
        rb.friction        = 0.2f;
        ColliderPart& box = scene().get<Collider>(m_player).parts[0];
        box.halfExtents.y = PLAYER_HALF_Y;
        box.center.y      = 0.0f;
    }
    for (auto& [part, mat] : m_playerParts) {        // undo the death recolour
        scene().get<Mesh>(part).material = mat;
        scene().get<Mesh>(part).visible  = true;
    }
    Animator& stride = scene().get<Animator>(m_player);
    stride.playing = true;
    stride.time    = 0.0f;

    Transform& pt = scene().get<Transform>(m_player);
    pt.position = {0.0f, PLAYER_HALF_Y, 0.0f};
    pt.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    pt.scale    = {1.0f, 1.0f, 1.0f};

    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        m_obstacles[i].z = INITIAL_AHEAD + static_cast<float>(i) * OBS_SPACING;
        randomizeObstacle(m_obstacles[i]);
    }
    int lane = randLane();
    for (int i = 0; i < COIN_COUNT; ++i) {
        if (i % 4 == 0) lane = randLane();   // coins run in lanes of four
        m_coins[i].lane   = lane;
        m_coins[i].z      = COIN_AHEAD + static_cast<float>(i) * COIN_SPACING;
        m_coins[i].active = true;
        scene().get<Mesh>(m_coins[i].entity).visible = true;
    }
    for (auto& lanePool : m_ties)
        for (int i = 0; i < TIE_COUNT; ++i)
            lanePool[i].z = DESPAWN_Z + static_cast<float>(i) * TIE_SPACING;
    for (int i = 0; i < PILLAR_COUNT; ++i) {
        const float z = DESPAWN_Z + static_cast<float>(i) * PILLAR_SPACING;
        m_pillarsL[i].z   = z;
        m_pillarsR[i].z   = z;
        m_signalsL[i].z   = z;
        m_signalsR[i].z   = z;
        m_arches[i].z     = z;
        m_archLights[i].z = z;
    }
    // Half a bay off the pillars; the spacing divides WRAP, so the phase survives wrapping.
    for (int i = 0; i < static_cast<int>(m_platformsL.size()); ++i) {
        m_platformsL[i].z = DESPAWN_Z + 5.0f  + static_cast<float>(i) * 40.0f;
        m_platformsR[i].z = DESPAWN_Z + 25.0f + static_cast<float>(i) * 40.0f;
        m_platEdgesL[i].z = m_platformsL[i].z;
        m_platEdgesR[i].z = m_platformsR[i].z;
    }

    scrollWorld(0.0f);   // lift the coins onto whatever roof is under them
    placeWorld(0.0f);

    LOG_INFO("Go! Ride the trains, slide under the gantries, grab the coins - dodge the red barriers.");
}

void PotionRunner::readInput() {
    // The tick's command, not the device: a tap between ticks still lands, and a
    // slow frame's press does not repeat on each of its ticks.
    const InputCommand& tick = command();
    const InputMap&     map  = input();

    m_edgeLeft    = map.pressed(tick, ACTION_LEFT);
    m_edgeRight   = map.pressed(tick, ACTION_RIGHT);
    m_edgeJump    = map.pressed(tick, ACTION_JUMP);
    m_edgeRestart = map.pressed(tick, ACTION_RESTART);

    m_crouchHeld = map.held(tick, ACTION_CROUCH);
}

void PotionRunner::updatePlayer(float dt) {
    if (m_edgeLeft)  m_lane = std::max(0, m_lane - 1);
    if (m_edgeRight) m_lane = std::min(2, m_lane + 1);

    Transform& t  = scene().get<Transform>(m_player);
    Rigidbody& rb = scene().get<Rigidbody>(m_player);

    // The solver owns Y; the behavior owns everything else.
    m_height = t.position.y - PLAYER_HALF_Y;

    // Not while rising: a jump's launch tick still touches the floor, which would
    // open a double-jump window.
    const bool standing = rb.supported && rb.supportNormal.y > 0.5f && rb.linearVelocity.y < 1.0f;
    m_coyoteTimer = standing ? COYOTE_TIME : std::max(0.0f, m_coyoteTimer - dt);
    m_grounded    = m_coyoteTimer > 0.0f;

    // Not left to the solver: a kinematic ramp moved by pose carries no velocity,
    // which position correction cannot out-climb. The solver owns the nose handoff.
    m_climbRamp = -1;
    for (size_t i = 0; i < m_obstacles.size(); ++i) {
        const Obstacle& o = m_obstacles[i];
        if (!o.hasRamp) continue;
        if (std::fabs(laneX(o.lane) - m_playerX) > obstacleHalfX() + PLAYER_HALF_X) continue;
        const float nose = o.z - o.length * 0.5f;
        const float toe  = nose - RAMP_RUN;
        if (toe > 0.0f || nose < 0.0f) continue;
        const float rampH = rampHeight(o, 0.0f);
        if (m_height > rampH + 0.4f)  continue;   // airborne above the slope: physics flies
        if (m_height < rampH - 1.2f)  continue;   // sidestepped into the tall side: no scoop
        t.position.y = rampH + PLAYER_HALF_Y;
        m_height     = rampH;
        rb.linearVelocity.y = std::max(rb.linearVelocity.y, 0.0f);   // gravity must not fight the climb
        m_grounded    = true;
        m_coyoteTimer = COYOTE_TIME;
        m_climbRamp   = static_cast<int>(i);
        break;
    }

    if (m_edgeJump && m_grounded) {
        rb.linearVelocity.y = jumpSpeed;
        m_grounded    = false;
        m_coyoteTimer = 0.0f;
        m_climbRamp   = -1;
    }

    // Lateral axes are pinned: x/z velocity from an angled contact (a ramp pushes
    // up and back) is cancelled, so the runner holds z = 0 and its lane.
    m_playerX = easeToward(m_playerX, laneX(m_lane), LANE_EASE, dt);
    rb.linearVelocity.x = 0.0f;
    rb.linearVelocity.z = 0.0f;

    // Crouch refits the collider too, bottom pinned at the feet, so a gantry
    // clearance is real physics.
    m_crouch = easeToward(m_crouch, crouchTarget(), CROUCH_EASE, dt);
    const float halfY = bodyHalfHeight(m_crouch);
    ColliderPart& box = scene().get<Collider>(m_player).parts[0];
    box.halfExtents.y = halfY;
    box.center.y      = halfY - PLAYER_HALF_Y;

    // Cadence follows the run and nearly freezes mid-air.
    const float cadence = m_grounded ? (0.85f + 1.15f * (m_speed / maxSpeed)) : 0.30f;
    scene().get<Animator>(m_player).speed = cadence;

    poseRunner(0.0f);
}

void PotionRunner::poseRunner(float ahead) {
    const float x      = easeToward(m_playerX, laneX(m_lane), LANE_EASE, ahead);
    const float crouch = easeToward(m_crouch, crouchTarget(), CROUCH_EASE, ahead);

    // Bank into the lane change; freezeRotation leaves rotation to the script.
    const float bank = std::clamp((x - laneX(m_lane)) * 0.18f, -0.35f, 0.35f);

    Transform& t = scene().get<Transform>(m_player);
    t.position.x = x;
    t.position.z = 0.0f;
    t.rotation   = glm::angleAxis(bank, Math::WORLD_AXIS_Z);
    t.scale      = {1.0f, bodyHalfHeight(crouch) / PLAYER_HALF_Y, 1.0f};
}

void PotionRunner::drawRunner(float ahead) {
    Transform& body = scene().get<Transform>(m_player);
    // Every frame until the next tick draws from the tick's pose.
    if (!m_drawnAhead) m_tickBody = body;
    m_drawnAhead = true;

    poseRunner(ahead);

    // On a ramp the rise comes from the slope, which velocity does not carry.
    const Obstacle* ramp = m_climbRamp >= 0 ? &m_obstacles[m_climbRamp] : nullptr;
    const float     rise = ramp
        ? rampHeight(*ramp, ahead) - rampHeight(*ramp, 0.0f)
        : scene().get<Rigidbody>(m_player).linearVelocity.y * ahead;
    body.position.y = m_tickBody.position.y + rise;
}

float PotionRunner::rampHeight(const Obstacle& o, float ahead) const {
    const float nose = obstacleZ(o, ahead) - o.length * 0.5f;
    return o.top * std::clamp(1.0f - nose / RAMP_RUN, 0.0f, 1.0f);
}

void PotionRunner::scrollWorld(float dt) {
    for (auto& o : m_obstacles) {
        o.z = obstacleZ(o, dt);
        if (o.z < DESPAWN_Z) {
            o.z += WRAP;
            randomizeObstacle(o);
        }
    }

    for (auto& c : m_coins) {
        c.z -= m_speed * dt;
        if (c.z < DESPAWN_Z) {
            c.z += WRAP;
            c.active = true;
            scene().get<Mesh>(c.entity).visible = true;
        }

        // On a train roof when one is under it, reachable only while riding.
        c.y = 1.0f;
        for (const auto& o : m_obstacles) {
            if (!o.rideable || o.top < 1.0f) continue;
            if (o.lane != c.lane) continue;
            if (std::fabs(o.z - c.z) > o.length * 0.5f) continue;
            c.y = o.top + 0.6f;
            break;
        }
    }

    m_sceneryScroll = std::fmod(m_sceneryScroll + m_speed * dt, WRAP);
}

void PotionRunner::placeScenery(const std::vector<Scenery>& pool, float x, float y, float ahead) {
    const float scroll = m_sceneryScroll + m_speed * ahead;
    for (const auto& s : pool) {
        float z = s.z - scroll;
        if (z < DESPAWN_Z) z += WRAP;
        scene().get<Transform>(s.entity).position = {x, y, z};
    }
}

void PotionRunner::placeWorld(float ahead) {
    for (const auto& o : m_obstacles) {
        const float x       = laneX(o.lane);
        const float z       = obstacleZ(o, ahead);
        const float centerY = (o.bottom + o.top) * 0.5f;
        scene().get<Transform>(o.entity).position = {x, centerY, z};
        scene().get<Transform>(o.accent).position =
            {x + o.accentOffset.x, centerY + o.accentOffset.y, z + o.accentOffset.z};
        // At the nose; harmlessly stale while disabled.
        scene().get<Transform>(o.lamp).position =
            {x, o.top * 0.58f, z - o.length * 0.5f - 0.12f};
        // Placed even when rampless (hidden, collider disabled), so nothing needs parking.
        scene().get<Transform>(o.ramp).position =
            {x, o.top * 0.5f - 0.05f, z - o.length * 0.5f - RAMP_RUN * 0.5f};
        // The lamp bar sits at the headlight's height, so glow and beam read as one fixture.
        if (o.isTrain) {
            const float nose = z - o.length * 0.5f;
            scene().get<Transform>(o.skirt).position   = {x, 0.21f, z};
            scene().get<Transform>(o.tail).position    =
                {x, 1.60f, z + o.length * 0.5f + 0.06f};
            scene().get<Transform>(o.plow).position    = {x, 0.30f, nose - 0.08f};
            scene().get<Transform>(o.lampBar).position =
                {x, o.top * 0.58f, nose - 0.05f};
        }
        if (o.auxVisible) {
            scene().get<Transform>(o.auxA).position =
                {x + o.auxAOffset.x, centerY + o.auxAOffset.y, z + o.auxAOffset.z};
            scene().get<Transform>(o.auxB).position =
                {x + o.auxBOffset.x, centerY + o.auxBOffset.y, z + o.auxBOffset.z};
        }
    }

    // Position only; the Animation owns rotation and scale.
    for (const auto& c : m_coins) {
        scene().get<Transform>(c.entity).position = {laneX(c.lane), c.y, c.z - m_speed * ahead};
    }

    for (int lane = 0; lane < 3; ++lane)
        placeScenery(m_ties[lane], laneX(lane), 0.05f, ahead);
    placeScenery(m_pillarsL,   -m_wallX, ARCH_Y * 0.5f,  ahead);
    placeScenery(m_pillarsR,    m_wallX, ARCH_Y * 0.5f,  ahead);
    placeScenery(m_signalsL, -(m_wallX - 0.28f), 1.42f,  ahead);
    placeScenery(m_signalsR,  (m_wallX - 0.28f), 1.42f,  ahead);
    placeScenery(m_arches,      0.0f,    ARCH_Y,         ahead);
    placeScenery(m_archLights,  0.0f,    ARCH_Y - 0.33f, ahead);
    placeScenery(m_platformsL, -(m_wallX - 0.48f), 0.50f, ahead);
    placeScenery(m_platformsR,  (m_wallX - 0.48f), 0.50f, ahead);
    placeScenery(m_platEdgesL, -(m_wallX - 0.48f), 1.03f, ahead);
    placeScenery(m_platEdgesR,  (m_wallX - 0.48f), 1.03f, ahead);

    // Cube shadows only for the fixtures nearest the player: the atlas assigns in
    // entity-slot order (GLShadowData::build), so the caster count must stay at budget.
    for (size_t i = 0; i < m_archLights.size(); ++i) {
        const float z = scene().get<Transform>(m_archLights[i].entity).position.z;
        Light& wash = scene().get<Light>(m_archLights[i].entity);
        wash.castShadows = (z > -6.0f && z < 14.0f);
        // Every fourth fixture flickers.
        if (i % 4 == 0) {
            const float slow = std::sin(m_camTime * 9.0f + z);
            const float fast = std::sin(m_camTime * 23.0f);
            wash.intensity = 45.0f * (0.78f + 0.22f * slow * fast);
        }
    }
}

void PotionRunner::updateCamera(float dt) {
    Transform* view = scene().tryGet<Transform>(m_camera);
    if (!view) return;

    m_camTime += dt;
    const float k = 1.0f - std::exp(-dt * 8.0f);
    // A crash holds the eye where it was, watching the ragdoll.
    if (m_started && m_alive) {
        const glm::vec3& body = scene().get<Transform>(m_player).position;
        m_camFollow = {body.x * 0.5f, CAMERA_REST_Y + (body.y - PLAYER_HALF_Y) * 0.35f};
    }
    // The start screen drifts in a figure-eight; the run eases into the chase, no cut.
    const float targetX = m_started
        ? m_camFollow.x
        : std::sin(m_camTime * 0.35f) * 1.4f;
    const float targetY = m_started
        ? m_camFollow.y
        : CAMERA_REST_Y + std::sin(m_camTime * 0.23f) * 0.35f;
    m_camX += (targetX - m_camX) * k;
    m_camY += (targetY - m_camY) * k;

    if (Camera* cam = scene().tryGet<Camera>(m_camera)) {
        const float norm = maxSpeed > startSpeed
            ? std::clamp((m_speed - startSpeed) / (maxSpeed - startSpeed), 0.0f, 1.0f)
            : 0.0f;
        cam->fovY += (glm::radians(68.0f + 13.0f * norm) - cam->fovY) * k;
    }

    Transform& t = *view;
    t.position = {m_camX, m_camY, CAMERA_BACK_Z};
    // Turned to look down +Z, since forward is -Z.
    t.rotation = glm::angleAxis(CAMERA_PITCH, Math::WORLD_AXIS_X)
               * glm::angleAxis(glm::pi<float>(), Math::WORLD_AXIS_Y);
}

void PotionRunner::die() {
    if (!m_alive) return;
    m_alive = false;
    m_speed = 0.0f;
    for (auto& [part, mat] : m_playerParts)
        scene().get<Mesh>(part).material = m_matBarrier;

    // A running Animator would swing limbs and announce footsteps on the tumbling body.
    scene().get<Animator>(m_player).playing = false;

    // Unfrozen to tumble, the full box restored in case of a mid-slide death, then launched.
    Rigidbody& rb = scene().get<Rigidbody>(m_player);
    rb.freezeRotation = false;
    rb.restitution    = 0.45f;
    rb.friction       = 0.5f;
    rb.linearVelocity  = {(frand() * 2.0f - 1.0f) * 3.5f, 8.0f + frand() * 3.0f, -3.0f - frand() * 2.5f};
    rb.angularVelocity = {
        (frand() * 2.0f - 1.0f) * 8.0f,
        (frand() * 2.0f - 1.0f) * 8.0f,
        (frand() * 2.0f - 1.0f) * 8.0f
    };
    ColliderPart& box = scene().get<Collider>(m_player).parts[0];
    box.halfExtents.y = PLAYER_HALF_Y;
    box.center.y      = 0.0f;

    const int score = static_cast<int>(m_distance) + m_coinCount * coinValue + m_bonusScore;
    m_newBest = score > m_best;
    if (m_newBest) m_best = score;

    // Written once: nothing on the game-over screen changes until the next run.
    if (UIText* text = scene().tryGet<UIText>(m_uiFinalScore))
        text->text = "SCORE  " + std::to_string(score);
    if (UIText* text = scene().tryGet<UIText>(m_uiFinalDist))
        text->text = "DIST  " + std::to_string(static_cast<int>(m_distance)) + " m";
    if (UIText* text = scene().tryGet<UIText>(m_uiFinalCoins))
        text->text = "COINS  " + std::to_string(m_coinCount);
    if (UIText* held = scene().tryGet<UIText>(m_uiFinalBest)) {
        UIText& best = *held;
        best.text  = (m_newBest ? "NEW BEST  " : "BEST  ") + std::to_string(m_best);
        best.color = m_newBest
            ? glm::vec4(1.0f, 0.84f, 0.30f, 1.0f)
            : glm::vec4(0.62f, 0.68f, 0.78f, 1.0f);
    }

    LOG_INFO(
        "Crash! Score %d  (distance %d, coins %d%s). Press R or Enter to run again.",
        score,
        static_cast<int>(m_distance),
        m_coinCount,
        m_newBest ? ", new best" : ""
    );
}

void PotionRunner::buildUI() {
    auto makeElement = [&](const char* name, glm::vec2 at, glm::vec2 pos, glm::vec2 size, EntityId parent) {
        const EntityId e = spawn(name, parent);
        scene().add(e, UIElement::at(at, pos, size));
        return e;
    };
    auto makeText = [&](
        const char* name,
        std::string text,
        float px,
        glm::vec4 color,
        UIText::Align align,
        glm::vec2 at,
        glm::vec2 pos,
        glm::vec2 size,
        EntityId parent
    ) {
        EntityId e = makeElement(name, at, pos, size, parent);
        UIText t;
        t.text = std::move(text); t.pixelSize = px; t.color = color; t.align = align;
        scene().add(e, std::move(t));
        return e;
    };

    // Anchors: the point of the parent an element pins to, and of itself.
    const glm::vec2 TL{0.0f, 0.0f};
    const glm::vec2 C {0.5f, 0.5f};
    const glm::vec2 TC{0.5f, 0.0f};
    const glm::vec4 WHITE{1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 GREY {0.62f, 0.68f, 0.78f, 1.0f};
    const glm::vec4 GOLD {1.0f, 0.84f, 0.30f, 1.0f};
    const glm::vec4 CYAN {0.35f, 0.90f, 1.00f, 1.0f};
    const glm::vec4 MAG  {0.95f, 0.32f, 1.00f, 1.0f};
    const glm::vec4 REDH {1.00f, 0.36f, 0.30f, 1.0f};
    const glm::vec4 INK  {0.02f, 0.03f, 0.06f, 0.84f};  // HUD backing
    const glm::vec4 INK2 {0.04f, 0.05f, 0.10f, 0.95f};  // modal panels

    auto makeImage = [&](
        const char* name,
        glm::vec4 color,
        glm::vec2 at,
        glm::vec2 pos,
        glm::vec2 size,
        EntityId parent
    ) {
        EntityId e = makeElement(name, at, pos, size, parent);
        scene().add(e, UIImage{color, {}, {}});
        return e;
    };
    // A faint larger copy behind a bar makes a neon glow; the earlier sibling draws
    // behind (see UISystem::resolveElement).
    auto makeGlowBar = [&](glm::vec4 color, glm::vec2 at, glm::vec2 pos, glm::vec2 size, EntityId parent) {
        makeImage(
            "Glow",
            glm::vec4(color.r, color.g, color.b, 0.22f),
            at,
            pos - glm::vec2(6.0f, 4.0f),
            size + glm::vec2(12.0f, 8.0f),
            parent
        );
        makeImage("Bar", color, at, pos, size, parent);
    };
    auto makeNeonButton = [&](
        const char* name,
        const char* label,
        const char* event,
        glm::vec4 base,
        glm::vec4 hot,
        glm::vec2 pos,
        glm::vec2 size,
        EntityId parent
    ) {
        makeImage(
            "Btn Glow",
            glm::vec4(hot.r, hot.g, hot.b, 0.30f),
            TC,
            pos - glm::vec2(0.0f, 7.0f),
            size + glm::vec2(16.0f, 14.0f),
            parent
        );
        EntityId b = makeElement(name, TC, pos, size, parent);
        UIButton btn;
        btn.eventId      = event;
        btn.normalColor  = base;
        btn.hoverColor   = hot;
        btn.pressedColor = glm::vec4(base.r * 0.65f, base.g * 0.65f, base.b * 0.65f, 0.96f);
        scene().add(b, std::move(btn));
        // The label shares the button's rect: UISystem::resolveElement draws text after the button.
        UIText caption;
        caption.text      = label;
        caption.pixelSize = size.y * 0.45f;
        caption.color     = WHITE;
        caption.align     = UIText::Align::Center;
        caption.valign    = UIText::VAlign::Middle;
        scene().add(b, std::move(caption));
        return b;
    };

    EntityId hud = spawn("Potion HUD");
    scene().add(hud, UICanvas{});

    EntityId hudPanel = makeImage("HUD Panel", INK, TL, {20.0f, 18.0f}, {326.0f, 158.0f}, hud);
    const glm::vec4 edgeGlow = glm::vec4(CYAN.r, CYAN.g, CYAN.b, 0.25f);
    const glm::vec4 topColor = glm::vec4(CYAN.r, CYAN.g, CYAN.b, 0.55f);
    makeImage("HUD Edge Glow", edgeGlow, TL, {0.0f, 0.0f}, {10.0f, 158.0f}, hudPanel);
    makeImage("HUD Edge",      CYAN,     TL, {0.0f, 0.0f}, {4.0f, 158.0f},  hudPanel);
    makeImage("HUD Top",       topColor, TL, {0.0f, 0.0f}, {326.0f, 3.0f},  hudPanel);

    m_uiScore = makeText(
        "HUD Score",
        "SCORE  0",
        30.0f,
        WHITE,
        UIText::Align::Left,
        TL,
        {22.0f, 10.0f},
        {296.0f, 38.0f},
        hudPanel
    );
    m_uiDist  = makeText(
        "HUD Dist",
        "DIST  0 m",
        22.0f,
        CYAN,
        UIText::Align::Left,
        TL,
        {22.0f, 54.0f},
        {296.0f, 30.0f},
        hudPanel
    );
    makeImage("HUD Coin Pip", GOLD, TL, {23.0f, 93.0f}, {15.0f, 15.0f}, hudPanel);
    m_uiCoins = makeText(
        "HUD Coins",
        "0",
        24.0f,
        GOLD,
        UIText::Align::Left,
        TL,
        {48.0f, 88.0f},
        {270.0f, 30.0f},
        hudPanel
    );
    m_uiSpeed = makeText(
        "HUD Speed",
        "SPEED  0",
        20.0f,
        GREY,
        UIText::Align::Left,
        TL,
        {22.0f, 124.0f},
        {296.0f, 26.0f},
        hudPanel
    );

    // Shown only on a roof; UIElement visibility hides the whole subtree, label included.
    EntityId ride = makeImage(
        "Ride Tag",
        glm::vec4(GOLD.r, GOLD.g, GOLD.b, 0.18f),
        TC,
        {0.0f, 16.0f},
        {200.0f, 38.0f},
        hud
    );
    scene().get<UIElement>(ride).visible = false;
    m_uiRideTag = ride;
    EntityId rideText = makeText(
        "Ride Tag Text",
        "ROOF RIDE  2x",
        20.0f,
        GOLD,
        UIText::Align::Center,
        C,
        {0.0f, 0.0f},
        {200.0f, 38.0f},
        ride
    );
    scene().get<UIText>(rideText).valign = UIText::VAlign::Middle;

    // Armed every 500 m by onFixedUpdate, shown while its timer runs.
    m_uiMilestone = makeText(
        "Milestone",
        "500 m",
        46.0f,
        CYAN,
        UIText::Align::Center,
        TC,
        {0.0f, 78.0f},
        {420.0f, 56.0f},
        hud
    );
    scene().get<UIElement>(m_uiMilestone).visible = false;

    EntityId start = spawn("Potion Start");
    UICanvas startCanvas;
    startCanvas.sortOrder = 5;
    scene().add(start, std::move(startCanvas));
    m_startCanvas = start;

    EntityId startPanel = makeImage("Start Panel", INK2, C, {0.0f, 0.0f}, {680.0f, 400.0f}, start);
    makeGlowBar(CYAN, TC, {0.0f,   0.0f}, {680.0f, 4.0f}, startPanel);
    makeGlowBar(MAG,  TC, {0.0f, 396.0f}, {680.0f, 4.0f}, startPanel);
    makeText(
        "Start Title",
        "POTION RUNNER",
        60.0f,
        CYAN,
        UIText::Align::Center,
        TC,
        {0.0f, 44.0f},
        {660.0f, 70.0f},
        startPanel
    );
    makeGlowBar(MAG, TC, {0.0f, 120.0f}, {300.0f, 4.0f}, startPanel);
    makeText(
        "Start Subtitle",
        "ENDLESS RUNNER",
        22.0f,
        GREY,
        UIText::Align::Center,
        TC,
        {0.0f, 134.0f},
        {660.0f, 28.0f},
        startPanel
    );
    makeNeonButton(
        "Start Button",
        "START",
        "potion:start",
        glm::vec4(0.06f, 0.44f, 0.42f, 0.96f),
        glm::vec4(0.14f, 0.78f, 0.72f, 0.98f),
        {0.0f, 196.0f},
        {260.0f, 64.0f},
        startPanel
    );

    // The box is shorter than its paragraph, so the UIScroll has something to scroll.
    makeText(
        "Help Title",
        "HOW TO PLAY",
        16.0f,
        CYAN,
        UIText::Align::Center,
        TC,
        {0.0f, 274.0f},
        {660.0f, 20.0f},
        startPanel
    );
    EntityId help = makeImage(
        "Help Box",
        glm::vec4(0.0f, 0.0f, 0.0f, 0.35f),
        TC,
        {0.0f, HELP_TOP},
        {620.0f, HELP_HEIGHT},
        startPanel
    );
    scene().add(help, UIScroll{});
    m_helpBox = help;
    const char* briefingText =
        "A or D moves a lane. SPACE jumps. S slides under a gantry.\n"
        "R restarts once you have crashed.\n"
        "\n"
        "Run into the pale ramp on a train's nose and it walks you onto the "
        "roof - the jump is deliberately too short to board one from the "
        "ground, so the ramp is the way up.\n"
        "\n"
        "Coins picked up on a roof pay double, and every few trains start a "
        "convoy in one lane with hoppable gaps, so the fastest line is along "
        "the roofs rather than the trackbed.\n"
        "\n"
        "Scroll this box with the wheel.";
    EntityId briefing = makeText(
        "Help Text",
        briefingText,
        15.0f,
        GREY,
        UIText::Align::Left,
        TL,
        {10.0f, 4.0f},
        {600.0f, 320.0f},
        help
    );
    scene().get<UIText>(briefing).wrap = true;

    // Siblings, not children: a scroll view's child scrolls with the content. See ui.md.
    makeImage(
        "Help Track",
        glm::vec4(1.0f, 1.0f, 1.0f, 0.07f),
        TC,
        {HELP_BAR_X, HELP_TOP},
        {HELP_BAR_W, HELP_HEIGHT},
        startPanel
    );
    m_helpThumb = makeImage(
        "Help Thumb",
        glm::vec4(CYAN.r, CYAN.g, CYAN.b, 0.55f),
        TC,
        {HELP_BAR_X, HELP_TOP},
        {HELP_BAR_W, HELP_HEIGHT},
        startPanel
    );

    makeText(
        "Start Tip",
        "roof coins pay double",
        15.0f,
        GOLD,
        UIText::Align::Center,
        TC,
        {0.0f, 366.0f},
        {660.0f, 22.0f},
        startPanel
    );

    EntityId over = spawn("Potion Game Over");
    UICanvas overCanvas;
    overCanvas.sortOrder = 10;
    overCanvas.visible   = false;
    scene().add(over, std::move(overCanvas));
    m_gameOverCanvas = over;

    EntityId panel = makeImage("Game Over Panel", INK2, C, {0.0f, 0.0f}, {600.0f, 400.0f}, over);
    makeGlowBar(REDH, TC, {0.0f,   0.0f}, {600.0f, 4.0f}, panel);
    makeGlowBar(MAG,  TC, {0.0f, 396.0f}, {600.0f, 4.0f}, panel);
    makeText(
        "Game Over Title",
        "GAME OVER",
        60.0f,
        REDH,
        UIText::Align::Center,
        TC,
        {0.0f, 40.0f},
        {560.0f, 70.0f},
        panel
    );
    makeGlowBar(REDH, TC, {0.0f, 116.0f}, {300.0f, 4.0f}, panel);
    m_uiFinalScore = makeText(
        "Game Over Score",
        "SCORE  0",
        34.0f,
        WHITE,
        UIText::Align::Center,
        TC,
        {0.0f, 138.0f},
        {560.0f, 40.0f},
        panel
    );
    m_uiFinalDist  = makeText(
        "Game Over Dist",
        "DIST  0 m",
        24.0f,
        CYAN,
        UIText::Align::Center,
        TC,
        {0.0f, 186.0f},
        {560.0f, 32.0f},
        panel
    );
    m_uiFinalCoins = makeText(
        "Game Over Coins",
        "COINS  0",
        24.0f,
        GOLD,
        UIText::Align::Center,
        TC,
        {0.0f, 222.0f},
        {560.0f, 32.0f},
        panel
    );
    m_uiFinalBest  = makeText(
        "Game Over Best",
        "BEST  0",
        24.0f,
        GREY,
        UIText::Align::Center,
        TC,
        {0.0f, 254.0f},
        {560.0f, 30.0f},
        panel
    );
    makeNeonButton(
        "Restart Button",
        "RESTART",
        "potion:restart",
        glm::vec4(0.08f, 0.40f, 0.62f, 0.96f),
        glm::vec4(0.16f, 0.60f, 0.86f, 0.98f),
        {0.0f, 292.0f},
        {240.0f, 56.0f},
        panel
    );
    makeText(
        "Restart Hint",
        "or press  R / Enter",
        17.0f,
        GREY,
        UIText::Align::Center,
        TC,
        {0.0f, 360.0f},
        {560.0f, 24.0f},
        panel
    );
}

void PotionRunner::refreshUI() {
    const int score = static_cast<int>(m_distance) + m_coinCount * coinValue + m_bonusScore;

    if (UIText* text = score != m_shownScore ? scene().tryGet<UIText>(m_uiScore) : nullptr) {
        text->text = "SCORE  " + std::to_string(score);
        m_shownScore = score;
    }
    const int metres = static_cast<int>(m_distance);
    if (UIText* text = metres != m_shownDist ? scene().tryGet<UIText>(m_uiDist) : nullptr) {
        text->text  = "DIST  " + std::to_string(metres) + " m";
        m_shownDist = metres;
    }
    if (UIText* text = m_coinCount != m_shownCoins ? scene().tryGet<UIText>(m_uiCoins) : nullptr) {
        text->text   = std::to_string(m_coinCount);
        m_shownCoins = m_coinCount;
    }
    const int speed = static_cast<int>(m_speed);
    if (UIText* text = speed != m_shownSpeed ? scene().tryGet<UIText>(m_uiSpeed) : nullptr) {
        text->text   = "SPEED  " + std::to_string(speed);
        m_shownSpeed = speed;
    }

    // Roof height only: a hurdle (0.7) or a jump apex is not a ride.
    if (UIElement* pill = scene().tryGet<UIElement>(m_uiRideTag)) {
        pill->visible = m_started && m_alive && m_grounded && m_height > 1.5f;
    }
    if (UIElement* flash = scene().tryGet<UIElement>(m_uiMilestone)) {
        flash->visible = m_alive && m_milestoneTimer > 0.0f;
    }

    // The thumb's share of the track is the view's share of the content.
    // viewSize is zero before the first walk.
    const UIScroll* box   = scene().tryGet<UIScroll>(m_helpBox);
    UIElement*      thumbEl = box ? scene().tryGet<UIElement>(m_helpThumb) : nullptr;
    if (thumbEl) {
        const UIScroll& scroll = *box;
        UIElement&      thumb  = *thumbEl;
        const float     view   = scroll.viewSize.y;
        const float     travel = scroll.range().y;

        thumb.visible = view > 0.0f && travel > 0.0f;
        if (thumb.visible) {
            thumb.size.y     = view * (view / scroll.contentSize.y);
            thumb.position.y = HELP_TOP + (scroll.offset.y / travel) * (view - thumb.size.y);
        }
    }

    if (UICanvas* start = scene().tryGet<UICanvas>(m_startCanvas)) {
        start->visible = !m_started;
    }
    if (UICanvas* over = scene().tryGet<UICanvas>(m_gameOverCanvas)) {
        over->visible = m_started && !m_alive;
    }
}

} // namespace Potion

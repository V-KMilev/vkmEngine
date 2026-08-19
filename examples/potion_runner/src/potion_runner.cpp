#include "potion_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "ecs/scene.h"
#include "ecs/component/animation.h"
#include "ecs/component/camera.h"
#include "ecs/component/collider.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "ecs/component/ui_canvas.h"
#include "ecs/component/ui_element.h"
#include "ecs/component/ui_image.h"
#include "ecs/component/ui_text.h"
#include "ecs/component/ui_button.h"
#include "platform/input/input_map.h"
#include "platform/window/input_handle.h"
#include "platform/window/glfw_include.h"
#include "proc_mesh.h"
#include "resource/resource_manager.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/physics_events.h"
#include "system/ui/ui_events.h"

// Player-facing messages go to the console via the C runtime rather than the
// engine's logger: this code is also compiled as the editor's hot-reload module,
// which resolves symbols from the host exe - and Logger lives in a separate DLL
// the exe can't re-export. stderr is unbuffered, so messages show immediately.
#define POTION_LOG(...) (std::fprintf(stderr, "[Potion] " __VA_ARGS__), std::fputc('\n', stderr))

namespace Engine {

namespace {

// Track layout. The world scrolls toward the camera along -Z while the player
// stays at z = 0; obstacles/coins/scenery are pooled and recycled by wrapping
// WRAP units back to the far end the moment they pass behind the camera.
constexpr int   OBSTACLE_COUNT = 9;
constexpr int   COIN_COUNT     = 44;
constexpr int   TIE_COUNT      = 56;   // sleepers under the rails; denser = more sense of speed
constexpr int   PILLAR_COUNT   = 16;   // per side; the arches share this lattice

// Overhead arch height = the pillar height that holds it, forming a portal the
// player runs through. Sized for the tall trains: a jump taken ON a roof puts
// the head at TRAIN_TOP + jump apex + body (~5.6), so the lattice sits above
// that - and above the camera (y ~= 4.9) - with clearance to spare.
constexpr float ARCH_Y          = 6.6f;

constexpr float SPAWN_Z         = 140.0f;  // far end where recycled props reappear
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

// Player box half extents and the y of its feet/centre while grounded.
constexpr float PLAYER_HALF_X = 0.42f;
constexpr float PLAYER_HALF_Z = 0.42f;
constexpr float PLAYER_HALF_Y = 0.7f;
// Body half-height while fully crouched: the rig squashes to this so the head
// drops from 2*PLAYER_HALF_Y (1.4) to 2*CROUCH_HALF_Y (0.9), low enough to clear
// an overhead gantry whose underside sits just above it.
constexpr float CROUCH_HALF_Y = 0.45f;

// One full stride (both limbs swing out and back) at cadence 1. updatePlayer
// scales each pivot's Animation::speed with the run speed, so the cycle
// quickens as the track does.
constexpr float RUN_PERIOD = 0.55f;

// Train roof height: properly Subway-Surfers tall, ~1.8x the runner, so cars
// read as vehicles rather than crates - and deliberately above the jump apex,
// so from the ground you board via the nose ramps, never by jumping.
constexpr float TRAIN_TOP = 2.5f;

// Ground run of a train's boarding ramp (the slope rises TRAIN_TOP over this
// distance - ~31 degrees, a runnable grade). Steady, non-convoy-follower
// trains carry one at the leading face; running into it walks you up.
constexpr float RAMP_RUN = 4.2f;

// The runner's actions. The alternate keys for each are bindings, not an ||
// chain at the read site, which is also what lets them be rebound.
constexpr const char* ACTION_LEFT    = "Runner/Left";
constexpr const char* ACTION_RIGHT   = "Runner/Right";
constexpr const char* ACTION_JUMP    = "Runner/Jump";
constexpr const char* ACTION_CROUCH  = "Runner/Crouch";
constexpr const char* ACTION_RESTART = "Runner/Restart";

/**
 * @brief Bind the runner's actions, each to every key that should trigger it.
 */
void installRunnerBindings(InputMap& map) {
    const auto key = [](int code) { return InputBinding{InputSource::Key, code, 1.0f}; };
    map.define(ACTION_LEFT,    { key(GLFW_KEY_A),     key(GLFW_KEY_LEFT) });
    map.define(ACTION_RIGHT,   { key(GLFW_KEY_D),     key(GLFW_KEY_RIGHT) });
    map.define(ACTION_JUMP,    { key(GLFW_KEY_SPACE), key(GLFW_KEY_W), key(GLFW_KEY_UP) });
    map.define(ACTION_CROUCH,  { key(GLFW_KEY_S),     key(GLFW_KEY_DOWN), key(GLFW_KEY_LEFT_CONTROL) });
    map.define(ACTION_RESTART, { key(GLFW_KEY_R),     key(GLFW_KEY_ENTER) });
}

// Every run reseeds the PCG32 with this, so the track deals the same layout
// each time - a death is always retryable against the identical sequence.
constexpr uint64_t RUN_SEED = 0x9E3779B9u;

// How long a grounding contact keeps the runner "grounded". Contacts arrive
// once per fixed tick via the event flush, so this bridges that latency - and
// its tail doubles as coyote time at ledges.
constexpr float GROUNDED_GRACE = 0.12f;

/**
 * @brief A looping limb swing for the AnimationSystem: rotate about X between
 *        -amplitude and +amplitude with a sine ease, starting @p phase seconds
 *        into the cycle (opposing limbs start half a period apart).
 */
Animation makeSwing(float amplitude, float phase) {
    Animation anim;
    anim.rotationTrack.setEasing(Easing::byName("easeInOutSine"));
    anim.rotationTrack.addKeyframe(0.0f,             glm::angleAxis(-amplitude, Math::WORLD_AXIS_X));
    anim.rotationTrack.addKeyframe(RUN_PERIOD * 0.5f, glm::angleAxis( amplitude, Math::WORLD_AXIS_X));
    anim.rotationTrack.addKeyframe(RUN_PERIOD,        glm::angleAxis(-amplitude, Math::WORLD_AXIS_X));
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

/**
 * @brief A coin's idle motion: a constant-rate full revolution about Y (four
 *        120-degree keys, so each slerp takes the short way round) plus a soft
 *        scale pulse. Replaces the old hand-advanced spin in scrollWorld.
 */
Animation makeCoinSpin(float phase) {
    constexpr float PERIOD = 1.4f;
    Animation anim;
    anim.rotationTrack.setEasing(&Easing::linear);
    for (int k = 0; k <= 3; ++k) {
        anim.rotationTrack.addKeyframe(
            PERIOD * static_cast<float>(k) / 3.0f,
            glm::angleAxis(glm::two_pi<float>() * static_cast<float>(k) / 3.0f, Math::WORLD_AXIS_Y));
    }
    anim.scaleTrack.setEasing(Easing::byName("easeInOutSine"));
    anim.scaleTrack.addKeyframe(0.0f,           {0.70f, 0.70f, 0.12f});
    anim.scaleTrack.addKeyframe(PERIOD * 0.5f,  {0.80f, 0.80f, 0.16f});
    anim.scaleTrack.addKeyframe(PERIOD,         {0.70f, 0.70f, 0.12f});
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

} // namespace

void PotionRunner::onStart() {
    // Cache the engine handles from the (session-stable) context; every step
    // below reaches the scene / resources / window through these.
    m_scene     = context().scene;
    m_resources = context().resources;
    m_window    = context().window;

    installRunnerBindings(*context().input);

    if (m_built) return;
    buildWorld();
    buildUI();
    // Mouse clicks on the START / RESTART buttons drive the same state changes
    // as the keyboard.
    subscribe<UIClickEvent>([this](const UIClickEvent& e) {
        if (e.eventId == "potion:start") m_started = true;
        else if (e.eventId == "potion:restart" && !m_alive) resetGame();
    });
    // The player is a dynamic body, so the solver reports every contact it
    // makes. This one handler is the whole interaction ruleset:
    //  - dead: each ragdoll slam kicks the camera;
    //  - a mostly-up contact normal = standing on something (ground, roof,
    //    ramp slope) -> arm the grounded grace timer;
    //  - any other contact with an obstacle HULL (leading face, side, or a
    //    gantry's underside) = the crash.
    // `normal` points a -> b, so flip it when the player is `a` to get the
    // surface normal as the player feels it.
    subscribe<CollisionEvent>([this](const CollisionEvent& e) {
        const EntityId player = m_player;
        const bool playerIsA = (e.a == player);
        if (!playerIsA && e.b != player) return;

        if (!m_alive) return;   // dead: the ragdoll IS the crash feedback

        const float    up    = playerIsA ? -e.normal.y : e.normal.y;
        const EntityId other = playerIsA ? e.b : e.a;
        for (const auto& o : m_obstacles) {
            if (other != o.entity) continue;
            // A non-top contact is a crash - UNLESS the feet are already
            // near the roof line. That grace covers the ramp-to-roof seam
            // (the box grazes the hull face just under the roof on the way
            // up; the next tick's contact pops it on top) and forgives hurdle
            // hops and convoy roof-hops that land a hair short. Floating
            // gantries are exempt: a rider's feet CAN sit near the bar's top
            // while their head is inside it, so any contact with a bar you
            // were meant to duck under stays lethal.
            const bool grace = o.bottom <= 0.0f && m_height >= o.top - 0.6f;
            if (up < 0.7f && !grace) { die(); return; }
            break;
        }
        // Arm grounded only when not ascending: the jump's launch tick still
        // overlaps the floor, and letting that contact re-arm the grace timer
        // made the runner count as "grounded" for the first airborne moments
        // (flashing the ride pill mid-jump and opening a double-jump window).
        if (up > 0.5f && m_scene->get<Rigidbody>(m_player).linearVelocity.y < 1.0f) {
            m_grounded      = true;
            m_groundedTimer = GROUNDED_GRACE;
        }
    });
    // Coin pickup rides the physics trigger pipeline: the coin's trigger
    // volume overlaps the dynamic player and the narrowphase reports it.
    subscribe<TriggerEvent>([this](const TriggerEvent& e) {
        if (!m_alive || e.other != m_player) return;
        for (auto& c : m_coins) {
            if (e.trigger != c.entity) continue;
            if (!c.active) return;
            c.active = false;
            m_scene->get<Mesh>(c.entity).visible = false;
            ++m_coinCount;
            // Roof coins pay double - the payoff the ROOF RIDE pill advertises.
            if (c.y > 1.5f) m_bonusScore += coinValue;
            if (m_coinCount % 10 == 0) POTION_LOG("Coins: %d", m_coinCount);
            return;
        }
    });
}

void PotionRunner::onUpdate(float dt) {
    if (!m_built) return;

    readInput();

    // Hold on the start screen until the player gives any run input (a lane move,
    // a jump, or a click on the START button).
    if (!m_started) {
        if (m_edgeJump || m_edgeLeft || m_edgeRight) m_started = true;
        updateCamera(dt);
        refreshUI();
        return;
    }

    if (m_alive) {
        m_speed = std::min(maxSpeed, m_speed + acceleration * dt);
        m_distance += m_speed * dt;

        // Crashes and coin pickups arrive as physics events (see onStart);
        // there is no polling pass anymore.
        updatePlayer(dt);
        scrollWorld(dt);

        if (m_milestoneTimer > 0.0f) m_milestoneTimer -= dt;
        if (m_distance >= m_nextDistanceLog) {
            POTION_LOG("Distance %d  (coins %d)", static_cast<int>(m_distance), m_coinCount);
            // Flash the milestone centre-screen for a couple of seconds.
            if (m_scene->isAlive(m_uiMilestone)) {
                m_scene->get<UIText>(m_uiMilestone).text =
                    std::to_string(static_cast<int>(m_nextDistanceLog)) + " m";
            }
            m_milestoneTimer   = 2.2f;
            m_nextDistanceLog += 500.0f;
        }
    } else if (m_edgeRestart) {
        resetGame();
    }

    updateCamera(dt);
    refreshUI();
}

MaterialHandle PotionRunner::makeMaterial(
    const glm::vec3& albedo, float metallic, float roughness,
    const glm::vec3& emission, float emissiveStrength, bool unlit, const char* name) {
    MaterialAsset material;
    material.type      = unlit ? MaterialType::Unlit : MaterialType::Opaque;
    material.albedo    = glm::vec4(albedo, 1.0f);
    material.metallic  = metallic;
    material.roughness = roughness;
    material.emission  = emission;
    material.emissiveStrength = emissiveStrength;
    // No texture handles are set: GLMaterial keys each map's "is bound" flag off
    // a valid handle, so the shader falls back to these scalars cleanly.
    return m_resources->add(std::move(material), name);
}

EntityId PotionRunner::spawnBox(MeshHandle mesh, MaterialHandle material, const char* name) {
    // spawn() is just m_scene->createEntity(); call it directly so the editor's
    // hot-reload module needn't import that (otherwise GC'd) host-exe symbol.
    EntityId entity = m_scene->createEntity();
    m_scene->add(entity, makeName(name));
    m_scene->add(entity, Mesh{mesh, material});
    m_scene->add(entity, Transform{});
    return entity;
}

void PotionRunner::buildWorld() {
    // Own the mood: this is a night run, and lights only read against dark.
    // Near-zero image-based ambient (the skybox dims with it); the ceiling
    // pools and train headlights below do the actual lighting.
    // Matches potion_scene.h, but enforced here so the scene file and the game
    // can't drift apart.
    m_scene->environment().sky.intensity  = 0.08f;
    m_scene->environment().sky.showSkybox = false;   // underground: no sky, just the tunnel
    // No sun underground - switch off any authored directional light (the saved
    // editor scene still carries one). This frees the whole 2D shadow atlas for
    // the train headlights: without a directional caster no CSM layers are
    // reserved, so spots get all six slots.
    m_scene->forEach<Light>([](EntityId, Light& light) {
        if (light.type == LightType::Directional) light.enabled = false;
    });
    // The ragdoll should fall with the same weight as the jump arc, not the
    // default earth gravity - the crash reads floaty otherwise.
    m_scene->physics().gravity = {0.0f, -gravity, 0.0f};

    m_cubeMesh   = m_resources->add(makeCubeMesh(), "potion:cube");

    // Materials. Emission is reserved for things that genuinely glow (fixtures,
    // lamps, pickups); everything structural is lit by the real Lights below
    // plus the faint image-based ambient.
    //
    // Trackbed & structure: matte, and NOT pitch black. Lighting here is
    // physically attenuated (inverse-square), so a 2% albedo floor cannot show
    // a light pool no matter how strong the lamp - surfaces need plausible
    // night reflectance (5-12%) for the pools to register. Roughness sits near
    // 1 so grazing-angle Fresnel doesn't sheen the long walls glossy; the only
    // deliberately reflective surface is the polished steel rail.
    m_matGround  = makeMaterial({0.030f, 0.032f, 0.037f}, 0.0f,  0.96f, {0,0,0}, 1.0f, false, "potion:ground");
    m_matBallast = makeMaterial({0.050f, 0.050f, 0.056f}, 0.0f,  0.96f, {0,0,0}, 1.0f, false, "potion:ballast"); // coarse gravel bed
    m_matRail    = makeMaterial({0.52f,  0.54f,  0.58f},  1.0f,  0.55f, {0,0,0}, 1.0f, false, "potion:rail");   // brushed steel
    m_matTie     = makeMaterial({0.085f, 0.062f, 0.042f}, 0.0f,  0.95f, {0,0,0}, 1.0f, false, "potion:tie");     // creosote sleeper
    m_matWall    = makeMaterial({0.055f, 0.058f, 0.070f}, 0.0f,  0.95f, {0,0,0}, 1.0f, false, "potion:wall");    // concrete
    m_matPillar  = makeMaterial({0.070f, 0.073f, 0.085f}, 0.0f,  0.93f, {0,0,0}, 1.0f, false, "potion:pillar");  // concrete column

    // Emissives are accents, not light sources: strengths sit just over the
    // bloom threshold so they halo softly, while the real Lights beside them do
    // the illuminating. (Higher values flooded the frame and drowned the actual
    // light pools - the old too-bright look.)
    m_matPlayer     = makeMaterial({0.05f,  0.45f,  0.62f},  0.5f,  0.42f, {0.00f, 0.18f, 0.28f}, 1.0f, false, "potion:player");
    m_matPlayerGlow = makeMaterial({0.40f,  0.95f,  1.00f},  0.0f,  0.40f, {0.20f, 0.85f, 1.00f}, 1.8f, true,  "potion:player_glow");
    m_matTrain      = makeMaterial({0.10f,  0.12f,  0.18f},  1.00f, 0.70f, {0.00f, 0.14f, 0.30f}, 0.5f, false, "potion:train");
    m_matTrainB     = makeMaterial({0.06f,  0.16f,  0.15f},  1.00f, 0.70f, {0.00f, 0.18f, 0.14f}, 0.4f, false, "potion:train_b");
    m_matTrainC     = makeMaterial({0.11f,  0.11f,  0.13f},  1.00f, 0.72f, {0.10f, 0.10f, 0.16f}, 0.35f, false, "potion:train_c");
    m_matWindow     = makeMaterial({0.70f,  0.90f,  1.00f},  0.0f,  0.30f, {0.55f, 0.85f, 1.00f}, 1.8f, true,  "potion:window");
    // The nose light bar glows the same warm tone the headlight spot casts, so
    // the beam visibly comes FROM somewhere.
    m_matHeadlamp   = makeMaterial({1.00f,  0.95f,  0.80f},  0.0f,  0.40f, {1.00f, 0.92f, 0.72f}, 1.8f, true,  "potion:headlamp");

    // One hazard family, one style: every obstacle body is the same matte
    // painted red with white reflective bands - boards and barricades that are
    // LIT BY their warning lamps, not glowing like lamps themselves (a trace of
    // emission keeps them from dying to pure black at distance, nothing more).
    m_matBarrier = makeMaterial({0.42f,  0.05f,  0.04f},  0.0f,  0.75f, {0.85f, 0.05f, 0.03f}, 0.25f, false, "potion:barrier");
    m_matStripe  = makeMaterial({0.82f,  0.84f,  0.87f},  0.0f,  0.60f, {0.60f, 0.63f, 0.70f}, 0.30f, false, "potion:stripe");

    // Trackside signal heads - tiny unlit lamps, red down the left wall, green
    // down the right, purely scenery.
    m_matSignalRed   = makeMaterial({1.00f, 0.12f, 0.08f}, 0.0f, 0.5f, {1.00f, 0.08f, 0.05f}, 1.1f, true, "potion:signal_red");
    m_matSignalGreen = makeMaterial({0.20f, 1.00f, 0.40f}, 0.0f, 0.5f, {0.10f, 0.90f, 0.30f}, 1.1f, true, "potion:signal_green");

    // Collectible: the one thing that gets to break the "no self-glow" rule -
    // mirror gold with a strong warm glow, so pickups sparkle and bloom like
    // arcade treasure against the dark track.
    m_matCoin    = makeMaterial({1.00f,  0.78f,  0.28f},  1.0f,  0.08f, {1.00f, 0.6f, 0.0f}, 2.0f, false, "potion:coin");

    // The neon fixtures: bright enough to read as sources, dim enough that the
    // real Lights they carry remain the visible contribution.
    m_matArch    = makeMaterial({0.85f,  0.92f,  1.00f},  0.0f,  0.40f, {0.45f, 0.70f, 1.00f}, 1.3f, true,  "potion:arch");
    m_matTrim    = makeMaterial({0.90f,  0.35f,  1.00f},  0.0f,  0.40f, {0.80f, 0.18f, 1.00f}, 1.3f, true,  "potion:trim");

    // Drive whichever camera the scene already provides (see potion_scene.h).
    m_camera = EntityId{};
    m_scene->forEach<Camera>([&](EntityId id, Camera&) {
        if (!m_camera) m_camera = id;
    });

    const float trackWidth = laneWidth * 3.0f + 4.0f;
    m_wallX = laneWidth * 1.5f + 0.5f;

    // Static scenery: a long ground slab and two side walls framing the track.
    {
        EntityId ground = spawnBox(m_cubeMesh, m_matGround, "Ground");
        Transform& t = m_scene->get<Transform>(ground);
        t.position = {0.0f, -0.2f, GROUND_CENTER_Z};   // top face sits at y = 0
        t.scale    = {trackWidth, 0.4f, GROUND_LEN};
        m_scene->get<Mesh>(ground).castShadows = false;

        // A static physics floor so the crash ragdoll has something to land on.
        // The solver ignores Transform scale, so the collider carries the real
        // world half-extents.
        Rigidbody rb;
        rb.isStatic = true;
        m_scene->add(ground, std::move(rb));
        Collider col;
        col.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, {trackWidth * 0.5f, 0.2f, GROUND_LEN * 0.5f}}};
        m_scene->add(ground, std::move(col));
    }
    for (int side = -1; side <= 1; side += 2) {
        EntityId wall = spawnBox(m_cubeMesh, m_matWall, "Wall");
        Transform& t = m_scene->get<Transform>(wall);
        t.position = {static_cast<float>(side) * (m_wallX + 0.3f), 3.2f, GROUND_CENTER_Z};
        t.scale    = {0.4f, 7.7f, GROUND_LEN};   // below grade up to the raised ceiling, no gap
        m_scene->get<Mesh>(wall).castShadows = false;

        // Two service pipes running the tunnel's length high on each wall - the
        // horizontal detail lines every real cut-and-cover tunnel carries.
        for (float pipeY : {2.05f, 4.35f}) {
            EntityId pipe = spawnBox(m_cubeMesh, m_matPillar, "Wall Pipe");
            Transform& pt = m_scene->get<Transform>(pipe);
            pt.position = {static_cast<float>(side) * (m_wallX + 0.04f), pipeY, GROUND_CENTER_Z};
            pt.scale    = {0.13f, 0.13f, GROUND_LEN};
            m_scene->get<Mesh>(pipe).castShadows = false;
        }
    }

    // A concrete ceiling sealing the tunnel: the arch ribs run up into it and
    // the tube reads as underground instead of an open-topped trench. Like all
    // static scenery it stays out of the shadow pass - only the moving pieces
    // (player, obstacles) are worth a caster's cost.
    {
        EntityId ceiling = spawnBox(m_cubeMesh, m_matWall, "Ceiling");
        Transform& t = m_scene->get<Transform>(ceiling);
        t.position = {0.0f, ARCH_Y + 0.42f, GROUND_CENTER_Z};
        t.scale    = {2.0f * m_wallX + 1.4f, 0.3f, GROUND_LEN};
        m_scene->get<Mesh>(ceiling).castShadows = false;
    }

    // A raised gravel ballast bed under each lane's track - the shoulder of
    // grey stone a real permanent way sits on. Static like the rails; the
    // sleepers and rails ride on top of it.
    for (int lane = 0; lane < 3; ++lane) {
        EntityId bed = spawnBox(m_cubeMesh, m_matBallast, "Ballast Bed");
        Transform& t = m_scene->get<Transform>(bed);
        t.position = {laneX(lane), 0.015f, GROUND_CENTER_Z};   // top face just proud of the ground
        t.scale    = {laneWidth * 0.88f, 0.07f, GROUND_LEN};
        m_scene->get<Mesh>(bed).castShadows = false;
    }

    // Train rails: a polished-steel pair per lane, running the full length of the
    // track. They are uniform along Z, so - unlike the sleepers below - they need
    // no scrolling and are spawned once as static boxes. This pair-per-lane layout
    // is what reads as "tracks for trains" rather than the old crosswalk stripes.
    constexpr float RAIL_GAUGE_HALF = 0.50f;   // half the spacing within a lane's rail pair
    constexpr float RAIL_W = 0.12f, RAIL_H = 0.14f;
    for (int lane = 0; lane < 3; ++lane) {
        for (int s = -1; s <= 1; s += 2) {
            EntityId rail = spawnBox(m_cubeMesh, m_matRail, "Rail");
            Transform& t = m_scene->get<Transform>(rail);
            t.position = {laneX(lane) + static_cast<float>(s) * RAIL_GAUGE_HALF,
                          0.04f + RAIL_H * 0.5f, GROUND_CENTER_Z};
            t.scale    = {RAIL_W, RAIL_H, GROUND_LEN};
            m_scene->get<Mesh>(rail).castShadows = false;
        }
    }

    // A glowing neon trim line down the inner face of each wall - the headline
    // 'cool' accent: a bright streak receding to the horizon. Set just inside the
    // pillars so the columns pass in front of it.
    for (int side = -1; side <= 1; side += 2) {
        EntityId trim = spawnBox(m_cubeMesh, m_matTrim, "Wall Trim");
        Transform& t = m_scene->get<Transform>(trim);
        t.position = {static_cast<float>(side) * (m_wallX - 0.25f), 3.1f, GROUND_CENTER_Z};
        t.scale    = {0.10f, 0.18f, GROUND_LEN};
        m_scene->get<Mesh>(trim).castShadows = false;
    }

    // Player: a little runner rig rather than a bare cube. The root is an
    // invisible entity the gameplay drives (its scale stays 1, so the parented
    // parts below keep their own, un-distorted scales). The root's origin sits at
    // the body centre (updatePlayer places it at PLAYER_HALF_Y + height), so every
    // part offset is relative to that centre. Parts cast shadows (the cube did),
    // so the runner throws a proper shadow on the track.
    m_player = m_scene->createEntity();
    m_scene->add(m_player, makeName("Player"));
    m_scene->add(m_player, Transform{});
    // The player is a DYNAMIC body for its whole life: the solver owns gravity,
    // jumping, landing, roof support and ramp climbing (contacts vs the
    // obstacle/ramp colliders below). freezeRotation keeps it upright - the
    // behavior still owns rotation (bank) and lateral position, and death just
    // unfreezes rotation to hand the same body over as the ragdoll.
    {
        Rigidbody rb;
        rb.mass           = 1.0f;
        rb.restitution    = 0.0f;    // land dead, no bounce
        rb.friction       = 0.2f;
        rb.freezeRotation = true;
        rb.canSleep       = false;   // a dozing character eats jump inputs and ignores ramps
        m_scene->add(m_player, std::move(rb));
        Collider col;
        col.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, {PLAYER_HALF_X, PLAYER_HALF_Y, PLAYER_HALF_Z}}};
        m_scene->add(m_player, std::move(col));
    }
    m_playerParts.clear();
    m_limbPivots.clear();
    auto addPart = [&](const char* name, MaterialHandle mat,
                       const glm::vec3& scale, const glm::vec3& offset, EntityId parent) {
        EntityId e = spawnBox(m_cubeMesh, mat, name);
        Transform& t = m_scene->get<Transform>(e);
        t.position = offset;
        t.scale    = scale;
        HierarchyOperations::setParent(*m_scene, e, parent);
        m_playerParts.emplace_back(e, mat);
    };
    addPart("Torso",   m_matPlayer,     {0.74f, 0.78f, 0.52f}, { 0.00f,  0.08f,  0.00f}, m_player);
    addPart("Head",    m_matPlayer,     {0.46f, 0.42f, 0.46f}, { 0.00f,  0.66f,  0.00f}, m_player);
    addPart("Visor",   m_matPlayerGlow, {0.50f, 0.12f, 0.50f}, { 0.00f,  0.74f,  0.00f}, m_player);
    addPart("Pack",    m_matPlayerGlow, {0.46f, 0.62f, 0.18f}, { 0.00f,  0.10f, -0.36f}, m_player);  // on the back, toward the camera
    // Limbs hang from meshless pivot entities at the shoulder/hip joints, so
    // the AnimationSystem's swing (see makeSwing) rotates them about the joint
    // instead of paddling them about their own centres. Opposing limbs (and the
    // opposite arm/leg of each side) start half a period out of phase, like a
    // real stride. updatePlayer drives Animation::speed with the run cadence.
    auto addLimb = [&](const char* name, const glm::vec3& scale, const glm::vec3& joint,
                       float amplitude, float phase) {
        EntityId pivot = m_scene->createEntity();
        m_scene->add(pivot, makeName(name));
        m_scene->add(pivot, Transform{joint, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)});
        HierarchyOperations::setParent(*m_scene, pivot, m_player);
        m_scene->add(pivot, makeSwing(amplitude, phase));
        m_limbPivots.push_back(pivot);
        addPart(name, m_matPlayer, scale, {0.0f, -scale.y * 0.5f + 0.04f, 0.0f}, pivot);
    };
    addLimb("Arm L", {0.15f, 0.56f, 0.28f}, {-0.46f,  0.30f, 0.00f}, 0.9f, 0.0f);
    addLimb("Arm R", {0.15f, 0.56f, 0.28f}, { 0.46f,  0.30f, 0.00f}, 0.9f, RUN_PERIOD * 0.5f);
    addLimb("Leg L", {0.20f, 0.46f, 0.30f}, {-0.18f, -0.28f, 0.00f}, 1.1f, RUN_PERIOD * 0.5f);
    addLimb("Leg R", {0.20f, 0.46f, 0.30f}, { 0.18f, -0.28f, 0.00f}, 1.1f, 0.0f);

    // Scrolling decoration pools (no gameplay, just a sense of speed). Pillars
    // stay shorter than the camera height so they never cross the view.
    auto makeScenery = [&](std::vector<Scenery>& pool, int count, MaterialHandle mat,
                           const glm::vec3& scale, const char* name) {
        pool.resize(count);
        for (auto& s : pool) {
            s.entity = spawnBox(m_cubeMesh, mat, name);
            m_scene->get<Transform>(s.entity).scale = scale;
            m_scene->get<Mesh>(s.entity).castShadows = false;
        }
    };
    // Each station has a left pillar, a right pillar, and a ceiling rib capping
    // them (shared z lattice) - a portal frame the player runs through. The pillars
    // rise to ARCH_Y so they visibly hold the rib; the rib spans pillar-to-pillar
    // (just past m_wallX on each side) instead of overhanging.
    const float archSpan = 2.0f * m_wallX + 0.4f;
    // Sleepers: dark wooden cross-ties under each lane's rail pair. One short tie
    // per lane (not one log across all three) so each lane reads as its own track.
    // They scroll (recycled by scrollScenery) so the steel rails sitting on them
    // read as rushing past - the sense of speed the old bright stripes carried.
    for (auto& lanePool : m_ties)
        makeScenery(lanePool, TIE_COUNT, m_matTie, {laneWidth * 0.62f, 0.10f, 0.30f}, "Tie");
    makeScenery(m_pillarsL, PILLAR_COUNT, m_matPillar, {0.40f, ARCH_Y, 0.40f},           "Pillar");
    makeScenery(m_pillarsR, PILLAR_COUNT, m_matPillar, {0.40f, ARCH_Y, 0.40f},           "Pillar");
    // Signal heads mounted on each pillar's inner face (same z lattice, so they
    // stay glued to their pillar as both pools scroll): red aspects down the
    // left wall, green down the right - the classic trackside blinkenlights.
    makeScenery(m_signalsL, PILLAR_COUNT, m_matSignalRed,   {0.12f, 0.26f, 0.12f}, "Signal");
    makeScenery(m_signalsR, PILLAR_COUNT, m_matSignalGreen, {0.12f, 0.26f, 0.12f}, "Signal");
    // Station platforms: slabs along the walls with a painted safety line on
    // top. Every scenery pool scrolls at the same speed and wraps by the same
    // WRAP, so relative z offsets are constant forever - spacing 40 (a multiple
    // of the 10-unit pillar lattice) with a half-bay phase parks each platform
    // permanently BETWEEN pillars, never intersecting one. Sides are staggered
    // so a platform slides past every ~20 units of track.
    makeScenery(m_platformsL, 4, m_matPillar, {0.90f, 1.00f, 7.0f}, "Platform");
    makeScenery(m_platformsR, 4, m_matPillar, {0.90f, 1.00f, 7.0f}, "Platform");
    makeScenery(m_platEdgesL, 4, m_matStripe, {0.90f, 0.05f, 7.0f}, "Platform Edge");
    makeScenery(m_platEdgesR, 4, m_matStripe, {0.90f, 0.05f, 7.0f}, "Platform Edge");
    // Overhead "roof": a concrete rib (structure) with a glowing light strip
    // recessed under it, so the ceiling reads as lit station girders instead of
    // flat neon slabs. The light shares the ribs' z lattice and scrolls with them.
    makeScenery(m_arches, PILLAR_COUNT, m_matPillar, {archSpan, 0.55f, 0.75f}, "Arch Beam");
    // Each strip carries a POINT light, not a Rect: this renderer stacks
    // inverse-square attenuation on top of the LTC form factor (which already
    // falls off geometrically), so a Rect at the 5-unit ceiling-to-floor throw
    // decays ~1/d^4 and never reaches the track. A point at ~150 delivers
    // ~150/27 = a real pool below each fixture; the strip mesh still LOOKS
    // like the tube doing the emitting.
    makeScenery(m_archLights, PILLAR_COUNT, m_matArch, {archSpan * 0.88f, 0.16f, 0.45f}, "Arch Light");
    for (auto& strip : m_archLights) {
        Light wash;
        wash.type       = LightType::Point;
        wash.color      = {0.45f, 0.70f, 1.00f};   // matches the strip's emissive
        wash.intensity  = 60.0f;   // scaled for the raised ceiling's longer throw
        // Radius vs the 10-unit arch spacing sets the pooling: it must stay
        // UNDER the spacing or adjacent pools merge into one flat, even wash -
        // the valleys between pools are what keep the tunnel reading dark.
        wash.radius     = 8.0f;
        // Off at spawn; the cube atlas has exactly two slots, so scrollWorld
        // flips castShadows on for just the fixtures nearest the player - the
        // runner and obstacles throw real moving shadows as they pass through
        // each pool, and the caster count never exceeds the slots.
        wash.castShadows = false;
        m_scene->add(strip.entity, std::move(wash));
    }

    // Obstacle + coin pools. Per-recycle scale/material set in randomizeObstacle().
    // Each obstacle also owns an "accent" box - a train windscreen or a hazard bar -
    // placed alongside it each frame (like the train-roof coins). It is a sibling,
    // not a child, so the obstacle box's per-recycle scale never distorts it.
    m_obstacles.resize(OBSTACLE_COUNT);
    for (auto& o : m_obstacles) {
        o.entity = spawnBox(m_cubeMesh, m_matTrain,  "Obstacle");
        o.accent = spawnBox(m_cubeMesh, m_matWindow, "Obstacle Accent");
        o.auxA   = spawnBox(m_cubeMesh, m_matPillar, "Obstacle Detail");   // gantry leg / barrier stripe
        o.auxB   = spawnBox(m_cubeMesh, m_matPillar, "Obstacle Detail");
        m_scene->get<Mesh>(o.accent).castShadows = false;
        // The train headlight: a warm spot yawed 180 deg so it shines down -Z
        // at the oncoming player. The only light an obstacle carries - it has a
        // visible source (the cab) to be coming from. Enabled per recycle,
        // trains only. Sunless, the 2D atlas has six spot slots and disabled
        // lights never reach the renderer's list - the 3-4 headlights typically
        // running all fit, so trains push real shadows ahead of themselves.
        Light beam;
        beam.type           = LightType::Spot;
        beam.color          = {1.00f, 0.92f, 0.72f};
        beam.intensity      = 70.0f;
        beam.radius         = 20.0f;
        beam.innerConeAngle = 0.28f;
        beam.outerConeAngle = 0.55f;
        beam.enabled        = false;
        beam.castShadows    = true;
        beam.shadowBias     = 0.0f;
        o.lamp = m_scene->createEntity();
        m_scene->add(o.lamp, makeName("Train Headlight"));
        m_scene->add(o.lamp, std::move(beam));
        m_scene->add(o.lamp, Transform{{0.0f, 1.05f, SPAWN_Z},
                                       glm::angleAxis(glm::pi<float>(), Math::WORLD_AXIS_Y),
                                       glm::vec3(1.0f)});
        // The hull is a kinematic collision body: the dynamic player lands on
        // its roof (solver contact), and any non-top contact is the crash
        // signal (see the CollisionEvent handler in onStart). Extents follow
        // each recycle in randomizeObstacle.
        {
            Rigidbody rb;
            rb.isKinematic = true;
            m_scene->add(o.entity, std::move(rb));
            Collider col;
            col.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
            m_scene->add(o.entity, std::move(col));
        }
        // The boarding ramp, hidden until a recycle makes this a steady train.
        // Pale like every "go here" cue - white invites, red kills. Its
        // kinematic collider is what physically walks the player up the slope;
        // thicker than the visual slab so a fast fall can't tunnel through it.
        o.ramp = spawnBox(m_cubeMesh, m_matStripe, "Boarding Ramp");
        m_scene->get<Mesh>(o.ramp).visible = false;
        {
            Rigidbody rb;
            rb.isKinematic = true;
            m_scene->add(o.ramp, std::move(rb));
            Collider col;
            col.parts = {ColliderBox{{0.0f, -0.30f, 0.0f}, {1.0f, 0.35f, 1.0f}}};
            m_scene->add(o.ramp, std::move(col));
        }
        // Train dressing, shown only while the recycle is a train: underframe
        // skirt, red tail-light band, steel nose plow, and the warm headlamp
        // bar the beam visibly shines from.
        o.skirt   = spawnBox(m_cubeMesh, m_matGround,    "Train Skirt");
        o.tail    = spawnBox(m_cubeMesh, m_matSignalRed, "Train Tail Light");
        o.plow    = spawnBox(m_cubeMesh, m_matPillar,    "Train Plow");
        o.lampBar = spawnBox(m_cubeMesh, m_matHeadlamp,  "Train Lamp Bar");
        m_scene->get<Mesh>(o.skirt).visible   = false;
        m_scene->get<Mesh>(o.tail).visible    = false;
        m_scene->get<Mesh>(o.plow).visible    = false;
        m_scene->get<Mesh>(o.lampBar).visible = false;
        // Stripes, bands and lamps are detail; the hull already throws the
        // silhouette, so keep them out of the shadow pass.
        m_scene->get<Mesh>(o.tail).castShadows    = false;
        m_scene->get<Mesh>(o.lampBar).castShadows = false;
        // Plow and lamp bar never change shape (train width and height are
        // constants), so scale - and the plow's shovel tilt - are set once
        // here; recycles only flip visibility, frames only move them.
        {
            Transform& plowT = m_scene->get<Transform>(o.plow);
            plowT.scale    = {obstacleHalfX() * 1.7f, 0.50f, 0.12f};
            plowT.rotation = glm::angleAxis(-0.7f, Math::WORLD_AXIS_X);
            m_scene->get<Transform>(o.lampBar).scale =
                {obstacleHalfX() * 2.0f * 0.55f, 0.16f, 0.08f};
        }
    }
    m_coins.resize(COIN_COUNT);
    for (int i = 0; i < COIN_COUNT; ++i) {
        Coin& c = m_coins[i];
        c.entity = spawnBox(m_cubeMesh, m_matCoin, "Coin");
        m_scene->get<Transform>(c.entity).scale = {0.7f, 0.7f, 0.12f};
        m_scene->get<Mesh>(c.entity).castShadows = false;
        // The AnimationSystem owns each coin's rotation + scale from here on
        // (scrollWorld only writes positions); staggered start times keep the
        // row from spinning in lockstep.
        m_scene->add(c.entity, makeCoinSpin(static_cast<float>(i) * 0.11f));
        // Pickup is a physics trigger: a generous volume (the old hand-check's
        // window) that overlaps the dynamic player and raises a TriggerEvent -
        // queried by the narrowphase, never resolved, so it can't push anyone.
        {
            Rigidbody rb;
            rb.isKinematic = true;
            m_scene->add(c.entity, std::move(rb));
            Collider col;
            col.isTrigger = true;
            col.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.2f, 0.5f}}};
            m_scene->add(c.entity, std::move(col));
        }
    }

    m_built = true;
    resetGame();

    POTION_LOG("Potion Runner ready - A/D switch lane, Space/W jump, S/Down slide, "
               "run up the white ramps to ride the trains, R restart.");
}

void PotionRunner::randomizeObstacle(Obstacle& o) {
    o.lane = randLane();
    float r = frand();
    // Convoy stretches - the Subway-Surfers elevated run. Obstacles recycle in
    // z order, so consecutive recycles are consecutive down the track: while a
    // convoy is owed, force this one to be another train in the convoy lane.
    // The gaps between cars (spacing 17.8 minus 12-15 of train) are one clean
    // roof-to-roof jump, and roof coins pay double up there.
    const bool convoyCar = m_convoyLeft > 0;
    if (convoyCar) {
        --m_convoyLeft;
        o.lane = m_convoyLane;
        r = 0.5f;                                  // land in the train branch below
    }
    const float halfX = obstacleHalfX();
    o.bottom     = 0.0f;        // default: box sits on the ground (overhead overrides below)
    o.auxVisible = false;       // default: no extra detail boxes (hurdle only)
    o.hasRamp    = false;       // only steady trains grow a boarding ramp below
    o.isTrain    = false;       // flips in the train branch; gates dressing + headlight

    // ---- solvability guard 1 ------------------------------------------------
    // Two recycles can share a z window: a faster train slowly closes on the
    // obstacle just ahead of it (the previous recycle). Side-by-side blockers
    // are survivable ONLY if the middle lane stays free - any lane can step to
    // lane 1, but reaching a far lane THROUGH a blocked middle is the
    // impossible path. A would-be barrier next to a closing train must keep
    // that invariant or it demotes to a gantry, which is passable in-lane and
    // therefore solvable no matter what overlaps it.
    const auto middleStaysFree = [](int a, int b) {
        return a == b || (a + b == 2 && a != 1);   // same lane, or the {0, 2} pair
    };
    if (r >= 0.78f && m_prevRel > 0.0f && !middleStaysFree(o.lane, m_prevLane)) {
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
        // A white band along the leading top edge (the line you clear).
        accentMat      = m_matStripe;
        o.accentScale  = {halfX * 2.0f * 1.04f, 0.14f, 0.14f};
        o.accentOffset = {0.0f, o.top * 0.5f - 0.06f, -o.length * 0.5f - 0.04f};
    } else if (r < 0.40f) {          // overhead gantry - crouch/slide under it
        o.top = 2.8f; o.bottom = 1.05f; o.length = 1.4f; o.rideable = false; o.relFactor = 0.0f;
        mat = m_matBarrier;             // same barricade red as every hazard body
        // A white band on the underside leading edge - the "duck here" line.
        accentMat      = m_matStripe;
        o.accentScale  = {halfX * 2.0f * 1.05f, 0.26f, 0.14f};
        o.accentOffset = {0.0f, o.bottom + 0.10f - (o.bottom + o.top) * 0.5f, -o.length * 0.5f - 0.05f};
        // Two concrete legs down to the ground so the bar reads as a portal, not
        // a floating slab. A leg spans 0..bottom (centre at -top/2 from the box
        // centre), set just inside each end of the bar.
        o.auxVisible = true;
        auxAMat      = m_matPillar;
        auxBMat      = m_matPillar;
        auxAScale    = {0.20f, o.bottom, 0.32f};
        auxBScale    = auxAScale;
        o.auxAOffset = {-(halfX - 0.10f), -o.top * 0.5f, 0.0f};
        o.auxBOffset = { (halfX - 0.10f), -o.top * 0.5f, 0.0f};
    } else if (r < 0.78f) {          // train - ride the roof or dodge the lane
        o.top = TRAIN_TOP; o.length = 10.0f + frand() * 5.0f; o.rideable = true;
        // Most ride along with the track (look parked as you rush past); some
        // bear down on you faster. None ever move slower than the track, which
        // would read as drifting backwards.
        o.relFactor = (frand() < 0.4f) ? 0.20f + frand() * 0.30f : 0.0f;
        if (convoyCar) {
            // Convoy cars run long and steady so the roof line stays hoppable.
            o.length    = 12.0f + frand() * 3.0f;
            o.relFactor = 0.0f;
        } else if (frand() < 0.35f) {
            // This train opens a convoy: the next 2-3 recycles chain behind it.
            m_convoyLeft = 2 + (frand() < 0.4f ? 1 : 0);
            m_convoyLane = o.lane;
        }
        // ---- solvability guard 2: a fast train closes on the previous
        // recycle; if that pairing would block two lanes without keeping the
        // middle free, it runs steady instead and the gap never closes.
        if (o.relFactor > 0.0f && m_prevBlocking && !middleStaysFree(o.lane, m_prevLane)) {
            o.relFactor = 0.0f;
        }
        // Steady trains carry a boarding ramp at the nose - run into it and it
        // walks you onto the roof, no jump. Convoy followers skip it (their
        // ramp would poke into the car ahead; you board the leader instead),
        // and the fast bearing-down trains stay ramp-less threats.
        o.hasRamp = (o.relFactor == 0.0f) && !convoyCar;
        if (o.hasRamp) {
            const float slopeLen = std::sqrt(o.top * o.top + RAMP_RUN * RAMP_RUN);
            Transform& rt = m_scene->get<Transform>(o.ramp);
            rt.scale    = {halfX * 2.0f * 0.9f, 0.14f, slopeLen};
            // Pitch the slab nose-down about X so its +Z end sits up at the
            // roof and its -Z end (toward the oncoming player) meets the rails.
            rt.rotation = glm::angleAxis(-std::atan2(o.top, RAMP_RUN), Math::WORLD_AXIS_X);
        }
        o.isTrain = true;
        // Three hull colours, cycled deterministically; a convoy reuses its
        // leader's so the whole chain reads as one long train.
        const int style = convoyCar ? m_convoyStyle : m_rng.nextInt(0, 2);
        m_convoyStyle = style;
        const MaterialHandle hulls[3] = {m_matTrain, m_matTrainB, m_matTrainC};
        mat = hulls[style];
        // Dressing scales (positions follow in scrollWorld): the underframe
        // skirt and the red tail-light band track this car's length.
        m_scene->get<Transform>(o.skirt).scale  = {halfX * 2.0f * 1.04f, 0.42f, o.length * 1.01f};
        m_scene->get<Transform>(o.tail).scale   = {halfX * 2.0f * 0.80f, 0.30f, 0.10f};
        // A lit windscreen band across the leading face - reads as a train cab.
        accentMat      = m_matWindow;
        o.accentScale  = {halfX * 2.0f * 0.82f, 0.46f, 0.10f};
        o.accentOffset = {0.0f, o.top * 0.18f, -o.length * 0.5f - 0.06f};
        // Dressing: a narrow roof walk line (the "run here" cue, not a deck),
        // and one window band WIDER than the hull so it surfaces as a lit
        // strip of passenger windows along BOTH flanks - one box, two sides,
        // sitting upper-half like real car windows.
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
        // A proper barricade board: three white reflective bands (this accent +
        // the two aux) across the matte red face, plus the pulsing red beacon
        // lamp - red/white stripes say "line closed" like nothing else does.
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
    Transform& t = m_scene->get<Transform>(o.entity);
    t.scale    = {halfX * 2.0f, o.top - o.bottom, o.length};   // box centre (Y) applied in scrollWorld
    t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_scene->get<Mesh>(o.entity).material = mat;

    // Colliders carry real half-extents (the solver ignores Transform scale);
    // refit the hull to this incarnation, and the ramp when it exists.
    m_scene->get<Collider>(o.entity).parts[0].halfExtents =
        {halfX, (o.top - o.bottom) * 0.5f, o.length * 0.5f};
    // A rampless incarnation simply disables its ramp collider - no broadphase
    // entry, no contacts, and the collider overlay skips it too.
    m_scene->get<Collider>(o.ramp).enabled = o.hasRamp;
    if (o.hasRamp) {
        // Extend the collider toe-ward past the visual slab and keep it thick:
        // the tilted TOE end-face then sits below grade, so an approaching
        // player's first contact is always the walkable top face - never the
        // end face, whose down-forward normal would shove them into the
        // ground instead of up the slope.
        const float slopeLen = std::sqrt(o.top * o.top + RAMP_RUN * RAMP_RUN);
        ColliderBox& rampBox = m_scene->get<Collider>(o.ramp).parts[0];
        rampBox.halfExtents  = {halfX * 0.9f, 0.35f, slopeLen * 0.5f + 0.35f};
        rampBox.center       = {0.0f, -0.30f, -0.35f};
    }

    // Only trains carry a light: the headlight has a visible source (the cab)
    // to shine from. A light floating in front of a barricade with nothing
    // emitting it reads wrong, so the other types run dark - their white bands
    // and the ceiling pools they pass through do the telegraphing.
    m_scene->get<Light>(o.lamp).enabled = o.isTrain;

    m_scene->get<Transform>(o.accent).scale = o.accentScale;
    m_scene->get<Mesh>(o.accent).material   = accentMat;

    m_scene->get<Mesh>(o.ramp).visible    = o.hasRamp;
    m_scene->get<Mesh>(o.skirt).visible   = o.isTrain;
    m_scene->get<Mesh>(o.tail).visible    = o.isTrain;
    m_scene->get<Mesh>(o.plow).visible    = o.isTrain;
    m_scene->get<Mesh>(o.lampBar).visible = o.isTrain;
    m_scene->get<Mesh>(o.auxA).visible = o.auxVisible;
    m_scene->get<Mesh>(o.auxB).visible = o.auxVisible;
    if (o.auxVisible) {
        m_scene->get<Transform>(o.auxA).scale = auxAScale;
        m_scene->get<Transform>(o.auxB).scale = auxBScale;
        m_scene->get<Mesh>(o.auxA).material   = auxAMat;
        m_scene->get<Mesh>(o.auxB).material   = auxBMat;
    }

    // Remember this recycle for the next one's solvability guards: it is the
    // obstacle just ahead in z of whatever spawns next.
    m_prevLane     = o.lane;
    m_prevRel      = o.isTrain ? o.relFactor : 0.0f;
    m_prevBlocking = o.isTrain || (!o.rideable && o.bottom <= 0.0f);   // hull or barrier
}

void PotionRunner::resetGame() {
    m_alive    = true;
    m_lane     = 1;
    m_playerX  = 0.0f;
    m_height   = 0.0f;
    m_grounded = true;
    m_groundedTimer = 0.2f;
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
    m_camX     = 0.0f;
    m_camY     = 4.0f;
    m_milestoneTimer = 0.0f;
    m_nextDistanceLog = 500.0f;
    m_rng.seed(RUN_SEED);

    // Take the runner back from ragdoll mode: same body, same collider - just
    // re-freeze rotation, kill the crash momentum, restore the upright collider
    // box, and stand the rig back at the start pose.
    {
        Rigidbody& rb = m_scene->get<Rigidbody>(m_player);
        rb.freezeRotation  = true;
        rb.linearVelocity  = {0.0f, 0.0f, 0.0f};
        rb.angularVelocity = {0.0f, 0.0f, 0.0f};
        rb.restitution     = 0.0f;
        rb.friction        = 0.2f;
        ColliderBox& box = m_scene->get<Collider>(m_player).parts[0];
        box.halfExtents.y = PLAYER_HALF_Y;
        box.center.y      = 0.0f;
    }
    for (auto& [part, mat] : m_playerParts) {        // undo the death-flash recolour
        m_scene->get<Mesh>(part).material = mat;
        m_scene->get<Mesh>(part).visible  = true;
    }
    Transform& pt = m_scene->get<Transform>(m_player);
    pt.position = {0.0f, PLAYER_HALF_Y, 0.0f};
    pt.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);   // undo the ragdoll tumble
    pt.scale    = {1.0f, 1.0f, 1.0f};                   // undo any crouch squash

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
        m_scene->get<Mesh>(m_coins[i].entity).visible = true;
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
    // Platforms phase in half a bay after each pillar (see the lattice note in
    // buildWorld); their spacing divides WRAP, so the phase survives wrapping
    // and they never intersect a pillar.
    for (int i = 0; i < static_cast<int>(m_platformsL.size()); ++i) {
        m_platformsL[i].z = DESPAWN_Z + 5.0f  + static_cast<float>(i) * 40.0f;
        m_platformsR[i].z = DESPAWN_Z + 25.0f + static_cast<float>(i) * 40.0f;
        m_platEdgesL[i].z = m_platformsL[i].z;
        m_platEdgesR[i].z = m_platformsR[i].z;
    }

    scrollWorld(0.0f);   // write every prop's transform into place

    POTION_LOG("Go! Ride the trains, slide under the gantries, grab the coins - dodge the red barriers.");
}

void PotionRunner::readInput() {
    const InputMap& input = *context().input;

    // Each action carries all of its keys, so the alternatives (A or Left,
    // Space or W or Up) live in the binding table rather than in an || chain
    // here, and the edges come from the map instead of four cached flags.
    m_edgeLeft    = input.pressed(ACTION_LEFT);
    m_edgeRight   = input.pressed(ACTION_RIGHT);
    m_edgeJump    = input.pressed(ACTION_JUMP);
    m_edgeRestart = input.pressed(ACTION_RESTART);

    // Crouch / slide is a hold, not an edge: stay low under an overhead gantry
    // for as long as it is held.
    m_crouchHeld = input.held(ACTION_CROUCH);
}

void PotionRunner::updatePlayer(float dt) {
    if (m_edgeLeft)  m_lane = std::max(0, m_lane - 1);
    if (m_edgeRight) m_lane = std::min(2, m_lane + 1);

    Transform& t  = m_scene->get<Transform>(m_player);
    Rigidbody& rb = m_scene->get<Rigidbody>(m_player);

    // The solver owns Y (gravity, landings, roof support, ramp climbing); the
    // behavior owns everything else. Feet height derives from the body centre.
    m_height = t.position.y - PLAYER_HALF_Y;

    // Grounded is contact-driven (see the CollisionEvent handler): the grace
    // timer bridges event-flush latency and grants a touch of coyote time.
    m_groundedTimer = std::max(0.0f, m_groundedTimer - dt);
    m_grounded = m_groundedTimer > 0.0f;

    // Ramp assist - the one interaction every character controller in the
    // industry special-cases in code rather than leaving to the solver. Our
    // kinematic ramps TELEPORT forward each frame and so advertise zero
    // velocity to the contact solver; its position correction alone cannot
    // out-climb a surface closing at track speed. While a ramp's span is under
    // the runner, the feet track the slope surface directly; the solver still
    // owns the handoff onto the roof (its contacts take over at the nose).
    for (const auto& o : m_obstacles) {
        if (!o.hasRamp) continue;
        if (std::fabs(laneX(o.lane) - m_playerX) > obstacleHalfX() + PLAYER_HALF_X) continue;
        const float nose = o.z - o.length * 0.5f;
        const float toe  = nose - RAMP_RUN;
        if (toe > 0.0f || nose < 0.0f) continue;
        const float rampH = o.top * (0.0f - toe) / RAMP_RUN;
        if (m_height > rampH + 0.4f)  continue;   // airborne above the slope: physics flies
        if (m_height < rampH - 1.2f)  continue;   // sidestepped into the tall side: no scoop
        t.position.y = rampH + PLAYER_HALF_Y;
        m_height     = rampH;
        rb.linearVelocity.y = std::max(rb.linearVelocity.y, 0.0f);   // gravity must not fight the climb
        m_grounded      = true;                   // the slope IS ground (jump off it freely)
        m_groundedTimer = GROUNDED_GRACE;
        break;
    }

    if (m_edgeJump && m_grounded) {
        rb.linearVelocity.y = jumpSpeed;
        m_grounded      = false;
        m_groundedTimer = 0.0f;
    }

    // The behavior pins the lateral axes every frame: lane position is eased
    // directly, and any x/z velocity the solver picked up from angled contacts
    // (a ramp's surface normal pushes up AND back) is cancelled so the runner
    // never drifts off z = 0 or out of its lane.
    const float k = 1.0f - std::exp(-dt * 14.0f);
    m_playerX += (laneX(m_lane) - m_playerX) * k;
    rb.linearVelocity.x = 0.0f;
    rb.linearVelocity.z = 0.0f;

    // Crouch only on the ground (a jump cancels it). The eased amount squashes
    // the rig AND refits the collider: the box top drops while the bottom stays
    // pinned at the feet, so sliding under a gantry is a real physics clearance.
    const float crouchTarget = (m_crouchHeld && m_grounded) ? 1.0f : 0.0f;
    m_crouch += (crouchTarget - m_crouch) * (1.0f - std::exp(-dt * 16.0f));
    const float halfY   = PLAYER_HALF_Y - m_crouch * (PLAYER_HALF_Y - CROUCH_HALF_Y);
    const float squashY = halfY / PLAYER_HALF_Y;
    ColliderBox& box = m_scene->get<Collider>(m_player).parts[0];
    box.halfExtents.y = halfY;
    box.center.y      = halfY - PLAYER_HALF_Y;   // keep the box bottom at the feet

    // Stride cadence follows the run: the limb swings quicken as the track
    // speeds up, and nearly freeze mid-pose while airborne.
    const float cadence = m_grounded ? (0.85f + 1.15f * (m_speed / maxSpeed)) : 0.30f;
    for (const EntityId& pivot : m_limbPivots) {
        m_scene->get<Animation>(pivot).speed = cadence;
    }

    // Bank into the lane change for a bit of life. Rotation is script-owned -
    // freezeRotation means the solver passes it through untouched.
    const float bank = std::clamp((m_playerX - laneX(m_lane)) * 0.18f, -0.35f, 0.35f);

    t.position.x = m_playerX;
    t.position.z = 0.0f;
    t.rotation   = glm::angleAxis(bank, Math::WORLD_AXIS_Z);
    t.scale      = {1.0f, squashY, 1.0f};
}

void PotionRunner::scrollScenery(std::vector<Scenery>& pool, float x, float y, float dt) {
    const float step = m_speed * dt;
    for (auto& s : pool) {
        s.z -= step;
        if (s.z < DESPAWN_Z) s.z += WRAP;
        m_scene->get<Transform>(s.entity).position = {x, y, s.z};
    }
}

void PotionRunner::scrollWorld(float dt) {
    for (auto& o : m_obstacles) {
        o.z -= m_speed * (1.0f + o.relFactor) * dt;   // some trains bear down faster
        if (o.z < DESPAWN_Z) {
            o.z += WRAP;
            randomizeObstacle(o);
        }
        const float x       = laneX(o.lane);
        const float centerY = (o.bottom + o.top) * 0.5f;   // box floats with a gap when bottom > 0
        m_scene->get<Transform>(o.entity).position = {x, centerY, o.z};
        // The accent (windscreen / hazard bar) rides alongside the box, offset
        // from its centre.
        m_scene->get<Transform>(o.accent).position =
            {x + o.accentOffset.x, centerY + o.accentOffset.y, o.z + o.accentOffset.z};
        // The headlight rides at the nose, just under the windscreen
        // (harmlessly stale while disabled, so no per-type branch here).
        m_scene->get<Transform>(o.lamp).position =
            {x, o.top * 0.58f, o.z - o.length * 0.5f - 0.12f};
        // The boarding ramp spans nose -> toe ahead of the hull. Rampless
        // incarnations keep it here too - mesh invisible, collider disabled -
        // so there is nothing to park anywhere.
        m_scene->get<Transform>(o.ramp).position =
            {x, o.top * 0.5f - 0.05f, o.z - o.length * 0.5f - RAMP_RUN * 0.5f};
        // Train dressing rides with the hull: skirt under it, tail lights on
        // the rear face, plow at the rails, and the lamp bar at the exact
        // height the headlight spot emits from - the glow and the beam read
        // as one fixture.
        if (o.isTrain) {
            const float nose = o.z - o.length * 0.5f;
            m_scene->get<Transform>(o.skirt).position   = {x, 0.21f, o.z};
            m_scene->get<Transform>(o.tail).position    =
                {x, 1.60f, o.z + o.length * 0.5f + 0.06f};
            m_scene->get<Transform>(o.plow).position    = {x, 0.30f, nose - 0.08f};
            m_scene->get<Transform>(o.lampBar).position =
                {x, o.top * 0.58f, nose - 0.05f};
        }
        // The extra detail boxes (gantry legs / barrier stripes), when this type uses them.
        if (o.auxVisible) {
            m_scene->get<Transform>(o.auxA).position =
                {x + o.auxAOffset.x, centerY + o.auxAOffset.y, o.z + o.auxAOffset.z};
            m_scene->get<Transform>(o.auxB).position =
                {x + o.auxBOffset.x, centerY + o.auxBOffset.y, o.z + o.auxBOffset.z};
        }
    }

    for (auto& c : m_coins) {
        c.z -= m_speed * dt;
        if (c.z < DESPAWN_Z) {
            c.z += WRAP;
            c.active = true;
            m_scene->get<Mesh>(c.entity).visible = true;
        }

        // Sit on a train roof when one passes under this coin's lane, so the
        // coin is only reachable while riding.
        float y = 1.0f;
        for (const auto& o : m_obstacles) {
            if (!o.rideable || o.top < 1.0f) continue;
            if (o.lane != c.lane) continue;
            if (std::fabs(o.z - c.z) > o.length * 0.5f) continue;
            y = o.top + 0.6f;
            break;
        }
        c.y = y;

        // Position only - the coin's Animation owns rotation and scale.
        m_scene->get<Transform>(c.entity).position = {laneX(c.lane), y, c.z};
    }

    for (int lane = 0; lane < 3; ++lane)
        scrollScenery(m_ties[lane], laneX(lane), 0.05f, dt);   // a sleeper run per lane, under its rails
    scrollScenery(m_pillarsL,   -m_wallX, ARCH_Y * 0.5f,  dt);   // span ground -> rib
    scrollScenery(m_pillarsR,    m_wallX, ARCH_Y * 0.5f,  dt);
    scrollScenery(m_signalsL, -(m_wallX - 0.28f), 1.42f,  dt);   // aspect heads on the pillar faces
    scrollScenery(m_signalsR,  (m_wallX - 0.28f), 1.42f,  dt);
    scrollScenery(m_arches,      0.0f,    ARCH_Y,         dt);   // concrete rib
    scrollScenery(m_archLights,  0.0f,    ARCH_Y - 0.33f, dt);   // glowing light recessed beneath it
    scrollScenery(m_platformsL, -(m_wallX - 0.48f), 0.50f, dt);  // station slabs against the walls
    scrollScenery(m_platformsR,  (m_wallX - 0.48f), 0.50f, dt);
    scrollScenery(m_platEdgesL, -(m_wallX - 0.48f), 1.03f, dt);  // painted safety line on top
    scrollScenery(m_platEdgesR,  (m_wallX - 0.48f), 1.03f, dt);

    // Hand the two cube-shadow slots to the ceiling lights nearest the action:
    // one just behind the player, one ahead. The atlas assigns slots first-come
    // in light order, so keeping the caster count at ~2 here is what guarantees
    // the shadows are these fixtures' and not two arbitrary ones down the track.
    // And every fourth fixture is a tired one: its pool breathes with a slow
    // two-sine flicker, which keeps a repeated corridor feeling alive.
    for (size_t i = 0; i < m_archLights.size(); ++i) {
        Light& wash = m_scene->get<Light>(m_archLights[i].entity);
        wash.castShadows = (m_archLights[i].z > -6.0f && m_archLights[i].z < 14.0f);
        if (i % 4 == 0) {
            wash.intensity = 45.0f * (0.78f + 0.22f * std::sin(m_camTime * 9.0f + m_archLights[i].z)
                                                    * std::sin(m_camTime * 23.0f));
        }
    }
}

void PotionRunner::updateCamera(float dt) {
    if (!m_scene->isAlive(m_camera) || !m_scene->has<Transform>(m_camera)) return;

    m_camTime += dt;
    const float k = 1.0f - std::exp(-dt * 8.0f);
    // On the start screen the camera drifts in a slow figure-eight over the
    // idle runner; once the run starts it eases back into the chase framing
    // through the same smoothing, no cut.
    const float targetX = m_started ? m_playerX * 0.5f
                                    : std::sin(m_camTime * 0.35f) * 1.4f;
    const float targetY = m_started ? 4.0f + m_height * 0.35f
                                    : 4.0f + std::sin(m_camTime * 0.23f) * 0.35f;
    m_camX += (targetX - m_camX) * k;
    m_camY += (targetY - m_camY) * k;

    // Speed reads through the lens too: ease the Camera's vertical FOV from a
    // calm 68 deg at start speed out to 81 deg at max, so top speed feels fast
    // even though the player never moves.
    if (m_scene->has<Camera>(m_camera)) {
        const float norm = maxSpeed > startSpeed
            ? std::clamp((m_speed - startSpeed) / (maxSpeed - startSpeed), 0.0f, 1.0f)
            : 0.0f;
        Camera& cam = m_scene->get<Camera>(m_camera);
        cam.fovY += (glm::radians(68.0f + 13.0f * norm) - cam.fovY) * k;
    }

    Transform& t = m_scene->get<Transform>(m_camera);
    t.position = {m_camX, m_camY, -8.5f};
    t.rotation = glm::angleAxis(0.34f, Math::WORLD_AXIS_X);  // look down the +Z track
}

void PotionRunner::die() {
    if (!m_alive) return;
    m_alive = false;
    m_speed = 0.0f;
    for (auto& [part, mat] : m_playerParts)                 // flash the whole runner red
        m_scene->get<Mesh>(part).material = m_matBarrier;

    // The crash is the same dynamic body with the leash off: unfreeze rotation
    // so it tumbles, restore the full collider box (in case death came mid
    // slide), give it a bounce, and launch it up and back toward the camera.
    // The behavior stops steering while dead (updatePlayer is skipped), so the
    // solver owns the pose until resetGame() re-freezes it.
    Rigidbody& rb = m_scene->get<Rigidbody>(m_player);
    rb.freezeRotation = false;
    rb.restitution    = 0.45f;
    rb.friction       = 0.5f;
    rb.linearVelocity  = {(frand() * 2.0f - 1.0f) * 3.5f, 8.0f + frand() * 3.0f, -3.0f - frand() * 2.5f};
    rb.angularVelocity = {(frand() * 2.0f - 1.0f) * 8.0f,
                          (frand() * 2.0f - 1.0f) * 8.0f,
                          (frand() * 2.0f - 1.0f) * 8.0f};
    ColliderBox& box = m_scene->get<Collider>(m_player).parts[0];
    box.halfExtents.y = PLAYER_HALF_Y;
    box.center.y      = 0.0f;

    const int score = static_cast<int>(m_distance) + m_coinCount * coinValue + m_bonusScore;
    m_newBest = score > m_best;
    if (m_newBest) m_best = score;
    POTION_LOG("Crash! Score %d  (distance %d, coins %d%s). Press R or Enter to run again.",
               score, static_cast<int>(m_distance), m_coinCount,
               m_newBest ? ", new best" : "");
}

void PotionRunner::buildUI() {
    // Create a UIElement entity parented under `parent`.
    auto makeElement = [&](const char* name, glm::vec2 anchor, glm::vec2 pivot,
                           glm::vec2 pos, glm::vec2 size, EntityId parent) {
        EntityId e = m_scene->createEntity();
        m_scene->add(e, makeName(name));
        UIElement el;
        el.anchor = anchor; el.pivot = pivot; el.position = pos; el.size = size;
        m_scene->add(e, std::move(el));
        HierarchyOperations::setParent(*m_scene, e, parent);
        return e;
    };
    auto makeText = [&](const char* name, std::string text, float px, glm::vec4 color,
                        UIText::Align align, glm::vec2 anchor, glm::vec2 pivot,
                        glm::vec2 pos, glm::vec2 size, EntityId parent) {
        EntityId e = makeElement(name, anchor, pivot, pos, size, parent);
        UIText t;
        t.text = std::move(text); t.pixelSize = px; t.color = color; t.align = align;
        m_scene->add(e, std::move(t));
        return e;
    };

    const glm::vec2 TL{0.0f, 0.0f};   // top-left anchor / pivot
    const glm::vec2 C {0.5f, 0.5f};   // centre anchor / pivot
    const glm::vec2 TC{0.5f, 0.0f};   // top-centre anchor / pivot
    const glm::vec4 WHITE{1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 GREY {0.62f, 0.68f, 0.78f, 1.0f};
    const glm::vec4 GOLD {1.0f, 0.84f, 0.30f, 1.0f};
    const glm::vec4 CYAN {0.35f, 0.90f, 1.00f, 1.0f};   // matches the ceiling/neon accents
    const glm::vec4 MAG  {0.95f, 0.32f, 1.00f, 1.0f};   // matches the wall trim
    const glm::vec4 REDH {1.00f, 0.36f, 0.30f, 1.0f};
    const glm::vec4 INK  {0.02f, 0.03f, 0.06f, 0.84f};  // HUD backing
    const glm::vec4 INK2 {0.04f, 0.05f, 0.10f, 0.95f};  // modal panels

    // A flat colour quad on its own element.
    auto makeImage = [&](const char* name, glm::vec4 color, glm::vec2 anchor, glm::vec2 pivot,
                         glm::vec2 pos, glm::vec2 size, EntityId parent) {
        EntityId e = makeElement(name, anchor, pivot, pos, size, parent);
        m_scene->add(e, UIImage{color});
        return e;
    };
    // A bright bar with a faint, larger copy behind it -> a cheap neon glow. The
    // glow is the earlier sibling, so it draws behind the core (UISystem emits in
    // child order, parents/earlier-siblings first).
    auto makeGlowBar = [&](glm::vec4 color, glm::vec2 anchor, glm::vec2 pivot,
                           glm::vec2 pos, glm::vec2 size, EntityId parent) {
        makeImage("Glow", glm::vec4(color.r, color.g, color.b, 0.22f), anchor, pivot,
                  pos - glm::vec2(6.0f, 4.0f), size + glm::vec2(12.0f, 8.0f), parent);
        makeImage("Bar", color, anchor, pivot, pos, size, parent);
    };
    // A neon button: a glow halo, the button itself, and a centred label.
    auto makeNeonButton = [&](const char* name, const char* label, const char* event,
                              glm::vec4 base, glm::vec4 hot, glm::vec2 pos, glm::vec2 size,
                              EntityId parent) {
        makeImage("Btn Glow", glm::vec4(hot.r, hot.g, hot.b, 0.30f), TC, TC,
                  pos - glm::vec2(0.0f, 7.0f), size + glm::vec2(16.0f, 14.0f), parent);
        EntityId b = makeElement(name, TC, TC, pos, size, parent);
        UIButton btn;
        btn.eventId      = event;
        btn.normalColor  = base;
        btn.hoverColor   = hot;
        btn.pressedColor = glm::vec4(base.r * 0.65f, base.g * 0.65f, base.b * 0.65f, 0.96f);
        m_scene->add(b, std::move(btn));
        // The label centres itself through anchor/pivot + Middle valign - no
        // hand-tuned pixel offsets.
        EntityId lbl = makeText("Btn Label", label, size.y * 0.45f, WHITE, UIText::Align::Center,
                                C, C, {0.0f, 0.0f}, {size.x, size.y}, b);
        m_scene->get<UIText>(lbl).valign = UIText::VAlign::Middle;
        return b;
    };

    // ---- HUD: score / distance / coins, top-left, always visible ----
    EntityId hud = m_scene->createEntity();
    m_scene->add(hud, makeName("Potion HUD"));
    m_scene->add(hud, UICanvas{});

    EntityId hudPanel = makeImage("HUD Panel", INK, TL, TL, {20.0f, 18.0f}, {326.0f, 158.0f}, hud);
    makeImage("HUD Edge Glow", glm::vec4(CYAN.r, CYAN.g, CYAN.b, 0.25f), TL, TL, {0.0f, 0.0f}, {10.0f, 158.0f}, hudPanel);
    makeImage("HUD Edge",      CYAN,                                      TL, TL, {0.0f, 0.0f}, {4.0f, 158.0f},  hudPanel);
    makeImage("HUD Top",       glm::vec4(CYAN.r, CYAN.g, CYAN.b, 0.55f),  TL, TL, {0.0f, 0.0f}, {326.0f, 3.0f},  hudPanel);

    m_uiScore = makeText("HUD Score", "SCORE  0", 30.0f, WHITE, UIText::Align::Left,
                         TL, TL, {22.0f, 10.0f}, {296.0f, 38.0f}, hudPanel);
    m_uiDist  = makeText("HUD Dist", "DIST  0 m", 22.0f, CYAN, UIText::Align::Left,
                         TL, TL, {22.0f, 54.0f}, {296.0f, 30.0f}, hudPanel);
    makeImage("HUD Coin Pip", GOLD, TL, TL, {23.0f, 93.0f}, {15.0f, 15.0f}, hudPanel);   // little coin
    m_uiCoins = makeText("HUD Coins", "0", 24.0f, GOLD, UIText::Align::Left,
                         TL, TL, {48.0f, 88.0f}, {270.0f, 30.0f}, hudPanel);
    m_uiSpeed = makeText("HUD Speed", "SPEED  0", 20.0f, GREY, UIText::Align::Left,
                         TL, TL, {22.0f, 124.0f}, {296.0f, 26.0f}, hudPanel);

    // A "ROOF RIDE" pill, top-centre, that exists permanently but only shows
    // while the runner is up on a roof - per-element visibility (which hides
    // the pill's whole subtree, label included), not canvas churn.
    EntityId ride = makeImage("Ride Tag", glm::vec4(GOLD.r, GOLD.g, GOLD.b, 0.18f),
                              TC, TC, {0.0f, 16.0f}, {200.0f, 38.0f}, hud);
    m_scene->get<UIElement>(ride).visible = false;
    m_uiRideTag = ride;
    EntityId rideText = makeText("Ride Tag Text", "ROOF RIDE  2x", 20.0f, GOLD, UIText::Align::Center,
                                 C, C, {0.0f, 0.0f}, {200.0f, 38.0f}, ride);
    m_scene->get<UIText>(rideText).valign = UIText::VAlign::Middle;

    // Distance milestone flash: a big centre-screen readout that onUpdate arms
    // every 500 m and refreshUI shows while its timer runs.
    m_uiMilestone = makeText("Milestone", "500 m", 46.0f, CYAN, UIText::Align::Center,
                             TC, TC, {0.0f, 78.0f}, {420.0f, 56.0f}, hud);
    m_scene->get<UIElement>(m_uiMilestone).visible = false;

    // ---- Start screen ----
    EntityId start = m_scene->createEntity();
    m_scene->add(start, makeName("Potion Start"));
    UICanvas startCanvas;
    startCanvas.sortOrder = 5;
    m_scene->add(start, std::move(startCanvas));
    m_startCanvas = start;

    EntityId startPanel = makeImage("Start Panel", INK2, C, C, {0.0f, 0.0f}, {680.0f, 380.0f}, start);
    makeGlowBar(CYAN, TC, TC, {0.0f,   0.0f}, {680.0f, 4.0f}, startPanel);   // top edge
    makeGlowBar(MAG,  TC, TC, {0.0f, 376.0f}, {680.0f, 4.0f}, startPanel);   // bottom edge
    makeText("Start Title", "POTION RUNNER", 60.0f, CYAN, UIText::Align::Center,
             TC, TC, {0.0f, 44.0f}, {660.0f, 70.0f}, startPanel);
    makeGlowBar(MAG, TC, TC, {0.0f, 120.0f}, {300.0f, 4.0f}, startPanel);    // title underline
    makeText("Start Subtitle", "ENDLESS RUNNER", 22.0f, GREY, UIText::Align::Center,
             TC, TC, {0.0f, 134.0f}, {660.0f, 28.0f}, startPanel);
    makeNeonButton("Start Button", "START", "potion:start",
                   glm::vec4(0.06f, 0.44f, 0.42f, 0.96f), glm::vec4(0.14f, 0.78f, 0.72f, 0.98f),
                   {0.0f, 196.0f}, {260.0f, 64.0f}, startPanel);
    makeText("Start Hint", "A / D  move      SPACE  jump      S  slide      R  restart", 17.0f,
             GREY, UIText::Align::Center, TC, TC, {0.0f, 296.0f}, {660.0f, 24.0f}, startPanel);
    makeText("Start Tip", "run up the white ramps to ride the trains  -  roof coins pay double", 15.0f,
             GOLD, UIText::Align::Center, TC, TC, {0.0f, 326.0f}, {660.0f, 22.0f}, startPanel);

    // ---- Game over ----
    EntityId over = m_scene->createEntity();
    m_scene->add(over, makeName("Potion Game Over"));
    UICanvas overCanvas;
    overCanvas.sortOrder = 10;
    overCanvas.visible   = false;
    m_scene->add(over, std::move(overCanvas));
    m_gameOverCanvas = over;

    EntityId panel = makeImage("Game Over Panel", INK2, C, C, {0.0f, 0.0f}, {600.0f, 400.0f}, over);
    makeGlowBar(REDH, TC, TC, {0.0f,   0.0f}, {600.0f, 4.0f}, panel);
    makeGlowBar(MAG,  TC, TC, {0.0f, 396.0f}, {600.0f, 4.0f}, panel);
    makeText("Game Over Title", "GAME OVER", 60.0f, REDH, UIText::Align::Center,
             TC, TC, {0.0f, 40.0f}, {560.0f, 70.0f}, panel);
    makeGlowBar(REDH, TC, TC, {0.0f, 116.0f}, {300.0f, 4.0f}, panel);
    m_uiFinalScore = makeText("Game Over Score", "SCORE  0", 34.0f, WHITE, UIText::Align::Center,
             TC, TC, {0.0f, 138.0f}, {560.0f, 40.0f}, panel);
    m_uiFinalDist  = makeText("Game Over Dist", "DIST  0 m", 24.0f, CYAN, UIText::Align::Center,
             TC, TC, {0.0f, 186.0f}, {560.0f, 32.0f}, panel);
    m_uiFinalCoins = makeText("Game Over Coins", "COINS  0", 24.0f, GOLD, UIText::Align::Center,
             TC, TC, {0.0f, 222.0f}, {560.0f, 32.0f}, panel);
    m_uiFinalBest  = makeText("Game Over Best", "BEST  0", 24.0f, GREY, UIText::Align::Center,
             TC, TC, {0.0f, 254.0f}, {560.0f, 30.0f}, panel);
    makeNeonButton("Restart Button", "RESTART", "potion:restart",
                   glm::vec4(0.08f, 0.40f, 0.62f, 0.96f), glm::vec4(0.16f, 0.60f, 0.86f, 0.98f),
                   {0.0f, 292.0f}, {240.0f, 56.0f}, panel);
    makeText("Restart Hint", "or press  R / Enter", 17.0f, GREY, UIText::Align::Center,
             TC, TC, {0.0f, 360.0f}, {560.0f, 24.0f}, panel);
}

void PotionRunner::refreshUI() {
    const int score = static_cast<int>(m_distance) + m_coinCount * coinValue + m_bonusScore;

    // Only rewrite a readout when its value actually changes.
    if (score != m_shownScore && m_scene->isAlive(m_uiScore)) {
        m_scene->get<UIText>(m_uiScore).text = "SCORE  " + std::to_string(score);
        m_shownScore = score;
    }
    if (static_cast<int>(m_distance) != m_shownDist && m_scene->isAlive(m_uiDist)) {
        m_scene->get<UIText>(m_uiDist).text = "DIST  " + std::to_string(static_cast<int>(m_distance)) + " m";
        m_shownDist = static_cast<int>(m_distance);
    }
    if (m_coinCount != m_shownCoins && m_scene->isAlive(m_uiCoins)) {
        m_scene->get<UIText>(m_uiCoins).text = std::to_string(m_coinCount);   // gold pip labels it
        m_shownCoins = m_coinCount;
    }
    const int speed = static_cast<int>(m_speed);
    if (speed != m_shownSpeed && m_scene->isAlive(m_uiSpeed)) {
        m_scene->get<UIText>(m_uiSpeed).text = "SPEED  " + std::to_string(speed);
        m_shownSpeed = speed;
    }

    // The roof-ride pill: per-element visibility, flipped straight off the
    // gameplay state. The height gate asks for real ROOF height - standing on
    // a hurdle (0.7) is not a 2x ride, and neither is the top of a jump.
    if (m_uiRideTag && m_scene->isAlive(m_uiRideTag)) {
        m_scene->get<UIElement>(m_uiRideTag).visible =
            m_started && m_alive && m_grounded && m_height > 1.5f;
    }
    if (m_scene->isAlive(m_uiMilestone)) {
        m_scene->get<UIElement>(m_uiMilestone).visible = m_alive && m_milestoneTimer > 0.0f;
    }

    // Toggle the start screen (until the run begins) and the game-over overlay
    // (while dead, once started).
    if (m_startCanvas && m_scene->isAlive(m_startCanvas)) {
        m_scene->get<UICanvas>(m_startCanvas).visible = !m_started;
    }
    if (m_gameOverCanvas && m_scene->isAlive(m_gameOverCanvas)) {
        m_scene->get<UICanvas>(m_gameOverCanvas).visible = m_started && !m_alive;
    }
    if (!m_alive) {
        if (m_scene->isAlive(m_uiFinalScore))
            m_scene->get<UIText>(m_uiFinalScore).text = "SCORE  " + std::to_string(score);
        if (m_scene->isAlive(m_uiFinalDist))
            m_scene->get<UIText>(m_uiFinalDist).text = "DIST  " + std::to_string(static_cast<int>(m_distance)) + " m";
        if (m_scene->isAlive(m_uiFinalCoins))
            m_scene->get<UIText>(m_uiFinalCoins).text = "COINS  " + std::to_string(m_coinCount);
        if (m_scene->isAlive(m_uiFinalBest)) {
            UIText& best = m_scene->get<UIText>(m_uiFinalBest);
            best.text  = (m_newBest ? "NEW BEST  " : "BEST  ") + std::to_string(m_best);
            best.color = m_newBest ? glm::vec4(1.0f, 0.84f, 0.30f, 1.0f)     // gold moment
                                   : glm::vec4(0.62f, 0.68f, 0.78f, 1.0f);   // quiet grey
        }
    }
}

} // namespace Engine

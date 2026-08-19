#pragma once

#include <array>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/math/random.h"
#include "ecs/entity.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/script/reflected_behavior.h"

namespace Vkm::Engine {

/**
 * @brief A Subway-Surfers-style endless runner, built as a single self-contained
 *        Behavior.
 *
 * Attach one instance to an otherwise empty "game" entity (see
 * example/potion_scene.h). On the first play tick it builds the whole visual
 * world procedurally from one in-code cube mesh - the ground, glowing side
 * pillars and high overhead arches, the player, pooled obstacles and coins -
 * then drives gameplay every frame.
 *
 * Obstacles come in three flavours: hurdles you jump over, barriers you must
 * dodge by lane, and trains - tall metro cars (three liveries, lit windows,
 * tail lights) you board by running up the white ramp on a steady train's
 * nose; the jump is deliberately too short to reach a roof from the ground.
 * Coins sitting on a train roof are only reachable while riding and pay
 * double. Every so often a train starts a CONVOY: the next few obstacle
 * recycles become trains in the same lane with hoppable gaps, so the best
 * line is up on the roofs, leaping car to car - the Subway-Surfers elevated
 * stretch.
 *
 * Beyond behaviors + UI, the game leans on the rest of the engine:
 *  - AnimationSystem: looping eased tracks drive the runner's limb swing (whose
 *    cadence follows the run speed) and every coin's spin/pulse.
 *  - Lighting: point pools under the ceiling luminaires and spot headlights on
 *    trains (toggled per recycle via Light::enabled). Every light has a visible
 *    fixture emitting it. Sunless, the headlight spots take the 2D shadow
 *    atlas; scrollWorld hands the two cube slots to the ceiling lights nearest
 *    the player.
 *  - Physics: the player is a DYNAMIC body (freezeRotation keeps it upright).
 *    The solver owns gravity, jumps, landings and roof support against
 *    kinematic hull colliders; crashes are CollisionEvents with a non-top
 *    contact normal, coin pickup is a TriggerEvent, and death just unfreezes
 *    rotation to turn the same body into the crash ragdoll. The behavior owns
 *    the lateral axes (lane, bank, crouch refit) plus the ramp slope assist -
 *    the one interaction every character controller special-cases in code.
 *  - Camera component: fovY widens with speed; the crash needs no camera
 *    gimmicks - the ragdoll's launch, tumble and bounces are all solver output.
 *
 * One behavior owns all gameplay state, so there is no cross-behavior wiring,
 * and the persisted scene stays trivial (a camera and this entity) -
 * every mesh and material is runtime-generated and never serialized, keeping
 * the editor's play snapshot/restore cheap.
 *
 * Controls: A/D or Left/Right switch lane, Space/W/Up jump, S/Down/Ctrl slide,
 * R or Enter restart.
 */
class PotionRunner : public ReflectedBehavior<PotionRunner> {
    public:
        static constexpr const char* TYPE_NAME = "PotionRunner";

        void onStart() override;
        void onUpdate(float dt) override;

        float laneWidth    = 2.4f;   ///< Lateral spacing between the three lanes, in world units.
        float startSpeed   = 16.0f;  ///< Initial forward (world-scroll) speed, in units per second.
        float maxSpeed     = 44.0f;  ///< Upper bound the ramping speed is clamped to.
        float acceleration = 0.7f;   ///< Speed ramp, in units per second squared.
        /** @brief Vertical launch velocity applied on jump. The arc apex
         *  (v^2/2g ~ 1.74) clears hurdles and roof-to-roof convoy hops, but
         *  deliberately NOT a train's boarding line - from the ground you
         *  board via the nose ramps, never by jumping. */
        float jumpSpeed    = 9.5f;
        /** @brief Downward acceleration (m/s^2), pushed into the physics
         *  Environment so the run and the ragdoll share it. Deliberately ~2.7x
         *  earth: we tried 9.81 and the hang time read as floaty - the classic
         *  reason arcade runners tune gravity up. Keep jumpSpeed^2 / (2 * g)
         *  ~ 1.74 if you retune either. */
        float gravity      = 26.0f;
        int   coinValue    = 5;  ///< Score awarded per collected coin.

    private:
        /**
         * @brief One pooled obstacle: a hurdle, a rideable train, or a barrier.
         */
        struct Obstacle {
            EntityId entity;
            EntityId accent;           ///< Per-type glow detail (train windscreen / hazard bar), placed each frame.
            // The train headlight spot (the only obstacle light - it has a
            // visible source to shine from). Enabled per recycle, trains only;
            // scrollWorld parks it at the nose each frame.
            EntityId  lamp;
            // The Subway-Surfers boarding ramp: a pale slope at the leading
            // face of steady trains. Run into it and the slope assist in
            // updatePlayer walks you onto the roof - no jump needed.
            EntityId  ramp;
            bool      hasRamp = false;
            // Train dressing (hidden for other types): dark underframe skirt,
            // red tail-light band, a steel nose plow, and the warm headlamp
            // bar the beam shines from.
            EntityId  skirt;
            EntityId  tail;
            EntityId  plow;
            EntityId  lampBar;
            bool      isTrain = false;
            // Two extra detail boxes, repurposed per type: a train's roof walk
            // strip + side window band, the gantry's support legs, or a
            // barrier's upper/lower stripes. Hidden for hurdles.
            EntityId auxA;
            EntityId auxB;
            float    z         = 0.0f;
            int      lane      = 1;
            float    top       = 1.4f; ///< Top of the box (its walkable roof, for rideables).
            float    bottom    = 0.0f; ///< Underside of the box; >0 leaves a gap to crouch through (overhead gantry).
            float    length    = 1.0f; ///< Z extent.
            float    relFactor = 0.0f; ///< 0 scrolls with the track; >0 approaches slower.
            bool     rideable  = true; ///< Can the player stand on the roof?
            // Detail placement, set per recycle in randomizeObstacle, applied in
            // scrollWorld. Offsets are from the obstacle box centre (x, y, z).
            glm::vec3 accentScale{0.0f};
            glm::vec3 accentOffset{0.0f};
            bool      auxVisible = false;
            glm::vec3 auxAOffset{0.0f};
            glm::vec3 auxBOffset{0.0f};
        };

        /**
         * @brief One pooled, collectible coin (its spin/pulse is an Animation).
         */
        struct Coin {
            EntityId entity;
            float    z      = 0.0f;
            int      lane   = 1;
            float    y      = 1.0f; ///< Float height; lifted onto a train roof when one is under it.
            bool     active = true; ///< False once collected, until it recycles.
        };

        /**
         * @brief One pooled scrolling decoration (sleeper tie / pillar / arch).
         */
        struct Scenery {
            EntityId entity;
            float    z = 0.0f;
        };

        // Setup
        void buildWorld();
        void buildUI();
        EntityId spawnBox(MeshHandle mesh, MaterialHandle material, const char* name);
        MaterialHandle makeMaterial(
            const glm::vec3& albedo, float metallic, float roughness,
            const glm::vec3& emission, float emissiveStrength, bool unlit, const char* name);

        // Per-frame steps
        void readInput();
        void updatePlayer(float dt);
        void scrollWorld(float dt);
        void scrollScenery(std::vector<Scenery>& pool, float x, float y, float dt);
        void updateCamera(float dt);

        // State transitions
        void resetGame();
        void randomizeObstacle(Obstacle& o);
        void die();

        // Refresh the HUD readouts and toggle the game-over overlay each frame.
        void refreshUI();

        // Helpers
        float laneX(int lane) const {
            // The chase camera looks down +Z, whose right-handed screen-right axis
            // is world -X - so lane 0 (left) maps to +X and lane 2 (right) to -X,
            // making A/D move the player the way the screen shows it.
            return static_cast<float>(1 - lane) * laneWidth;
        }
        float obstacleHalfX() const { return laneWidth * 0.42f; }
        /**
         * @brief Deterministic PCG32 draw in [0, 1) (engine Math::Rng).
         */
        float frand() { return m_rng.nextFloat(); }
        /**
         * @brief Unbiased uniform lane pick.
         */
        int   randLane() { return m_rng.nextInt(0, 2); }

        // Cached engine handles pulled from the BehaviorContext once in onStart.
        // The Behavior base no longer exposes the scene / resources / window
        // directly - context() is the single access path - so the game caches the
        // three it touches every frame rather than repeating context().* at every
        // call site. The context is session-stable, so these stay valid for life.
        Scene*           m_scene     = nullptr;
        ResourceManager* m_resources = nullptr;
        WindowManager*   m_window    = nullptr;

        // Procedurally generated assets (created in buildWorld).
        MeshHandle     m_cubeMesh;
        MaterialHandle m_matPlayer;
        MaterialHandle m_matPlayerGlow;   ///< Emissive accent on the runner (visor band, pack).
        MaterialHandle m_matTrain;         ///< Hull livery A: navy metal.
        MaterialHandle m_matTrainB;        ///< Hull livery B: teal metal.
        MaterialHandle m_matTrainC;        ///< Hull livery C: graphite metal.
        MaterialHandle m_matWindow;        ///< Train windscreen / window glow.
        MaterialHandle m_matHeadlamp;      ///< Warm nose light bar - the visible source of the beam.
        MaterialHandle m_matBarrier;       ///< The one hazard body material: matte barricade red.
        MaterialHandle m_matStripe;        ///< White reflective band on every hazard.
        MaterialHandle m_matSignalRed;     ///< Trackside signal lamp heads (left wall).
        MaterialHandle m_matSignalGreen;   ///< Trackside signal lamp heads (right wall).
        MaterialHandle m_matCoin;
        MaterialHandle m_matGround;
        MaterialHandle m_matBallast;       ///< Raised gravel bed under each lane's track.
        MaterialHandle m_matRail;
        MaterialHandle m_matTie;
        MaterialHandle m_matWall;
        MaterialHandle m_matTrim;
        MaterialHandle m_matPillar;
        MaterialHandle m_matArch;

        // World entities.
        EntityId              m_player{};  ///< Invisible rig root the gameplay drives; visible parts parent under it.
        std::vector<std::pair<EntityId, MaterialHandle>> m_playerParts;  ///< Part entity + its normal material (restored on reset).
        std::vector<EntityId> m_limbPivots;  ///< Shoulder/hip joints whose Animation swings the limbs; speed follows cadence.
        EntityId              m_camera{};
        std::vector<Obstacle> m_obstacles;
        std::vector<Coin>     m_coins;
        std::array<std::vector<Scenery>, 3> m_ties;   ///< One scrolling sleeper run per lane.
        std::vector<Scenery>  m_pillarsL;
        std::vector<Scenery>  m_pillarsR;
        std::vector<Scenery>  m_signalsL;     ///< Red signal heads mounted on the left pillars.
        std::vector<Scenery>  m_signalsR;     ///< Green signal heads mounted on the right pillars.
        std::vector<Scenery>  m_platformsL;   ///< Station platform slabs, phased into the pillar bays.
        std::vector<Scenery>  m_platformsR;
        std::vector<Scenery>  m_platEdgesL;   ///< Painted safety line along each platform top.
        std::vector<Scenery>  m_platEdgesR;
        std::vector<Scenery>  m_arches;       ///< Concrete ceiling ribs (the structural beam).
        std::vector<Scenery>  m_archLights;   ///< Glowing light strip recessed under each rib.
        float                 m_wallX = 4.0f;

        // In-game UI (screen-space overlay, built once in buildUI, driven by
        // refreshUI). HUD is always shown; the game-over overlay is a separate
        // canvas toggled by m_alive. Text entities are cached so refreshUI only
        // rewrites them when their value changes.
        EntityId m_startCanvas{};    ///< Title / "press to start" overlay; shown until the first run begins.
        EntityId m_uiScore{};        ///< HUD score readout.
        EntityId m_uiDist{};         ///< HUD distance readout.
        EntityId m_uiCoins{};        ///< HUD coin readout.
        EntityId m_uiSpeed{};        ///< HUD speed readout.
        EntityId m_uiRideTag{};      ///< "ROOF RIDE" pill; UIElement::visible only while on a roof.
        EntityId m_uiMilestone{};    ///< Centre-screen distance flash; visible while m_milestoneTimer > 0.
        EntityId m_gameOverCanvas{}; ///< Toggled visible on death.
        EntityId m_uiFinalScore{};   ///< Game-over screen final score.
        EntityId m_uiFinalDist{};    ///< Game-over screen distance.
        EntityId m_uiFinalCoins{};   ///< Game-over screen coin total.
        EntityId m_uiFinalBest{};    ///< Game-over screen session-best line.
        int      m_shownScore = -1;  ///< Last score pushed to the HUD.
        int      m_shownDist  = -1;  ///< Last distance pushed to the HUD.
        int      m_shownCoins = -1;  ///< Last coin count pushed to the HUD.
        int      m_shownSpeed = -1;  ///< Last speed pushed to the HUD.

        // Runtime gameplay state.
        bool  m_built    = false;
        bool  m_started  = false;  ///< False on the start screen; the run advances only once true.
        bool  m_alive    = true;
        int   m_lane     = 1;
        float m_playerX  = 0.0f;   ///< Smoothed lane position the player mesh tracks.
        float m_height   = 0.0f;   ///< Feet height above ground, derived from the solver-owned Transform.
        // Grounded state is contact-driven: any up-facing CollisionEvent on the
        // player arms this grace timer (which doubles as coyote time). The
        // solver owns vertical motion; the behavior only sets jump velocity.
        bool  m_grounded = true;
        float m_groundedTimer = 0.2f;
        bool  m_crouchHeld = false;   ///< Crouch/slide key currently held (set in readInput).
        float m_crouch     = 0.0f;    ///< Smoothed crouch amount 0..1; squashes the rig and lowers the head.
        float m_speed    = 0.0f;
        float m_distance = 0.0f;
        int   m_convoyLeft = 0;    ///< Obstacle recycles still owed to the convoy lane as trains.
        int   m_convoyLane = 1;    ///< Which lane the current convoy runs in.
        int   m_convoyStyle = 0;   ///< Livery of the convoy leader; followers reuse it.
        // The previously recycled obstacle - the one just ahead in z. The
        // solvability guards in randomizeObstacle compare against it so no two
        // z-overlapping blockers can ever wall off the reachable lanes.
        int   m_prevLane     = 1;
        float m_prevRel      = 0.0f;   ///< Its relFactor (>0 means it closes on the slot ahead).
        bool  m_prevBlocking = false;  ///< Full blocker (train hull or barrier)?
        int   m_coinCount = 0;
        int   m_bonusScore = 0;    ///< Extra score from roof coins (they pay double).
        int   m_best     = 0;      ///< Best score across runs this session.
        bool  m_newBest  = false;  ///< The run that just ended set a new best.
        float m_camX     = 0.0f;   ///< Smoothed camera lateral follow.
        float m_camY     = 4.0f;   ///< Smoothed camera height (rises while riding).
        float m_camTime  = 0.0f;   ///< Accumulated camera time; phases the idle sway and light flicker.
        float m_milestoneTimer = 0.0f;  ///< Seconds left on the distance-milestone flash.
        float m_nextDistanceLog = 0.0f;

        // This frame's edges, read off the input map in readInput.
        bool m_edgeLeft    = false;
        bool m_edgeRight   = false;
        bool m_edgeJump    = false;
        bool m_edgeRestart = false;

        /// Engine PCG32 (core/math/random.h), reseeded with RUN_SEED every run
        /// so the track deals the same layout each time. Never cloned: clone()
        /// copies only reflected fields, and a fresh run reseeds anyway.
        Math::Rng m_rng;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::PotionRunner)
    VKM_F(laneWidth),
    VKM_F(startSpeed),
    VKM_F(maxSpeed),
    VKM_F(acceleration),
    VKM_F(jumpSpeed),
    VKM_F(gravity),
    VKM_F(coinValue)
VKM_REFLECT_END()

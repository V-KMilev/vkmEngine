#pragma once

#include <array>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/math/random.h"
#include "ecs/entity.h"
#include "ecs/component/core/transform.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/script/reflected_behavior.h"

namespace Potion {

using namespace Vkm::Engine;

/**
 * @brief A Subway-Surfers-style endless runner, built as one self-contained Behavior.
 *
 * Attach it to an empty entity (see src/module.cpp); the first play tick builds
 * the whole world from one cube mesh, never serialized. The run is decided on the
 * fixed tick; a frame only draws, carrying the last tick forward, so the run
 * plays the same at any frame rate.
 *
 * Hurdles are jumped, barriers dodged by lane, and trains boarded up a nose ramp
 * (the jump cannot reach a roof). Roof coins pay double, and convoys of trains
 * in one lane make the roofs the best line. The solver owns the DYNAMIC player
 * body vertically; the behavior owns the lateral axes and the ramp assist.
 */
class PotionRunner : public ReflectedBehavior<PotionRunner> {
    public:
        /**
         * @brief The look the run is authored under.
         *
         * Public so the module's scene can show it before Play; the run reapplies it.
         */
        static constexpr float SKY_INTENSITY  = 0.08f;
        static constexpr float CAMERA_REST_Y  = 4.0f;   ///< Eye height at rest; rises while riding.
        static constexpr float CAMERA_BACK_Z  = -8.5f;  ///< How far behind the player the eye sits.
        static constexpr float CAMERA_PITCH   = 0.34f;  ///< Downward tilt, radians.

    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;

    public:
        float laneWidth    = 2.4f;   ///< Lateral spacing between the three lanes, in world units.
        float startSpeed   = 16.0f;  ///< Initial forward (world-scroll) speed, in units per second.
        float maxSpeed     = 44.0f;  ///< Upper bound the ramping speed is clamped to.
        float acceleration = 0.7f;   ///< Speed ramp, in units per second squared.
        /**
         * @brief Vertical launch velocity applied on jump.
         *
         * The apex (v^2/2g ~ 1.74) clears hurdles and convoy hops, but not a
         * train's roof from the ground.
         */
        float jumpSpeed    = 9.5f;
        /**
         * @brief Downward acceleration (m/s^2), shared with the ragdoll via PhysicsSettings.
         *
         * Keep jumpSpeed^2 / (2 * g) ~ 1.74 if you retune either.
         */
        float gravity      = 26.0f;
        int   coinValue    = 5;  ///< Score awarded per collected coin.

    private:
        /**
         * @brief One pooled obstacle: a hurdle, a rideable train, or a barrier.
         */
        struct Obstacle {
            EntityId entity;
            /// Per-type glow detail (train windscreen / hazard bar).
            EntityId accent;
            // The train headlight spot, trains only.
            EntityId  lamp;
            // Boarding ramp on steady trains' leading face; updatePlayer's assist climbs it.
            EntityId  ramp;
            bool      hasRamp = false;
            // Train dressing, hidden for other types: skirt, tail light, plow, headlamp bar.
            EntityId  skirt;
            EntityId  tail;
            EntityId  plow;
            EntityId  lampBar;
            bool      isTrain = false;
            // Two detail boxes repurposed per type; hidden for hurdles.
            EntityId auxA;
            EntityId auxB;
            float    z         = 0.0f;
            int      lane      = 1;
            float    top       = 1.4f; ///< Top of the box (its walkable roof, for rideables).
            /// Underside; >0 leaves a gap to crouch through (overhead gantry).
            float    bottom    = 0.0f;
            float    length    = 1.0f; ///< Z extent.
            float    relFactor = 0.0f; ///< 0 scrolls with the track; >0 approaches faster.
            bool     rideable  = true; ///< Can the player stand on the roof?
            // Detail placement, set per recycle; offsets from the box centre.
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
            float    z = 0.0f;   ///< Where it stands when a run begins; placeScenery scrolls it from there.
        };

    private:
        /**
         * @brief Build the whole world procedurally, once, on the first play tick.
         *
         * Lit only by ceiling pools and train headlights: ambient is near zero and
         * any directional light is switched off.
         */
        void buildWorld();
        void buildUI();
        /**
         * @brief Ask for one playback of @p clip at a world position.
         *
         * A PlaySoundEvent, not an AudioSource: footfalls overlap, and a coin's
         * entity recycles the instant it is collected. Volume and pitch vary a little.
         *
         * @param clip Clip to play; a dead handle is ignored.
         * @param position Where it is heard from, fixed at the call.
         * @param volume Base gain, before the spread.
         */
        void playAt(AudioClipHandle clip, const glm::vec3& position, float volume);

        EntityId spawnBox(MeshHandle mesh, MaterialHandle material, const char* name);
        /**
         * @brief File @p material under @p name.
         *
         * @param material Material to store.
         * @param name Its serializable identity.
         * @return Handle to the stored material.
         */
        MaterialHandle makeMaterial(MaterialAsset material, const char* name);

        /**
         * @brief Create every material the track is built from.
         *
         * Their creation order does not matter.
         */
        void buildMaterials();

        /// Scene-wide lighting and physics settings.
        void buildEnvironment();

        /// The static tube: ground, walls, ceiling, ballast, rails, neon trim.
        void buildTunnel();

        /// The runner: the body gameplay drives, its rig and its glow.
        void buildPlayer();

        /// Everything that scrolls - decoration, obstacle and coin pools, the train.
        void buildScenery();

        /// Read this tick's edges and held keys off its command.
        void readInput();

        /**
         * @brief Advance the run one tick: lanes, standing, the ramp assist, the
         *        jump, the crouch, and the runner's tick pose.
         *
         * @param dt The fixed step, seconds.
         */
        void updatePlayer(float dt);

        /**
         * @brief Write the runner's lane, bank and crouch squash, @p ahead
         *        seconds past the last tick.
         *
         * Vertical position is left alone: the solver owns it.
         *
         * @param ahead Seconds past the last tick; zero writes the tick pose.
         */
        void poseRunner(float ahead);

        /**
         * @brief Draw the runner this frame: its last tick carried forward.
         *
         * Keeps the tick pose in m_tickBody; onFixedUpdate restores it before simulating.
         *
         * @param ahead Seconds past the last tick.
         */
        void drawRunner(float ahead);

        /**
         * @brief Height of @p o's boarding ramp under the runner, @p ahead seconds past the last tick.
         *
         * @param o An obstacle carrying a ramp.
         * @param ahead Seconds past the last tick.
         * @return Feet height on the slope, clamped to the ramp's foot and its roof.
         */
        float rampHeight(const Obstacle& o, float ahead) const;

        /**
         * @brief Move the track one tick, recycling obstacles and coins at the far end.
         *
         * Moves no Transform; placeWorld does.
         *
         * @param dt Seconds to scroll; zero only refreshes where the coins sit.
         */
        void scrollWorld(float dt);

        /**
         * @brief Write every scrolling prop's Transform, @p ahead seconds past the last tick.
         *
         * @param ahead Seconds past the last tick; zero writes the tick pose.
         */
        void placeWorld(float ahead);

        void placeScenery(const std::vector<Scenery>& pool, float x, float y, float ahead);
        void updateCamera(float dt);

        void resetGame();
        void randomizeObstacle(Obstacle& o);
        void die();

        /// Refresh the HUD readouts and toggle the game-over overlay each frame.
        void refreshUI();

        float laneX(int lane) const {
            // The camera looks down +Z, so screen-right is world -X: lane 0 (left) is +X.
            return static_cast<float>(1 - lane) * laneWidth;
        }
        float obstacleHalfX() const { return laneWidth * 0.42f; }
        float obstacleZ(const Obstacle& o, float ahead) const {
            return o.z - m_speed * (1.0f + o.relFactor) * ahead;   // some trains bear down faster
        }
        float crouchTarget() const { return (m_crouchHeld && m_grounded) ? 1.0f : 0.0f; }
        /**
         * @brief Deterministic draw from the run's stream.
         *
         * @return A float in [0, 1).
         */
        float frand() { return m_rng.nextFloat(); }
        /**
         * @brief Uniform lane pick from the run's stream.
         *
         * @return A lane index, 0 to 2 inclusive.
         */
        int   randLane() { return m_rng.nextInt(0, 2); }

    private:
        MeshHandle      m_cubeMesh;
        MaterialHandle  m_matPlayer;
        MaterialHandle  m_matPlayerGlow;    ///< Emissive accent on the runner (visor band, pack).
        MaterialHandle  m_matTrain;         ///< Hull livery A: navy metal.
        MaterialHandle  m_matTrainB;        ///< Hull livery B: teal metal.
        MaterialHandle  m_matTrainC;        ///< Hull livery C: graphite metal.
        MaterialHandle  m_matWindow;        ///< Train windscreen / window glow.
        MaterialHandle  m_matHeadlamp;      ///< Warm nose light bar.
        MaterialHandle  m_matBarrier;       ///< Hazard body: matte red.
        MaterialHandle  m_matStripe;        ///< White reflective band on every hazard.
        MaterialHandle  m_matSignalRed;     ///< Trackside signal lamp heads (left wall).
        MaterialHandle  m_matSignalGreen;   ///< Trackside signal lamp heads (right wall).
        MaterialHandle  m_matCoin;
        MaterialHandle  m_matGround;
        MaterialHandle  m_matBallast;       ///< Raised gravel bed under each lane's track.
        MaterialHandle  m_matRail;
        MaterialHandle  m_matTie;
        MaterialHandle  m_matWall;
        MaterialHandle  m_matTrim;
        MaterialHandle  m_matPillar;
        MaterialHandle  m_matArch;
        AudioClipHandle m_footstep;         ///< Played per stride marker.
        AudioClipHandle m_coinChime;        ///< Played per coin.

        /// Invisible rig root the gameplay drives; visible parts parent under it.
        EntityId              m_player{};
        /// Part entity and its normal material, restored on reset.
        std::vector<std::pair<EntityId, MaterialHandle>> m_playerParts;
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
        std::vector<Scenery>  m_arches;       ///< Concrete ceiling ribs.
        std::vector<Scenery>  m_archLights;   ///< Light strip under each rib.
        float                 m_sceneryScroll = 0.0f;   ///< Within one wrap.
        float                 m_wallX = 4.0f;

        // Text entities are rewritten only when their value changes.
        EntityId m_startCanvas{};    ///< Title overlay, shown until the first run begins.
        EntityId m_uiScore{};
        EntityId m_uiDist{};
        EntityId m_uiCoins{};
        EntityId m_uiSpeed{};
        EntityId m_uiRideTag{};      ///< "ROOF RIDE" pill; visible only while on a roof.
        EntityId m_uiMilestone{};    ///< Distance flash; visible while m_milestoneTimer > 0.
        EntityId m_helpBox{};        ///< Scrolling how-to-play box; carries the UIScroll.
        EntityId m_helpThumb{};      ///< Its scrollbar thumb, placed each frame from that UIScroll.
        EntityId m_gameOverCanvas{};
        EntityId m_uiFinalScore{};
        EntityId m_uiFinalDist{};
        EntityId m_uiFinalCoins{};
        EntityId m_uiFinalBest{};
        int      m_shownScore = -1;  ///< Last values pushed to the HUD.
        int      m_shownDist  = -1;
        int      m_shownCoins = -1;
        int      m_shownSpeed = -1;

        bool  m_built    = false;
        bool  m_started  = false;  ///< False on the start screen.
        bool  m_alive    = true;
        bool  m_restartAsked = false;   ///< Game-over button clicked; the next tick restarts.
        int   m_lane     = 1;
        float m_playerX  = 0.0f;   ///< Lane position, eased toward m_lane each tick.
        float m_height   = 0.0f;   ///< Feet height, from the solver-owned Transform.
        // The last tick's solver support or ramp slope, held for COYOTE_TIME after leaving it.
        bool  m_grounded    = true;
        float m_coyoteTimer = 0.0f;
        int   m_climbRamp   = -1;   ///< Obstacle whose ramp was climbed this tick, or -1.
        bool  m_crouchHeld = false;   ///< On this tick's command.
        float m_crouch     = 0.0f;    ///< Smoothed crouch amount 0..1.
        float m_speed    = 0.0f;
        float m_distance = 0.0f;
        int   m_convoyLeft = 0;    ///< Recycles still owed to the convoy lane as trains.
        int   m_convoyLane = 1;
        int   m_convoyStyle = 0;   ///< Leader's livery; followers reuse it.
        // The obstacle recycled just before: randomizeObstacle's solvability guards
        // compare against it so two z-overlapping blockers never wall off the lanes.
        int   m_prevLane     = 1;
        float m_prevRel      = 0.0f;   ///< Its relFactor.
        bool  m_prevBlocking = false;  ///< Full blocker (train hull or barrier)?
        int   m_coinCount = 0;
        int   m_bonusScore = 0;    ///< Extra score from roof coins (they pay double).
        int   m_best     = 0;      ///< Best score this session.
        bool  m_newBest  = false;  ///< Set by the run that just ended.
        glm::vec2 m_camFollow{0.0f, CAMERA_REST_Y};   ///< Where the eye heads: x, then height.
        float m_camX     = 0.0f;   ///< Smoothed.
        float m_camY     = CAMERA_REST_Y;  ///< Smoothed.
        float m_camTime  = 0.0f;   ///< Phases the idle sway and light flicker.
        float m_milestoneTimer = 0.0f;  ///< Seconds left on the distance-milestone flash.
        float m_nextDistanceLog = 0.0f;

        // The last tick's pose, held while the Transform shows the drawn one (m_drawnAhead).
        Transform m_tickBody;
        bool      m_drawnAhead = false;

        // This tick's edges, read off its command in readInput.
        bool m_edgeLeft    = false;
        bool m_edgeRight   = false;
        bool m_edgeJump    = false;
        bool m_edgeRestart = false;

        /**
         * @brief The track's layout stream.
         *
         * Reseeded with RUN_SEED every run, so every run deals the same track.
         * Not reflected, so clone() does not copy it.
         */
        Math::Rng m_rng;
};

} // namespace Potion

VKM_REFLECT_BEGIN(::Potion::PotionRunner)
    VKM_F(laneWidth)
    VKM_F(startSpeed)
    VKM_F(maxSpeed)
    VKM_F(acceleration)
    VKM_F(jumpSpeed)
    VKM_F(gravity)
    VKM_F(coinValue)
VKM_REFLECT_END()

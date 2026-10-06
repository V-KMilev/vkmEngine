#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/engine_config.h"
#include "core/math/random.h"
#include "ecs/entity.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/script/reflected_behavior.h"

namespace Arena {

using namespace Vkm::Engine;

/**
 * @brief A profiling load: one scene that drives every engine subsystem at once.
 *
 * Built for Tracy capture, not play. The first play tick generates the world from
 * a fixed seed, so every machine gets the same load and captures are comparable.
 * Most dials are read once at build, so a change needs a play restart.
 *
 * The camera loop runs on simulated seconds: compare spans of it, not frame
 * numbers. F holds the camera; the number keys toggle subsystems (@ref readInput).
 */
class StressArena : public ReflectedBehavior<StressArena> {
    public:
        /**
         * @brief The camera's far plane, far enough that the far towers stay in frustum.
         *
         * Public so the module's scene is framed with it before Play.
         */
        static constexpr float CAMERA_FAR = 600.0f;

    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        // Defaults keep any one stage from dominating on a mid-range GPU. Raise one at a time.
        /// Instanced props: VisibilitySystem, GLInstanceBatcher, the Forward pass.
        int propCount = 4000;
        int towerCount = 180;      ///< Box-stack occluders, for the DepthPrepass and GTAO passes.
        int lightCount = 220;      ///< Point + spot lights: ClusterCull and the forward per-cluster loop.
        /**
         * @brief Static point lights that cast, out of @ref lightCount.
         *
         * Defaults to the cube atlas budget; the surplus past it draws unshadowed
         * and the backend warns once.
         */
        int shadowLights = static_cast<int>(Config::MAX_SHADOW_CASTERS_CUBE);
        int emitterCount = 40;     ///< Emitters, half additive: ParticleSystem and the Particles pass.
        int decalCount = 80;       ///< Ground decals: the Decals pass, which re-reads depth per projector.
        int physicsBodies = 220;   ///< Bodies in the central pit: PhysicsSystem, broadphase to solver.
        int animatedCount = 1500;  ///< Props with a track: AnimationSystem, and the transforms it dirties.
        int uiWidgetCount = 48;    ///< HUD widgets laid out every frame: UISystem and the UI pass.
        int reflectionProbes = 4;  ///< Reflection probes; each bakes six faces when it first appears.
        /**
         * @brief Instances of real cooked models scattered through the arena.
         *
         * Real triangle counts, bounds and albedo maps. Zero skips model loading,
         * as does a project with nothing cooked.
         */
        int modelInstances = 700;
        /**
         * @brief How many distinct cooked meshes to draw from.
         *
         * Instances add draws; kinds add resident geometry and break batches.
         */
        int modelKinds = 48;
        /**
         * @brief Distinct material instances the props draw from.
         *
         * Decides how well the draw sort batches: toward @ref propCount, every prop
         * breaks a batch. The palette has a floor of one, since props pick by `i % size`.
         */
        int uniqueMaterials = 12;

        /**
         * @brief Lights that patrol an orbit instead of standing still.
         *
         * Moving lights force a real cluster rebin every frame.
         */
        int movingLights = 90;
        /**
         * @brief Articulated flyers circulating over the block.
         *
         * Three-deep rigs that give HierarchySystem real work (the props are all
         * roots), change the visibility set, and some carry a shadowed spot.
         */
        int droneCount = 40;
        /**
         * @brief Debris pieces spawned per second, each destroyed seconds later.
         *
         * Entity churn: SparseSet insert/remove, SlotAllocator recycling, and a
         * drawable list that changes size every frame.
         */
        float debrisRate = 45.0f;
        /**
         * @brief Seconds between radial impulses that blast the physics pile apart.
         *
         * A settled pile costs nothing. Zero disables it.
         */
        float blastInterval = 6.0f;
        /**
         * @brief Materials whose emission is rewritten (and committed) every frame.
         *
         * Drives the version-gated GPU re-upload path.
         */
        int pulsingMaterials = 6;

        /**
         * @brief Give the round props distance-selected geometry.
         *
         * Off makes every prop draw its highest level always.
         */
        bool lodEnabled = true;

        bool scriptedCamera = true;    ///< Fly the camera loop; F toggles.
        float cameraLoopTime = 48.0f;  ///< Seconds per circuit.
        /**
         * @brief Seconds between hard camera cuts to another point on the loop.
         *
         * Replaces the whole visibility set at once: a deliberate spike, so 0 (off)
         * by default.
         */
        float cameraCutInterval = 0.0f;

    private:
        /**
         * @brief A light that orbits, dragging its visible fixture with it.
         *
         * Driven in code, not by a track, so the fixture follows the light exactly.
         */
        struct PatrolLight {
            EntityId entity;
            EntityId fixture; ///< The emissive source.
            float    radius;
            float    height;
            float    speed;   ///< Radians per second.
            float    phase;
            float    bobAmp;  ///< Vertical sway, so the orbit is not a flat circle.
        };

        /**
         * @brief An articulated flyer: body -> arm -> rotor, optionally lit.
         *
         * Only the body is moved; the rotor spins on its own Animation track.
         */
        struct Drone {
            EntityId body;
            EntityId lamp;  ///< Spot light child; null when unlit.
            float    radius;
            float    height;
            float    speed;
            float    phase;
        };

        struct Debris {
            EntityId entity;
            float    life;
        };

        /**
         * @brief One cooked model kind, plus the instances waiting on its load.
         *
         * The mesh loads off-thread, so updateModelScales sizes instances once it lands.
         */
        struct ModelKind {
            MeshHandle            mesh;
            std::vector<EntityId> instances;
            std::vector<float>    sizes;  ///< Per-instance target size, applied on fit.
            bool                  fitted = false;
        };

    private:
        void buildMaterials();
        void buildGround();
        void buildTowers();
        void buildProps();
        void buildLights();
        void buildEmitters();
        void buildDecals();
        void buildProbes();
        void buildPhysics();
        void buildModels();
        void buildDrones();
        void buildUI();

        EntityId spawnMesh(
            MeshHandle mesh,
            MaterialHandle material,
            const char* name,
            const glm::vec3& position,
            const glm::vec3& scale
        );
        MaterialHandle makeMaterial(const MaterialAsset& source, const char* name);

        void readInput();
        void updateCamera(float dt);
        void updatePhysics();
        void updateModelScales();
        void updatePatrolLights();
        void updateDrones();
        void updateDebris(float dt);
        void updateBlast(float dt);
        void updateMaterialPulse();
        void refreshUI(float dt);

        // Each toggle flips a component flag; nothing rebuilds.
        void setLightsEnabled(bool enabled);
        void setShadowsEnabled(bool enabled);
        void setPropsVisible(bool visible);
        void setParticlesEnabled(bool enabled);
        void setPhysicsEnabled(bool enabled);
        void setAnimationsEnabled(bool enabled);
        void setDecalsEnabled(bool enabled);
        void setUIVisible(bool visible);

        float frand() { return m_rng.nextFloat(); }
        float frand(float min, float max) { return m_rng.nextFloat(min, max); }

    private:
        // Few prop shapes, so same shape and material collapse into one instanced draw.
        MeshHandle m_cube;
        MeshHandle m_groundBand;  ///< One mesh per ground slab shape.
        MeshHandle m_groundSide;
        MeshHandle m_sphere;
        MeshHandle m_cylinder;
        MeshHandle m_sphereMid;   ///< Far LOD levels for the round shapes; cubes have none.
        MeshHandle m_sphereLow;
        MeshHandle m_cylMid;
        MeshHandle m_cylLow;

        /// uniqueMaterials entries.
        std::vector<MaterialHandle> m_propMaterials;
        MaterialHandle m_matGround;
        MaterialHandle m_matTower;
        MaterialHandle m_matGlass;      ///< The forward pass's blended path.
        MaterialHandle m_matChrome;     ///< Shows the reflections.
        MaterialHandle m_matEmissive;   ///< Feeds the bloom threshold.
        MaterialHandle m_matDecal;

        // Flat pools, so a toggle is one linear walk.
        std::vector<ModelKind> m_models;
        std::vector<EntityId> m_props;
        std::vector<EntityId> m_lights;
        /// The shadow toggle restores exactly these.
        std::vector<EntityId> m_shadowCasters;
        std::vector<EntityId> m_emitters;
        std::vector<EntityId> m_decals;
        std::vector<EntityId> m_spinners;  ///< Props carrying an Animation track.
        std::vector<EntityId> m_bodies;    ///< The pit's bodies, not the debris.
        std::vector<PatrolLight> m_patrol;
        std::vector<Drone> m_drones;
        std::vector<Debris> m_debris;
        std::vector<EntityId> m_uiWidgets;

        EntityId m_camera{};
        EntityId m_hudCanvas{};
        EntityId m_uiStats{};    ///< Frame timing, rewritten each second.
        EntityId m_uiToggles{};  ///< Toggle state, rewritten with it.

        bool  m_built = false;
        float m_camTime = 0.0f;     ///< Seconds along the scripted loop.
        float m_statsTimer = 0.0f;  ///< Throttles the HUD rewrite to once a second.
        int   m_frames = 0;         ///< Since the last HUD rewrite.
        float m_motionTime = 0.0f;  ///< Phases the patrols and rotors.
        float m_debrisAccum = 0.0f; ///< Fractional debris owed.
        float m_blastTimer = 0.0f;
        float m_cutTimer = 0.0f;
        /// Kinds still waiting on their mesh; updateModelScales idles at 0.
        int   m_unfittedKinds = 0;

        bool m_lightsOn     = true;
        bool m_shadowsOn    = true;
        bool m_propsOn      = true;
        bool m_particlesOn  = true;
        bool m_physicsOn    = true;
        bool m_animationsOn = true;
        bool m_decalsOn     = true;
        bool m_fogOn        = true;
        bool m_uiOn         = true;

        /**
         * @brief The build-time placement stream.
         *
         * Fixed seed, used only while building.
         */
        Math::Rng m_rng;

        /**
         * @brief The runtime churn stream, separate on purpose.
         *
         * Debris spawns per frame dt; drawn from m_rng, frame pacing would change the world.
         */
        Math::Rng m_churnRng;
};

} // namespace Arena

VKM_REFLECT_BEGIN(::Arena::StressArena)
    VKM_F(propCount)
    VKM_F(towerCount)
    VKM_F(lightCount)
    VKM_F(shadowLights)
    VKM_F(emitterCount)
    VKM_F(decalCount)
    VKM_F(physicsBodies)
    VKM_F(animatedCount)
    VKM_F(uiWidgetCount)
    VKM_F(reflectionProbes)
    VKM_F(modelInstances)
    VKM_F(modelKinds)
    VKM_F(uniqueMaterials)
    VKM_F(movingLights)
    VKM_F(droneCount)
    VKM_F(debrisRate)
    VKM_F(blastInterval)
    VKM_F(pulsingMaterials)
    VKM_F(cameraCutInterval)
    VKM_F(scriptedCamera)
    VKM_F(cameraLoopTime)
    VKM_F(lodEnabled)
VKM_REFLECT_END()

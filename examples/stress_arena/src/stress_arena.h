#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "system/render/render_limits.h"
#include "base/math/random.h"
#include "ecs/entity.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/script/behavior.h"

namespace Arena {

using namespace Vkm::Engine;

/**
 * @brief A profiling load: one scene that drives every engine subsystem at once.
 *
 * Built for Tracy capture, not play. The first play tick generates the world from
 * a fixed seed, so every machine gets the same load and captures are comparable.
 * Most dials are read once at build, so a change needs a play restart.
 *
 * The scene file holds what is not generated: a procedural sky, the same on every machine, its sun
 * off-axis so tower shadows fall across the block and the cascades span varied depth, with a small
 * Mie term, as a large one at that elevation washes the sky white; thin fog, one of the heaviest
 * passes, whose froxel grid costs the same at any density; and a far plane that keeps the far towers
 * in view.
 *
 * The camera loop runs on simulated seconds: compare spans of it, not frame
 * numbers. F holds the camera; the number keys toggle subsystems (@ref readInput).
 */
class StressArena : public Behavior<StressArena> {
    public:
        void onStart() override;
        void onFixedUpdate(float dt) override;
        void onUpdate(float dt) override;

    public:
        // Defaults keep any one stage from dominating on a mid-range GPU. Raise one at a time.
        int propCount  = 4000;  ///< Instanced props: VisibilitySystem, the renderer's instancing, the Forward
                                ///< pass.
        int towerCount = 180;   ///< Box-stack occluders, for the DepthPrepass and GTAO passes.
        int lightCount = 220;   ///< Point + spot lights: ClusterCull and the forward per-cluster loop.

        int shadowLights = static_cast<int>(Config::MAX_SHADOW_CASTERS_CUBE);
            ///< Static point lights that cast, out of lightCount. Defaults to the cube atlas budget; the
            ///< surplus past it draws unshadowed and the backend warns once.

        int emitterCount     = 40;    ///< Emitters, half additive: ParticleSystem and the Particles pass.
        int decalCount       = 80;    ///< Ground decals: the Decals pass, which re-reads depth per projector.
        int physicsBodies    = 220;   ///< Bodies in the central pit: PhysicsSystem, broadphase to solver.
        int animatedCount    = 1500;  ///< Props with a track: AnimationSystem, and the transforms it dirties.
        int uiWidgetCount    = 48;    ///< HUD widgets laid out every frame: UISystem and the UI pass.
        int reflectionProbes = 4;     ///< Reflection probes; each bakes six faces when it first appears.
        int uniqueMaterials  = 12;    ///< Distinct material instances the props draw from. Decides how well
                                      ///< the draw sort batches: toward propCount, every prop breaks a batch.
                                      ///< The palette has a floor of one, since props pick by `i % size`.

        int   movingLights     = 90;     ///< Lights that patrol an orbit instead of standing still. Moving
                                         ///< lights force a real cluster rebin every frame.
        int   droneCount       = 40;     ///< Articulated flyers circulating over the block. Three-deep rigs
                                         ///< that give HierarchySystem real work (the props are all roots),
                                         ///< change the visibility set, and some carry a shadowed spot.
        float debrisRate       = 45.0f;  ///< Debris pieces spawned per second, each destroyed seconds later.
                                         ///< Entity churn: SparseSet insert/remove, SlotAllocator recycling,
                                         ///< and a drawable list that changes size every frame.
        float blastInterval    = 6.0f;   ///< Seconds between radial impulses that blast the physics pile
                                         ///< apart. A settled pile costs nothing. Zero disables it.
        int   pulsingMaterials = 6;      ///< Materials whose emission is rewritten (and committed) every
                                         ///< frame. Drives the version-gated GPU re-upload path.

        bool lodEnabled = true;  ///< Give the round props distance-selected geometry. Off makes every prop
                                 ///< draw its highest level always.

        bool  scriptedCamera    = true;   ///< Fly the camera loop; F toggles.
        float cameraLoopTime    = 48.0f;  ///< Seconds per circuit.
        float cameraCutInterval = 0.0f;   ///< Seconds between hard camera cuts to another point on the loop.
                                          ///< Replaces the whole visibility set at once: a deliberate spike,
                                          ///< so 0 (off) by default.

    private:
        /**
         * @brief A light that orbits, dragging its visible fixture with it.
         *
         * Driven in code, not by a track, so the fixture follows the light exactly.
         */
        struct PatrolLight {
            EntityId entity;
            EntityId fixture;  ///< The emissive source.
            float    radius;
            float    height;
            float    speed;    ///< Radians per second.
            float    phase;
            float    bobAmp;   ///< Vertical sway, so the orbit is not a flat circle.
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

        // One draw a statement: a call's arguments run in an order each compiler picks, and GCC's is not
        // Clang's, so two draws in one call would build another world in another build.
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

        std::vector<MaterialHandle> m_propMaterials;  ///< uniqueMaterials entries.
        MaterialHandle              m_matGround;
        MaterialHandle              m_matTower;
        MaterialHandle              m_matGlass;       ///< The forward pass's blended path.
        MaterialHandle              m_matChrome;      ///< Shows the reflections.
        MaterialHandle              m_matEmissive;    ///< Feeds the bloom threshold.
        MaterialHandle              m_matDecal;

        // Flat pools, so a toggle is one linear walk.
        std::vector<EntityId>    m_props;
        std::vector<EntityId>    m_lights;
        std::vector<EntityId>    m_shadowCasters;  ///< The shadow toggle restores exactly these.
        std::vector<EntityId>    m_emitters;
        std::vector<EntityId>    m_decals;
        std::vector<EntityId>    m_spinners;       ///< Props carrying an Animation track.
        std::vector<EntityId>    m_bodies;         ///< The pit's bodies, not the debris.
        std::vector<PatrolLight> m_patrol;
        std::vector<Drone>       m_drones;
        std::vector<EntityId>    m_debris;         ///< The debris, dropped once destroyAfter takes it.
        std::vector<EntityId>    m_uiWidgets;

        EntityId m_camera{};
        EntityId m_hudCanvas{};
        EntityId m_uiStats{};    ///< Frame timing, rewritten each second.
        EntityId m_uiToggles{};  ///< Toggle state, rewritten with it.

        bool  m_built       = false;
        float m_camTime     = 0.0f;   ///< Seconds along the scripted loop.
        float m_statsTimer  = 0.0f;   ///< Throttles the HUD rewrite to once a second.
        int   m_frames      = 0;      ///< Since the last HUD rewrite.
        float m_motionTime  = 0.0f;   ///< Phases the patrols and rotors.
        float m_debrisAccum = 0.0f;   ///< Fractional debris owed.
        float m_blastTimer  = 0.0f;
        float m_cutTimer    = 0.0f;

        bool m_lightsOn     = true;
        bool m_shadowsOn    = true;
        bool m_propsOn      = true;
        bool m_particlesOn  = true;
        bool m_physicsOn    = true;
        bool m_animationsOn = true;
        bool m_decalsOn     = true;
        bool m_fogOn        = true;
        bool m_uiOn         = true;

        Math::Rng m_rng;  ///< The build-time placement stream. Fixed seed, used only while building.

        Math::Rng m_churnRng;  ///< The runtime churn stream, separate on purpose. Debris, blasts and
                               ///< relaunches draw from it on the tick, so a run is the same at any frame
                               ///< rate; apart from m_rng, so they cannot move the layout's draws.
};

} // namespace Arena

VKM_BEHAVIOR(::Arena::StressArena)
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

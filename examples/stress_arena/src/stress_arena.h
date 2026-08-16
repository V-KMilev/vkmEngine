#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/math/random.h"
#include "ecs/entity.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/script/reflected_behavior.h"

namespace Engine {

/**
 * @brief A profiling load: one scene that drives every engine subsystem at once.
 *
 * Built for capture, not for play. The point is to put the whole pipeline under
 * simultaneous, *representative* load - a daylit city block dense with geometry,
 * lights, shadows, transparency, particles, decals, physics and UI - so a Tracy
 * capture shows where frame time actually goes when nothing is idle. A scene
 * that stresses one subsystem at a time hides exactly the interactions worth
 * finding (shadow casters inflating the visibility pass, transparent draws
 * serialising behind the depth prepass, probe re-bakes landing on a frame that
 * was already long).
 *
 * Just as important, almost nothing here holds still. A static scene is the easy
 * case for most of the pipeline: the cluster grid keeps its bins between frames,
 * culling sets barely change, instanced batches keep their membership, and the
 * ECS never allocates - so a still benchmark reports numbers a real game never
 * sees. Lights patrol, drones fly articulated rigs through the frustum, debris
 * spawns and dies continuously, the physics pile is blasted apart before it can
 * settle, and a few materials are rewritten every frame. See the motion dials.
 *
 * Attach one instance to an otherwise empty entity; the project's module
 * seeds that entity in vkmBuildScene.
 * On the first play tick it generates the world procedurally from three in-code
 * meshes plus whatever the project has cooked, then drives it every frame. The
 * procedural half never touches the disk and the whole scene runs off a fixed
 * seed, so a given build produces the same load on every machine and every run -
 * two captures are comparable.
 *
 * What each dial reaches, and the zone to watch in Tracy:
 *  - @ref propCount    - drawables. VisibilitySystem (cull), GLInstanceBatcher
 *                        (merge), GPU.Forward. Props share three meshes and a
 *                        small material palette, so this measures the batcher's
 *                        best case; @ref uniqueMaterials breaks it deliberately.
 *  - @ref towerCount   - static occluders with depth complexity: GPU.DepthPrepass
 *                        and GPU.GTAO care, the batcher does not.
 *  - @ref lightCount   - GPU.Cluster (light binning) and the forward pass's
 *                        per-cluster loop. The Forward+ path is what this exists
 *                        to bend.
 *  - @ref shadowLights - GPU.Shadow, one atlas tile per caster, plus the
 *                        shadow-caster gather on the CPU. The most expensive
 *                        dial per unit; keep it low unless it is the subject.
 *  - @ref emitterCount - ParticleSystem (CPU integration) and GPU.Particle
 *                        (sorted transparent billboards).
 *  - @ref decalCount   - GPU.Decal, which re-reads depth per projector.
 *  - @ref physicsBodies- PhysicsSystem: broadphase, narrowphase, and a solver
 *                        running @ref Environment::solverIterations passes on a
 *                        pile that never fully settles.
 *  - @ref animatedCount- AnimationSystem track evaluation + the HierarchySystem
 *                        walk their dirty transforms force.
 *  - @ref uiWidgetCount- UISystem layout and GPU.UI.
 *
 * Two capture modes, because they answer different questions. The scripted
 * camera (default) flies a fixed loop, so frame N of one capture is the same
 * viewpoint as frame N of the next and a regression shows up as a diff rather
 * than as noise. Press F to take manual control when a spike needs chasing to a
 * specific spot; the scripted path resumes from where it left off.
 *
 * Number keys toggle one subsystem each at runtime (see @ref readInput). That is
 * the fast attribution loop: hold a Tracy capture open, toggle a subsystem, and
 * read the delta off the frame graph directly instead of rebuilding with new
 * dial values.
 */
class StressArena : public ReflectedBehavior<StressArena> {
    public:
        static constexpr const char* TYPE_NAME = "StressArena";

        void onStart() override;
        void onUpdate(float dt) override;

        // Load dials. Defaults aim at a load heavy enough that no single stage
        // trivially dominates on a mid-range GPU - the state where the profile is
        // actually informative. Raise one at a time; they are read once, at
        // build, so a change needs a play restart.
        int propCount = 4000;      ///< Scattered instanced props (cube/sphere/cylinder).
        int towerCount = 180;      ///< Buildings, each a stack of boxes.
        int lightCount = 220;      ///< Point + spot lights bound into the cluster grid.
        int shadowLights = 6;      ///< How many of those cast shadows (atlas tiles).
        int emitterCount = 40;     ///< Particle emitters (half additive sparks, half alpha smoke).
        int decalCount = 80;       ///< Projected decals on the ground plane.
        int physicsBodies = 220;   ///< Dynamic rigidbodies tumbling in the central pit.
        int animatedCount = 1500;  ///< Props carrying an Animation track (a subset of propCount).
        int uiWidgetCount = 48;    ///< HUD text/image widgets laid out every frame.
        int reflectionProbes = 4;  ///< Reflection probes; each re-bakes six faces when it first appears.
        /**
         * @brief Instances of real cooked models scattered through the arena.
         *
         * The procedural props measure a best case the engine rarely sees:
         * three meshes, uniform vertex density, no textures. Cooked models bring
         * what actually ships - irregular triangle counts, real bounds that make
         * frustum and screen-size culling do non-trivial work, and sampled
         * albedo maps, which is the only thing here that exercises the async
         * cooked-asset path and the material texture bindings.
         *
         * Zero skips model loading entirely, which is also the fallback when the
         * project has nothing cooked.
         */
        int modelInstances = 700;
        /**
         * @brief How many distinct cooked meshes to draw from.
         *
         * Separate from @ref modelInstances because the two cost different
         * things: instances add draws, kinds add unique geometry. More kinds
         * means more vertex buffers resident and fewer instances merging per
         * batch, which is the shape of a real level rather than one prop
         * repeated.
         */
        int modelKinds = 48;
        /**
         * @brief Distinct material instances the props draw from.
         *
         * The knob that decides how well the draw sort batches. Low values let
         * the batcher merge props into few instanced draws; raising it toward
         * @ref propCount forces a batch break per prop, which is what turns
         * GPU.Forward from vertex-bound into draw-call-bound.
         */
        int uniqueMaterials = 12;

        // Motion. A static scene is the easy case for most of the pipeline and
        // hides the costs worth finding: the cluster grid stays coherent between
        // frames, culling sets barely change, instanced batches keep their
        // membership, and the ECS never allocates. These dials break all of that
        // on purpose.

        /**
         * @brief Lights that patrol an orbit instead of standing still.
         *
         * The single most useful dial here for a Forward+ renderer. Static
         * lights let the cluster grid keep the same bins frame after frame;
         * moving ones force a genuine rebin every frame and keep shifting which
         * clusters are hot, which is what GPU.Cluster costs in a real scene.
         */
        int movingLights = 90;
        /**
         * @brief Articulated flyers circulating over the block.
         *
         * Each is a three-deep parented rig (body -> arm -> rotor), so moving
         * one dirties a subtree and gives HierarchySystem's depth-bucketed
         * resolve real work - the scattered props are all hierarchy roots and
         * never exercise it. They also travel in and out of the frustum, so the
         * visibility set actually changes, and a share of them carry a spot
         * light, which moves a shadow caster's frustum every frame.
         */
        int droneCount = 40;
        /**
         * @brief Debris pieces spawned per second, each destroyed seconds later.
         *
         * Continuous entity churn: SparseSet insert/remove, SlotAllocator
         * recycling, and a drawable list whose size changes every frame so no
         * batch can be reused wholesale. Benchmarks almost always run a fixed
         * entity set and miss this entirely, and it is exactly what a game does
         * constantly.
         */
        float debrisRate = 45.0f;
        /**
         * @brief Seconds between radial impulses that blast the physics pile apart.
         *
         * A pile that packs down stops costing anything: contacts stabilise and
         * the solver coasts. Re-scattering it keeps the broadphase pair count
         * and the solver iterations near their peak. Zero disables it.
         */
        float blastInterval = 6.0f;
        /**
         * @brief Materials whose emission is rewritten (and committed) every frame.
         *
         * Drives the version-gated GPU re-upload path - the one thing a scene
         * of fixed materials never touches, and the path a game hits whenever
         * anything pulses, flashes or fades.
         */
        int pulsingMaterials = 6;

        /**
         * @brief Give the round props distance-selected geometry.
         *
         * Off makes every prop draw its highest level always, which is the
         * comparison that says what LOD is worth in this scene.
         */
        bool lodEnabled = true;

        bool scriptedCamera = true;    ///< Fly the fixed camera loop (F toggles manual control).
        float cameraLoopTime = 48.0f;  ///< Seconds for one full circuit of the scripted path.
        /**
         * @brief Seconds between hard camera cuts to another point on the loop.
         *
         * The worst case for frame-to-frame coherence: the whole visibility set
         * is replaced at once, every newly visible mesh and material syncs in
         * the same frame, and any temporal accumulation restarts. Off by default
         * (0) because it puts a deliberate spike in an otherwise smooth capture
         * - turn it on when that spike is the thing being measured.
         */
        float cameraCutInterval = 0.0f;

    private:
        /**
         * @brief A light that orbits, dragging its visible fixture with it.
         *
         * Driven in code rather than by an Animation track because the light's
         * position has to be authoritative for the fixture too, and because the
         * point is to move it every single frame.
         */
        struct PatrolLight {
            EntityId entity;
            EntityId fixture; ///< The emissive source, kept at the light's position.
            float    radius;
            float    height;
            float    speed;   ///< Radians per second around the arena centre.
            float    phase;
            float    bobAmp;  ///< Vertical sway, so the orbit is not a flat circle.
        };

        /**
         * @brief An articulated flyer: body -> arm -> rotor, optionally lit.
         *
         * Only the body is moved; the rotor spins on its own Animation track and
         * the arm exists to make the chain three deep. Moving the body dirties
         * the whole subtree, which is the load this is here for.
         */
        struct Drone {
            EntityId body;
            EntityId lamp;  ///< Spot light child; default-constructed when this one is unlit.
            float    radius;
            float    height;
            float    speed;
            float    phase;
        };

        /**
         * @brief One live debris piece, destroyed when its life runs out.
         */
        struct Debris {
            EntityId entity;
            float    life;
        };

        /**
         * @brief One cooked model kind, plus the instances waiting on its load.
         *
         * A cooked mesh arrives as an empty stub and is filled in off-thread, so
         * its bounds are unknown at build time and its instances cannot be sized
         * yet. updateModelScales fits them once the vertices land - see there for
         * why the scale is not simply baked in at spawn.
         */
        struct ModelKind {
            MeshHandle            mesh;
            std::vector<EntityId> instances;
            std::vector<float>    sizes;  ///< Per-instance target size in world units, applied on fit.
            bool                  fitted = false;
        };

        // Setup
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

        EntityId spawnMesh(MeshHandle mesh, MaterialHandle material, const char* name,
                           const glm::vec3& position, const glm::vec3& scale);
        MaterialHandle makeMaterial(const MaterialAsset& source, const char* name);

        // Per-frame steps
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

        // Subsystem toggles, each driven by one number key. Every one flips a
        // component flag the engine already honours rather than rebuilding, so a
        // toggle costs a walk and the next frame shows the difference.
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

        // Cached from the BehaviorContext in onStart; the context is
        // session-stable, so these stay valid for life.
        Scene*           m_scene     = nullptr;
        ResourceManager* m_resources = nullptr;
        WindowManager*   m_window    = nullptr;

        // Procedural geometry. Three shapes, so props of the same shape and
        // material collapse into one instanced draw.
        MeshHandle m_cube;
        MeshHandle m_sphere;
        MeshHandle m_cylinder;
        MeshHandle m_sphereMid;   ///< Far LOD levels for the round shapes; cubes have none.
        MeshHandle m_sphereLow;
        MeshHandle m_cylMid;
        MeshHandle m_cylLow;

        std::vector<MaterialHandle> m_propMaterials;  ///< The palette props draw from (uniqueMaterials entries).
        MaterialHandle m_matGround;
        MaterialHandle m_matTower;
        MaterialHandle m_matGlass;      ///< Transparent + transmission: the forward pass's blended path.
        MaterialHandle m_matChrome;     ///< Smooth metal: shows probe / GI reflections.
        MaterialHandle m_matEmissive;   ///< Feeds the bloom threshold.
        MaterialHandle m_matDecal;

        // World entities, kept in flat pools so a toggle is one linear walk.
        std::vector<ModelKind> m_models;
        std::vector<EntityId> m_props;
        std::vector<EntityId> m_lights;
        std::vector<EntityId> m_emitters;
        std::vector<EntityId> m_decals;
        std::vector<EntityId> m_spinners;  ///< Props carrying an Animation track.
        std::vector<EntityId> m_bodies;    ///< The rigidbodies in the pit (not the debris).
        std::vector<PatrolLight> m_patrol;
        std::vector<Drone> m_drones;
        std::vector<Debris> m_debris;
        std::vector<EntityId> m_uiWidgets;

        EntityId m_camera{};
        EntityId m_hudCanvas{};
        // Two elements rather than one two-line string: UISystem lays a UIText
        // out as a single run and has no newline handling.
        EntityId m_uiStats{};    ///< Frame timing, rewritten each second.
        EntityId m_uiToggles{};  ///< Live toggle state, rewritten with it.

        // Runtime state.
        bool  m_built = false;
        float m_camTime = 0.0f;     ///< Position along the scripted loop, in seconds.
        float m_statsTimer = 0.0f;  ///< Throttles the HUD rewrite to once a second.
        int   m_frames = 0;         ///< Frames since the last HUD rewrite.
        float m_motionTime = 0.0f;  ///< Clock the patrols and rotors phase against.
        float m_debrisAccum = 0.0f; ///< Fractional debris owed since the last whole spawn.
        float m_blastTimer = 0.0f;  ///< Seconds until the next pile blast.
        float m_cutTimer = 0.0f;    ///< Seconds until the next camera cut.
        int   m_unfittedKinds = 0;  ///< Model kinds still waiting on their mesh; updateModelScales idles at 0.

        // Live toggle state, each mirroring one number key.
        bool m_lightsOn     = true;
        bool m_shadowsOn    = true;
        bool m_propsOn      = true;
        bool m_particlesOn  = true;
        bool m_physicsOn    = true;
        bool m_animationsOn = true;
        bool m_decalsOn     = true;
        bool m_fogOn        = true;
        bool m_uiOn         = true;

        /// Fixed seed, and used only while building: the whole world is placed
        /// before the first update, so the layout is identical on every run and
        /// two captures of the same build are comparable frame for frame.
        Math::Rng m_rng;

        /// Separate stream for the runtime churn. How many debris pieces spawn
        /// in a given frame depends on that frame's dt, so drawing them from
        /// m_rng would let frame pacing perturb the sequence - and a machine
        /// that ran slightly faster would get a different world. Splitting the
        /// streams keeps the build-time layout provably untouched by timing.
        Math::Rng m_churnRng;
};
} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::StressArena)
    VKM_F(propCount),
    VKM_F(towerCount),
    VKM_F(lightCount),
    VKM_F(shadowLights),
    VKM_F(emitterCount),
    VKM_F(decalCount),
    VKM_F(physicsBodies),
    VKM_F(animatedCount),
    VKM_F(uiWidgetCount),
    VKM_F(reflectionProbes),
    VKM_F(modelInstances),
    VKM_F(modelKinds),
    VKM_F(uniqueMaterials),
    VKM_F(movingLights),
    VKM_F(droneCount),
    VKM_F(debrisRate),
    VKM_F(blastInterval),
    VKM_F(pulsingMaterials),
    VKM_F(cameraCutInterval),
    VKM_F(scriptedCamera),
    VKM_F(cameraLoopTime),
    VKM_F(lodEnabled)
VKM_REFLECT_END()

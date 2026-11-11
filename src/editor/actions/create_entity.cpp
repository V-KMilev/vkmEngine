#include "actions/create_entity.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "undo/entity_commands.h"
#include "undo/entity_snapshot.h"
#include "editor_state.h"
#include "view/view_placement.h"
#include "actions/copy_parent.h"
#include "ecs/scene.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/effect/decal.h"
#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/light/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/light/reflection_probe.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/landscape/water.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_build.h"
#include "ecs/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "resource/asset/terrain_asset.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/asset/generate/material_generators.h"
#include "actions/asset_actions.h"
#include "actions/prefab_actions.h"
#include "ui/asset/asset_labels.h"
namespace Vkm::Engine {
namespace EditorActions {

/**
 * @brief What a kind's build is handed.
 */
struct CreateKit {
    Scene&           scene;
    ResourceManager& resources;
    EditorState&     state;
};

namespace {

// Meters across Create > Sea's surface: open water past most of what a scene builds by it.
constexpr float SEA_SIZE = 200.0f;
// The shortest wave its vertices are spaced for, meters, however short the scene's: a shorter swell
// wave is drawn by too few vertices.
constexpr float MIN_SEA_WAVE = 1.0f;

// Ground walked on as drawn: @p terrain, and a static body whose collider is its grid.
void addTerrainBody(Scene& scene, EntityId entity, TerrainHandle terrain) {
    scene.add(entity, Terrain{terrain});
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add(entity, std::move(body));
    ColliderPart part;
    part.shape = ColliderShape::Terrain;
    Collider collider;
    collider.parts = {part};
    scene.add(entity, std::move(collider));
}

// Makes an entity of default Ts: what most kinds start as.
template <typename... Ts>
EntityId made(CreateKit& kit) {
    const EntityId entity = kit.scene.createEntity();
    (kit.scene.add(entity, Ts{}), ...);
    return entity;
}

template <LightType TYPE>
EntityId light(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    kit.scene.add(entity, generateLight(TYPE));
    return entity;
}

// Reuses what the graph already holds under the names the mesh and the default material take.
EntityId primitive(CreateKit& kit, MeshAsset mesh) {
    const EntityId   entity = made<Transform>(kit);
    const MeshHandle shape  = addGeneratedMesh(kit.resources, std::move(mesh));
    kit.scene.add(entity, Mesh{shape, generateDefaultMaterial(kit.resources)});
    return entity;
}

EntityId cube(CreateKit& kit) { return primitive(kit, generateCube()); }
EntityId sphere(CreateKit& kit) { return primitive(kit, generateSphere()); }
EntityId plane(CreateKit& kit) { return primitive(kit, generatePlane()); }
EntityId cone(CreateKit& kit) { return primitive(kit, generateCone()); }
EntityId pyramid(CreateKit& kit) { return primitive(kit, generatePyramid()); }
EntityId triangle(CreateKit& kit) { return primitive(kit, generateTriangle()); }

EntityId camera(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    Camera cam;
    // Inactive unless there is no active camera, so it cannot take the game's view.
    cam.active = !findActiveCamera(kit.scene);
    kit.scene.add(entity, cam);
    return entity;
}

// What a CharacterController needs: a capsule and a body that will not tip over or doze off; and
// something to see, a child cylinder of the capsule's size, its own entity so the capsule's origin
// stays at the feet.
EntityId character(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    ColliderPart part;
    part.shape      = ColliderShape::Capsule;
    part.radius     = 0.3f;
    part.halfHeight = 0.6f;
    part.center     = {0.0f, part.halfHeight + part.radius, 0.0f};
    Collider collider;
    collider.parts = { part };
    kit.scene.add(entity, std::move(collider));

    Rigidbody body;
    body.mass           = 70.0f;
    body.freezeRotation = true;
    body.canSleep       = false;
    kit.scene.add(entity, std::move(body));

    kit.scene.add(entity, CharacterController{});

    const EntityId shown = made<Transform>(kit);
    kit.scene.get<Transform>(shown).position = part.center;
    const float height     = 2.0f * (part.halfHeight + part.radius);
    const MeshHandle shape = addGeneratedMesh(kit.resources, generateCylinder({part.radius, height}));
    kit.scene.add(shown, Mesh{shape, generateDefaultMaterial(kit.resources)});
    kit.scene.add(shown, makeName("Body"));
    HierarchyOperations::setParent(kit.scene, shown, entity);
    return entity;
}

// A blocker the world collides with and nothing moves: a cube, so a solid block is one step, its
// collider the cube's own size.
EntityId staticBody(CreateKit& kit) {
    const EntityId entity = cube(kit);
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    kit.scene.add(entity, std::move(body));
    kit.scene.add(entity, Collider{});
    return entity;
}

// A volume that reports what moves through it: a static body whose box senses and blocks nothing.
EntityId trigger(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    kit.scene.add(entity, std::move(body));
    Collider collider;
    collider.isTrigger = true;
    kit.scene.add(entity, std::move(collider));
    return entity;
}

// A new terrain is for sculpting: the window with the brushes comes forward.
EntityId terrain(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    addTerrainBody(kit.scene, entity, createNewTerrain(kit.resources));
    kit.state.openTerrain(entity);
    return entity;
}

// A still pool to stretch or reshape, on a material of its own: its Flow runs it along the mesh's +U, so
// a long strip of it is a river.
EntityId water(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    const MeshHandle sheet = addGeneratedMesh(kit.resources, generatePlane({20.0f, 20.0f, 20, 20}));
    const MaterialHandle shared = generateWaterMaterial(kit.resources);
    Mesh surface{sheet, copyMaterial(kit.resources, shared, "Water")};
    surface.castShadows = false;
    surface.movable     = false;
    kit.scene.add(entity, surface);
    kit.scene.add(entity, Water{});
    return entity;
}

// A surface dense enough to carry the scene's waves, on a material of its own: vertices closer than half
// the shortest wave, as the swell needs to be drawn whole.
EntityId sea(CreateKit& kit) {
    const EntityId entity = made<Transform>(kit);
    const float shortest = kit.scene.environment().waves.shape.minWavelength;
    const float spacing  = 0.5f * std::max(shortest, MIN_SEA_WAVE);
    const auto across = static_cast<uint32_t>(std::ceil(SEA_SIZE / spacing));
    MeshAsset grid = generatePlane({SEA_SIZE, SEA_SIZE, across, across});
    const MeshHandle sheet = addGeneratedMesh(kit.resources, std::move(grid));
    Mesh             surface{sheet, copyMaterial(kit.resources, generateSeaMaterial(kit.resources), "Sea")};
    surface.castShadows = false;
    surface.movable     = false;
    kit.scene.add(entity, surface);
    // The whole of the waves and ripples, no bottom, its crests and waterlines foaming.
    Water open;
    open.waves     = 1.0f;
    open.ripples   = 1.0f;
    open.depth     = 0.0f;
    open.foam      = 0.5f;
    open.waterline = 0.6f;
    kit.scene.add(entity, open);
    return entity;
}

// A control is the engine's, parts and all (UI::slider and its siblings).
const glm::vec2 MIDDLE(0.5f, 0.5f);
const glm::vec2 HERE(0.0f, 0.0f);

EntityId slider(CreateKit& kit) {
    return UI::slider(kit.scene, {}, UIElement::at(MIDDLE, HERE, {240.0f, 12.0f}), 0.0f, 1.0f, 0.5f);
}
EntityId toggle(CreateKit& kit) {
    return UI::toggle(kit.scene, {}, UIElement::at(MIDDLE, HERE, {64.0f, 32.0f}), false);
}
EntityId choice(CreateKit& kit) {
    const UIElement row = UIElement::at(MIDDLE, HERE, {240.0f, 36.0f});
    return UI::choice(kit.scene, {}, row, {"Low", "Medium", "High"}, 1);
}

constexpr const char* WATER_TIP = "A pool on a material of its own. Set its Flow and stretch it: a river.";
constexpr const char* SEA_TIP   = "Water taking all of the scene's waves";
constexpr const char* SENSE_TIP = "A box that reports what moves into it (TriggerEvent) and blocks nothing";

constexpr std::array<EntityKindRow, static_cast<size_t>(EntityKind::Count)> KINDS = {{
#define VKM_ENTITY_KIND_ROW(kind, name, label, icon, group, tip, ...) \
    {EntityKind::kind, name, label, EditorIcon::icon, CreateGroup::group, tip, &__VA_ARGS__},
    VKM_ENTITY_KINDS(VKM_ENTITY_KIND_ROW)
#undef VKM_ENTITY_KIND_ROW
}};

} // namespace

EntityId createEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityKind kind,
    EntityId parent
) {
    if (refusesNewChild(scene, parent)) {
        const char* message = "A prefab instance is built from its prefab - Unpack it to add to it";
        state.toasts.push(ToastKind::Warning, message);
        return {};
    }
    const EntityKindRow& row = KINDS[static_cast<size_t>(kind)];
    CreateKit kit{scene, resources, state};
    const EntityId entity = row.build(kit);
    scene.add(entity, makeName(row.name));

    // Where the view is looking, standing on what it meets. A camera takes the view's
    // own pose, so it sees what the author does; a terrain is the scene's ground and a sea its
    // water, at the origin.
    Transform* placed = scene.tryGet<Transform>(entity);
    if (placed && !parent && kind != EntityKind::Terrain && kind != EntityKind::Sea) {
        if (kind == EntityKind::Camera && state.viewAim.known) {
            placed->position = state.viewAim.ray.origin;
            placed->rotation = state.viewAim.rotation;
        } else {
            const Mesh* mesh = scene.tryGet<Mesh>(entity);
            const float base = mesh ? ViewPlacement::meshBase(resources, mesh->mesh) : 0.0f;
            placed->position = ViewPlacement::placeInView(scene, resources, state.viewAim, base);
        }
    }

    // A new UI element goes under the selected canvas or element so it renders at
    // once: it needs a UICanvas ancestor to be laid out and drawn.
    uint32_t parentSlot   = 0;
    const EntityId active = state.selection.active();
    if (parent) {
        HierarchyOperations::setParent(scene, entity, parent);
        parentSlot = parent.slot();
    } else if (scene.has<UIElement>(entity) && active && scene.isAlive(active)
        && (scene.has<UICanvas>(active) || scene.has<UIElement>(active))) {
        // Unless that belongs to a prefab instance, whose interior is never
        // written: the element would be gone after the next load.
        if (refusesNewChild(scene, active)) {
            const char* message =
                "A prefab instance is built from its prefab - the new element is "
                "left outside it, and needs a canvas the scene owns";
            state.toasts.push(ToastKind::Warning, message);
        } else {
            HierarchyOperations::setParent(scene, entity, active);
            parentSlot = active.slot();
        }
    }

    // A control's parts come with it, as one step. The parent slot rides along because
    // EntitySnapshot is leaf-only: without it a redone UI element comes back as a root and stops
    // being drawn.
    const Hierarchy* link = scene.tryGet<Hierarchy>(entity);
    if (link && link->firstChild) {
        SubtreeSnapshot made = SubtreeSnapshot::capture(scene, entity);
        state.pushStep(std::make_unique<CreateSubtreeCommand>(std::move(made), "Create Entity"));
        return entity;
    }
    state.pushStep(
        std::make_unique<CreateEntityCommand>(
            EntitySnapshot::capture(scene, entity),
            "Create Entity",
            parentSlot
        )
    );
    return entity;
}

const std::array<EntityKindRow, static_cast<size_t>(EntityKind::Count)>& entityKinds() {
    return KINDS;
}

bool makesEntity(AssetType type) {
    return type == AssetType::Mesh || type == AssetType::AudioClip || type == AssetType::Terrain;
}

EntityId createFromAsset(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    AssetType type,
    StorageIndex key,
    const glm::vec3& at
) {
    std::string name;
    if (type == AssetType::Mesh) {
        if (!resources.tryGet(MeshHandle{key})) return {};
        name = assetShortName(resources.get(MeshHandle{key}).name());
    } else if (type == AssetType::AudioClip) {
        if (!resources.tryGet(AudioClipHandle{key})) return {};
        name = assetShortName(resources.get(AudioClipHandle{key}).name());
    } else if (type == AssetType::Terrain) {
        if (!resources.tryGet(TerrainHandle{key})) return {};
        name = "Terrain";
    } else {
        return {};
    }

    const EntityId entity = scene.createEntity();
    Transform pose;
    // A terrain is ground for the whole scene, centered on its entity: at the origin, as Create puts one.
    if (type != AssetType::Terrain) pose.position = at;
    scene.add(entity, std::move(pose));
    scene.add(entity, makeName(name.empty() ? "Entity" : name.c_str()));
    if (type == AssetType::Mesh) {
        scene.add(entity, Mesh{MeshHandle{key}, generateDefaultMaterial(resources)});
    } else if (type == AssetType::Terrain) {
        addTerrainBody(scene, entity, TerrainHandle{key});
    } else {
        AudioSource source;
        source.clip = AudioClipHandle{key};
        scene.add(entity, std::move(source));
    }
    state.pushStep(
        std::make_unique<CreateEntityCommand>(EntitySnapshot::capture(scene, entity), "Create Entity")
    );
    state.selectEntity(entity);
    return entity;
}

} // namespace EditorActions
} // namespace Vkm::Engine

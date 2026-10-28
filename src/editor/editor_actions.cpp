#include "editor_actions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "io/asset/asset_library.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "command/component_edit.h"
#include "command/editor_commands.h"
#include "editor_state.h"
#include "input/view_placement.h"
#include "command/prefab_overrides.h"
#include "copy_parent.h"
#include "ecs/scene.h"
#include "system/script/script_component.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/ocean.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/planar_reflector.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/render/terrain.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/generate/light_generators.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/material_generators.h"
#include "session/content_pack.h"
#include "session/asset_writes.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"

namespace Vkm::Engine {
namespace EditorActions {

namespace {

/// A prefab file as a menu reads it: "smoke_plume.json" is "Smoke Plume".
std::string effectLabel(const std::string& file) {
    std::string label = std::filesystem::path(file).stem().string();
    bool wordStart = true;
    for (char& c : label) {
        if (c == '_') {
            c = ' ';
            wordStart = true;
        } else if (wordStart) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            wordStart = false;
        }
    }
    return label;
}

} // namespace

void reparentKeepingWorld(
    Scene& scene,
    EditorState& state,
    EntityId child,
    EntityId newParent,
    const char* label,
    EntityId before
) {
    if (!scene.isAlive(child) || newParent == child || before == child) return;

    // Refused here although setParent declines a cycle too: the rest would still
    // rebase and push a step for a move that never happened.
    if (HierarchyOperations::isAncestorOf(scene, child, newParent)) {
        state.pushToast(ToastKind::Warning, "An entity cannot be moved under one of its own descendants");
        return;
    }

    Transform* local = scene.tryGet<Transform>(child);
    const bool hasTransform = local != nullptr;

    // An instance's interior belongs to its prefab, so neither move survives a
    // load; see docs/reference/editor.md, "What an instance will not let you do".
    if (refusesNewChild(scene, newParent)) {
        const char* message =
            "A prefab instance is built from its prefab - an entity "
            "moved into one is not saved with the scene";
        state.pushToast(ToastKind::Warning, message);
        return;
    }
    const EntityId owningInstance = Prefab::instanceRootOf(scene, child);
    if (owningInstance && owningInstance != child) {
        const char* message =
            "This entity belongs to a prefab - move the instance root, or change the prefab and save it";
        state.pushToast(ToastKind::Warning, message);
        return;
    }

    EntityId oldParent{};
    EntityId oldNext{};
    if (const Hierarchy* node = scene.tryGet<Hierarchy>(child)) {
        oldParent = node->parent;
        oldNext   = node->nextSibling;
    }
    const bool toParent = newParent && scene.isAlive(newParent);
    // Only a child of the new parent places it; anything else puts it last.
    const Hierarchy* beside = scene.tryGet<Hierarchy>(before);
    const EntityId   next   = toParent && beside && beside->parent == newParent ? before : EntityId{};
    // Dropped where it already stands: under the same parent, before the same sibling (roots have no order).
    const EntityId target = toParent ? newParent : EntityId{};
    if (oldParent == target && (oldNext == next || !target)) return;

    // The world matrix is what stays fixed across the move; `pose` is for undo.
    const Transform pose = hasTransform ? *local : Transform{};
    const glm::mat4 world  = hasTransform
        ? HierarchyOperations::computeWorldMatrix(scene, child)
        : glm::mat4(1.0f);

    if (toParent) {
        HierarchyOperations::setParent(scene, child, newParent, next);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }

    Transform after = pose;
    if (hasTransform) {
        // Unparenting to the root leaves local == world.
        glm::mat4 rebased = world;
        if (toParent && scene.has<Transform>(newParent)) {
            rebased = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, newParent)) * world;
        }
        *local = Transform::fromModelMatrix(rebased);
        after = *local;
    }

    state.pushStep(
        std::make_unique<ReparentCommand>(
            child,
            oldParent,
            oldNext,
            toParent ? newParent : EntityId{},
            next,
            pose,
            after,
            label
        )
    );

    if (scene.has<UIElement>(child) && !hasCanvasAncestor(scene, child)) {
        const char* message =
            "UI elements are drawn by the canvas above them - this one has "
            "no canvas ancestor now, so it will not appear";
        state.pushToast(ToastKind::Warning, message);
    }
}

namespace {
// First material name not already taken: `base`, then "base 2", "base 3", ...
// Neither loaded nor in the library: a save would write over a library material not loaded.
std::string uniqueMaterialName(ResourceManager& resources, const std::string& base) {
    const auto taken = [&](const std::string& name) {
        return static_cast<bool>(resources.findByName<MaterialAsset>(name))
            || AssetLibrary::get().find(AssetType::Material, name) != nullptr;
    };
    std::string name = base;
    for (int n = 2; taken(name); ++n) name = base + " " + std::to_string(n);
    return name;
}

// A material of its own for a new thing, copied from @p base, so tuning one leaves the others.
MaterialHandle copyOf(ResourceManager& resources, MaterialHandle base, const std::string& name) {
    MaterialAsset copy = resources.get(base);
    return resources.add(std::move(copy), uniqueMaterialName(resources, name));
}
} // namespace

MaterialHandle forkMaterial(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity,
    MaterialHandle material
) {
    if (!material) return MaterialHandle{};
    const MaterialHandle fork = copyOf(resources, material, resources.get(material).name() + " copy");
    if (!fork) return fork;

    const Mesh* mesh = scene.isAlive(entity) ? scene.tryGet<Mesh>(entity) : nullptr;
    if (mesh && mesh->material == material) {
        EditScope<Mesh> edit(scene, resources, state, entity, "Duplicate Material");
        edit->material = fork;
    }
    state.openMaterial(fork);
    return fork;
}

MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state) {
    const MaterialHandle base = generateDefaultMaterial(resources);
    if (!base) return MaterialHandle{};

    const MaterialHandle h = copyOf(resources, base, "Material");
    if (!h) return MaterialHandle{};

    state.markSceneDirty();
    return h;
}

TerrainHandle createNewTerrain(ResourceManager& resources) {
    // A path neither a loaded terrain nor a file on disk holds.
    std::string name;
    for (int n = 1;; ++n) {
        name = "assets/terrain/terrain" + (n == 1 ? std::string{} : "_" + std::to_string(n)) + ".terrain";
        std::error_code ec;
        const bool onDisk = std::filesystem::exists(ProjectPaths::resolveProjectPath(name), ec);
        if (!resources.findByName<TerrainAsset>(name) && !onDisk) break;
    }
    TerrainAsset terrain;
    const size_t vertices = static_cast<size_t>(terrain.resolution) * terrain.resolution;
    terrain.heights.assign(vertices, 0.0f);
    terrain.weights.assign(vertices * 4, 0);
    for (size_t v = 0; v < vertices; ++v) terrain.weights[v * 4] = 255;
    terrain.layers[0] = generateDefaultMaterial(resources);
    terrain.sourceJson() = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, name}};
    return resources.add(std::move(terrain), name);
}

namespace {
// Metres across Create > Sea's surface: open water past most of what a scene builds by it.
constexpr float SEA_SIZE = 200.0f;
// The shortest wave its vertices are spaced for, metres, however short the ocean's: a shorter swell
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

// One part of a control, under @p parent: a named element pinned at @p anchor that lets presses
// through to the control.
EntityId addPart(
    Scene& scene,
    EntityId parent,
    const char* name,
    const glm::vec2& anchor,
    const glm::vec2& size
) {
    const EntityId part = scene.createEntity();
    scene.add(part, makeName(name));
    UIElement at      = UIElement::at(anchor, {0.0f, 0.0f}, size);
    at.blocksPointer  = false;
    scene.add(part, std::move(at));
    HierarchyOperations::setParent(scene, part, parent);
    return part;
}

// A slider wired as UISlider wants it: the button is the track, a fill grows across it and a
// knob stands at the value. Plain colors; the look is the project's.
void buildSlider(Scene& scene, EntityId track) {
    scene.get<UIElement>(track).size = {240.0f, 12.0f};
    UIButton button;
    button.shape.cornerRadius = 6.0f;
    scene.add(track, std::move(button));

    const EntityId fill = addPart(scene, track, "Fill", {0.0f, 0.5f}, {0.0f, 0.0f});
    scene.get<UIElement>(fill).relativeSize = {0.5f, 1.0f};
    UIImage bar{UIButton{}.hoverColor, {}, {}};
    bar.shape.cornerRadius = 6.0f;
    scene.add(fill, std::move(bar));

    const EntityId knob = addPart(scene, track, "Knob", {0.5f, 0.5f}, {20.0f, 20.0f});
    UIImage dot;
    dot.shape.cornerRadius = 10.0f;
    scene.add(knob, std::move(dot));

    UISlider slider;
    slider.value = 0.5f;
    slider.fill  = fill;
    slider.knob  = knob;
    scene.add(track, std::move(slider));
}

// A switch: the button is the body, a knob slides to the end that says the state.
void buildToggle(Scene& scene, EntityId body) {
    scene.get<UIElement>(body).size = {64.0f, 32.0f};
    UIButton button;
    button.shape.cornerRadius = 16.0f;
    scene.add(body, std::move(button));

    const EntityId knob = addPart(scene, body, "Knob", {0.0f, 0.5f}, {24.0f, 24.0f});
    UIImage dot;
    dot.shape.cornerRadius = 12.0f;
    scene.add(knob, std::move(dot));

    UIToggle toggle;
    toggle.onColor = UIButton{}.hoverColor;
    toggle.knob    = knob;
    scene.add(body, std::move(toggle));
}

// A choice: its own text shows the option, between two arrow buttons that step it.
void buildChoice(Scene& scene, EntityId choice) {
    scene.get<UIElement>(choice).size = {240.0f, 36.0f};
    UIText shown;
    shown.pixelSize = 22.0f;
    shown.align     = UIText::Align::Center;
    shown.valign    = UIText::VAlign::Middle;
    scene.add(choice, std::move(shown));

    const auto arrow = [&](const char* name, const char* caption, const glm::vec2& side) {
        const EntityId id = addPart(scene, choice, name, side, {36.0f, 36.0f});
        scene.get<UIElement>(id).blocksPointer = true;
        scene.add(id, UIButton{});
        UIText text;
        text.text      = caption;
        text.pixelSize = 22.0f;
        text.align     = UIText::Align::Center;
        text.valign    = UIText::VAlign::Middle;
        scene.add(id, std::move(text));
        return id;
    };

    UIChoice steps;
    steps.options = {"Low", "Medium", "High"};
    steps.index   = 1;
    steps.back    = arrow("Back", "<", {0.0f, 0.5f});
    steps.next    = arrow("Next", ">", {1.0f, 0.5f});
    steps.text    = choice;
    scene.add(choice, std::move(steps));
}

const char* defaultName(EntityKind k) {
    switch (k) {
        case EntityKind::Empty:            return "Empty";
        case EntityKind::Cube:             return "Cube";
        case EntityKind::Sphere:           return "Sphere";
        case EntityKind::Plane:            return "Plane";
        case EntityKind::Triangle:         return "Triangle";
        case EntityKind::Pyramid:          return "Pyramid";
        case EntityKind::Cone:             return "Cone";
        case EntityKind::PointLight:       return "Point Light";
        case EntityKind::SpotLight:        return "Spot Light";
        case EntityKind::DirectionalLight: return "Directional Light";
        case EntityKind::RectLight:        return "Rect Light";
        case EntityKind::DiskLight:        return "Disk Light";
        case EntityKind::Camera:           return "Camera";
        case EntityKind::ReflectionProbe:  return "Reflection Probe";
        case EntityKind::PlanarReflector:  return "Planar Reflector";
        case EntityKind::IrradianceVolume: return "Irradiance Volume";
        case EntityKind::Decal:            return "Decal";
        case EntityKind::ParticleEmitter:  return "Particle Emitter";
        case EntityKind::AudioSource:      return "Audio Source";
        case EntityKind::AudioListener:    return "Audio Listener";
        case EntityKind::UICanvas:         return "UI Canvas";
        case EntityKind::UIPanel:          return "UI Panel";
        case EntityKind::UIScrollView:     return "UI Scroll View";
        case EntityKind::UIText:           return "UI Text";
        case EntityKind::UIButton:         return "UI Button";
        case EntityKind::UISlider:         return "UI Slider";
        case EntityKind::UIToggle:         return "UI Toggle";
        case EntityKind::UIChoice:         return "UI Choice";
        case EntityKind::Character:        return "Character";
        case EntityKind::StaticBody:       return "Static Body";
        case EntityKind::Terrain:          return "Terrain";
        case EntityKind::Water:            return "Water";
        case EntityKind::Sea:              return "Sea";
    }
    return "Entity";
}
} // namespace

EntityId createEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityKind kind,
    EntityId parent
) {
    if (refusesNewChild(scene, parent)) {
        const char* message = "A prefab instance is built from its prefab - open the prefab to add to it";
        state.pushToast(ToastKind::Warning, message);
        return {};
    }
    const EntityId entity = scene.createEntity();

    const bool isCanvas    = kind == EntityKind::UICanvas;
    const bool isUIElement = kind == EntityKind::UIPanel || kind == EntityKind::UIText
        || kind == EntityKind::UIButton || kind == EntityKind::UIScrollView || kind == EntityKind::UISlider
        || kind == EntityKind::UIToggle || kind == EntityKind::UIChoice;
    if (isCanvas)         scene.add(entity, UICanvas{});
    else if (isUIElement) scene.add(entity, UIElement{});
    else                  scene.add(entity, Transform{});

    scene.add(entity, makeName(defaultName(kind)));

    // Both reuse what the graph already holds under the name they would take.
    auto addMesh = [&](MeshAsset mesh) {
        auto meshHandle = addGeneratedMesh(resources, std::move(mesh));
        auto matHandle  = generateDefaultMaterial(resources);
        scene.add(entity, Mesh{meshHandle, matHandle});
    };

    switch (kind) {
        case EntityKind::Empty:
            break;
        case EntityKind::PointLight:
            scene.add(entity, generateLight(LightType::Point));
            break;
        case EntityKind::SpotLight:
            scene.add(entity, generateLight(LightType::Spot));
            break;
        case EntityKind::DirectionalLight:
            scene.add(entity, generateLight(LightType::Directional));
            break;
        case EntityKind::RectLight:
            scene.add(entity, generateLight(LightType::Rect));
            break;
        case EntityKind::DiskLight:
            scene.add(entity, generateLight(LightType::Disk));
            break;
        case EntityKind::Character: {
            // What a CharacterController needs: a capsule and a body that will not
            // tip over or doze off.
            ColliderPart part;
            part.shape = ColliderShape::Capsule;
            part.radius = 0.3f;
            part.halfHeight = 0.6f;
            part.center = {0.0f, part.halfHeight + part.radius, 0.0f};
            Collider collider;
            collider.parts = { part };
            scene.add(entity, std::move(collider));

            Rigidbody body;
            body.mass = 70.0f;
            body.freezeRotation = true;
            body.canSleep = false;
            scene.add(entity, std::move(body));

            scene.add(entity, CharacterController{});
            break;
        }
        case EntityKind::StaticBody: {
            // A blocker: geometry the world collides with and nothing moves.
            Rigidbody body;
            body.motion = RigidbodyMotion::Static;
            scene.add(entity, std::move(body));
            scene.add(entity, Collider{});
            break;
        }
        case EntityKind::Terrain: {
            addTerrainBody(scene, entity, createNewTerrain(resources));
            // A new terrain is for sculpting: the window with the brushes comes forward.
            state.openTerrain(entity);
            break;
        }
        case EntityKind::Water: {
            // A still pool to stretch or reshape, on a material of its own: its Flow runs it along
            // the mesh's +U, so a long strip of it is a river.
            const MeshHandle plane = addGeneratedMesh(resources, generatePlane(20.0f, 20.0f, 20, 20));
            Mesh             surface{plane, copyOf(resources, generateWaterMaterial(resources), "Water")};
            surface.castShadows = false;
            surface.movable     = false;
            scene.add(entity, surface);
            break;
        }
        case EntityKind::Sea: {
            // The waves (Ocean) and a surface dense enough to carry them, on a material of its own;
            // a scene with a sea already gains only the surface, where that sea lies, as one sea
            // moves all.
            // Vertices closer than half the shortest wave, as the swell needs to be drawn whole.
            const EntityId existing = findOcean(scene);
            const Ocean*   waves    = existing ? scene.tryGet<Ocean>(existing) : nullptr;
            const float    shortest = waves ? waves->minWavelength : Ocean{}.minWavelength;
            const float    spacing  = 0.5f * std::max(shortest, MIN_SEA_WAVE);
            const int      across   = static_cast<int>(std::ceil(SEA_SIZE / spacing));
            MeshAsset        sheet  = generatePlane(SEA_SIZE, SEA_SIZE, across, across);
            const MeshHandle plane  = addGeneratedMesh(resources, std::move(sheet));
            Mesh             surface{plane, copyOf(resources, generateSeaMaterial(resources), "Sea")};
            surface.castShadows = false;
            surface.movable     = false;
            scene.add(entity, surface);
            // Laid where the ocean lies once it is parented, below.
            if (!existing) scene.add(entity, Ocean{});
            break;
        }
        case EntityKind::Cube:     addMesh(generateCube());     break;
        case EntityKind::Sphere:   addMesh(generateSphere());   break;
        case EntityKind::Plane:    addMesh(generatePlane());    break;
        case EntityKind::Triangle: addMesh(generateTriangle()); break;
        case EntityKind::Pyramid:  addMesh(generatePyramid());  break;
        case EntityKind::Cone:     addMesh(generateCone());     break;
        case EntityKind::Camera: {
            Camera cam;
            // Inactive unless there is no active camera, so it cannot take the game's view.
            cam.active = !findActiveCamera(scene);
            scene.add(entity, cam);
            break;
        }
        case EntityKind::ReflectionProbe:
            scene.add(entity, ReflectionProbe{});
            break;
        case EntityKind::PlanarReflector:
            scene.add(entity, PlanarReflector{});
            break;
        case EntityKind::IrradianceVolume:
            scene.add(entity, IrradianceVolume{});
            break;
        case EntityKind::Decal:
            scene.add(entity, Decal{});
            break;
        case EntityKind::ParticleEmitter:
            scene.add(entity, ParticleEmitter{});
            break;
        case EntityKind::AudioSource:
            scene.add(entity, AudioSource{});
            break;
        case EntityKind::AudioListener:
            scene.add(entity, AudioListener{});
            break;
        case EntityKind::UICanvas:                          break;  // UICanvas added above
        case EntityKind::UIPanel:  scene.add(entity, UIImage{});  break;
        case EntityKind::UIScrollView:
            // A backing panel too: a scroll view with nothing drawn is an invisible hole.
            scene.add(entity, UIImage{});
            scene.add(entity, UIScroll{});
            break;
        case EntityKind::UIText:   scene.add(entity, UIText{});   break;
        case EntityKind::UIButton: scene.add(entity, UIButton{}); break;
        case EntityKind::UISlider: buildSlider(scene, entity); break;
        case EntityKind::UIToggle: buildToggle(scene, entity); break;
        case EntityKind::UIChoice: buildChoice(scene, entity); break;
    }

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
    uint32_t parentSlot = 0;
    if (parent) {
        HierarchyOperations::setParent(scene, entity, parent);
        parentSlot = parent.slot();
    } else if (isUIElement && state.selectedEntity && scene.isAlive(state.selectedEntity)
        && (scene.has<UICanvas>(state.selectedEntity) || scene.has<UIElement>(state.selectedEntity))) {
        // Unless that belongs to a prefab instance, whose interior is never
        // written: the element would be gone after the next load.
        if (refusesNewChild(scene, state.selectedEntity)) {
            const char* message =
                "A prefab instance is built from its prefab - the new element is "
                "left outside it, and needs a canvas the scene owns";
            state.pushToast(ToastKind::Warning, message);
        } else {
            HierarchyOperations::setParent(scene, entity, state.selectedEntity);
            parentSlot = state.selectedEntity.slot();
        }
    }

    // Another surface for the scene's sea lies where that sea does, in its parent's frame.
    const EntityId ocean = kind == EntityKind::Sea ? findOcean(scene) : EntityId{};
    const Transform* oceanAt = ocean && ocean != entity ? scene.tryGet<Transform>(ocean) : nullptr;
    if (oceanAt) {
        glm::vec3 local = resolvedWorldPosition(scene, ocean, *oceanAt);
        const Hierarchy* link = scene.tryGet<Hierarchy>(entity);
        if (const Transform* above = link && link->parent ? scene.tryGet<Transform>(link->parent) : nullptr) {
            const glm::mat4 toParent = glm::inverse(resolvedWorldMatrix(scene, link->parent, *above));
            local = glm::vec3(toParent * glm::vec4(local, 1.0f));
        }
        scene.get<Transform>(entity).position = local;
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
    // A terrain is ground for the whole scene, centred on its entity: at the origin, as Create puts one.
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

namespace {
constexpr float DUPLICATE_OFFSET_X = 1.0f;

// A copy and the step that removes it, together because the batch path puts the
// step in its composite.
struct Duplicate {
    EntityId                 entity;
    std::unique_ptr<Command> step;
};

/**
 * @brief Which nodes of @p snap belong to a prefab instance nested inside it.
 *
 * A PrefabEntity uid inherited from the instance the original sat in must go, or
 * that instance's overrides would patch the clone; one under an instance inside
 * the copy stays, since that instance's overrides address it. Relies on nodes
 * listing each parent before its children.
 *
 * @param snap The subtree being copied, root first.
 * @return One flag per node, true for a node under a nested instance root.
 */
std::vector<bool> nodesInsideNestedInstance(const SubtreeSnapshot& snap) {
    std::vector<bool> nested(snap.nodes.size(), false);
    for (size_t i = 1; i < snap.nodes.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (snap.nodes[j].snap.slotIndex != snap.nodes[i].parentSlot) continue;
            nested[i] = nested[j] || snap.nodes[j].snap.prefabInstance.has_value();
            break;
        }
    }
    return nested;
}

// Where @p id stands in the world: the pose it keeps when copied out from under its parent.
Transform worldPoseOf(const Scene& scene, EntityId id) {
    if (!scene.has<Transform>(id)) return Transform{};
    return Transform::fromModelMatrix(HierarchyOperations::computeWorldMatrix(scene, id));
}

// What duplicateEntity says a copy of a subtree is: nothing running, and no prefab's but a nested one's.
void readyCopy(SubtreeSnapshot& snap) {
    const std::vector<bool> nested = nodesInsideNestedInstance(snap);
    for (size_t i = 0; i < snap.nodes.size(); ++i) {
        EntitySnapshot& node = snap.nodes[i].snap;
        if (node.camera)    node.camera->active = false;
        if (node.animation) node.animation->playing = false;
        // Its bones are cloned and re-stamped by apply, so it can still be thrown.
        if (node.ragdoll)   node.ragdoll->active = false;
        if (!nested[i])     node.prefabEntity.reset();
    }
}

// An instance of @p instance's prefab at @p at, under @p parent (the pose is local to it).
Duplicate placeInstance(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    const PrefabInstance& instance,
    const Transform& at,
    EntityId parent,
    const char* label
) {
    const EntityId copy = scene.createEntity();
    scene.add(copy, Transform{at});
    scene.add(copy, PrefabInstance{instance});
    if (!Prefab::instantiateInto(scene, resources, instance.source, copy, instance.overrides)) {
        // The subtree: a build that stopped partway has parented what it made.
        HierarchyOperations::destroyHierarchy(scene, copy);
        const std::string name = std::filesystem::path(instance.source).filename().string();
        state.pushToast(ToastKind::Error, "Could not build the instance of '" + name + "'");
        return {};
    }
    if (parent) HierarchyOperations::setParent(scene, copy, parent);
    const uint32_t under = parent.slot();
    return {copy, std::make_unique<PlacePrefabCommand>(resources, instance, copy, at, label, under)};
}

// duplicateEntity documents what a copy is.
Duplicate duplicateOne(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId source,
    bool inPlace
) {
    const float offset = inPlace ? 0.0f : DUPLICATE_OFFSET_X;
    // Beside the source, under its parent, since the pose is local to it; out of an instance's
    // interior, at the root where the source stands in the world (copyParent).
    const Hierarchy* node     = scene.tryGet<Hierarchy>(source);
    const EntityId   wasUnder = node ? node->parent : EntityId{};
    const EntityId   parent   = copyParent(scene, wasUnder);
    const bool       toRoot   = parent != wasUnder;
    if (const PrefabInstance* held = scene.tryGet<PrefabInstance>(source)) {
        const Transform* from = scene.tryGet<Transform>(source);
        Transform at = toRoot ? worldPoseOf(scene, source) : from ? *from : Transform{};
        at.position.x += offset;
        const PrefabInstance instance = *held;
        return placeInstance(scene, resources, state, instance, at, parent, "Duplicate Entity");
    }

    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, source);
    if (snap.nodes.empty()) return {};

    // The root alone: its descendants are placed relative to it.
    std::optional<Transform>& rootPose = snap.nodes.front().snap.transform;
    if (toRoot) {
        snap.rootParentSlot = 0;
        snap.rootNextSlot   = 0;
        if (rootPose) rootPose = worldPoseOf(scene, source);
    }
    if (rootPose) rootPose->position.x += offset;
    readyCopy(snap);

    const EntityId copy = snap.apply(scene, SnapshotSlots::Fresh);
    if (!copy) return {};
    SubtreeSnapshot placed = SubtreeSnapshot::capture(scene, copy);
    return {copy, std::make_unique<CreateSubtreeCommand>(std::move(placed), "Duplicate Entity")};
}
} // namespace

void duplicateEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId source,
    bool inPlace
) {
    Duplicate copy = duplicateOne(scene, resources, state, source, inPlace);
    if (!copy.entity) return;

    state.pushStep(std::move(copy.step));
    state.selectEntity(copy.entity);
}

void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state, bool inPlace) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) duplicateEntity(scene, resources, state, state.selectedEntity, inPlace);
        return;
    }

    const std::vector<EntityId> sources = state.selection;
    const EntityId              active  = state.selectedEntity;
    EntityId                    activeCopy{};
    auto batch = std::make_unique<CompositeCommand>("Duplicate Selection");
    std::vector<EntityId> clones;
    clones.reserve(sources.size());
    for (EntityId src : sources) {
        if (!scene.isAlive(src) || hasSelectedAncestor(scene, state, src)) continue;
        Duplicate copy = duplicateOne(scene, resources, state, src, inPlace);
        if (!copy.entity) continue;
        if (src == active) activeCopy = copy.entity;
        batch->add(std::move(copy.step));
        clones.push_back(copy.entity);
    }
    if (clones.empty()) return;

    state.pushStep(std::move(batch));

    // The active copy is the active original's, so a gizmo held on it stays on it.
    state.selectEntity(clones.front());
    for (size_t i = 1; i < clones.size(); ++i) state.addToSelection(clones[i]);
    state.makeActive(activeCopy ? activeCopy : clones.front());
}

void addScript(Scene& scene, EditorState& state, EntityId id, const char* title, const char* label) {
    scene.add(id, ScriptComponent{});
    const std::string added = ScriptEditCommand::capture(scene, id);
    state.pushStep(std::make_unique<ScriptEditCommand>(id, std::string{}, added, label));
    PrefabOverrides::warnComponentIsPrefabs(scene, state, id, title, "is not stored in the scene");
}

bool hasSelectedAncestor(const Scene& scene, const EditorState& state, EntityId id) {
    bool found = false;
    HierarchyOperations::forSelfAndAncestors(scene, id, [&](EntityId at) {
        found = at != id && state.isSelected(at);
        return !found;
    });
    return found;
}

namespace {
// An instance's interior comes back on every load, so this delete is undone by
// the next unless the instance is written back - a warning, not a refusal.
void warnDeleteInsideInstance(const Scene& scene, EditorState& state, EntityId entity) {
    if (!Prefab::isInsideInstance(scene, entity)) return;
    const char* message =
        "This entity belongs to a prefab - it comes back on the next "
        "load unless the instance root is saved as a prefab";
    state.pushToast(ToastKind::Warning, message);
}
} // namespace

void deleteSelection(Scene& scene, EditorState& state) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) deleteEntity(scene, state, state.selectedEntity);
        return;
    }

    // Its roots, while it is still the selection: the deselect below empties it.
    std::vector<EntityId> roots;
    for (EntityId id : state.selection) {
        if (scene.isAlive(id) && !hasSelectedAncestor(scene, state, id)) roots.push_back(id);
    }

    const EntityId priorSel = state.selectedEntity;
    state.deselect();

    auto batch = std::make_unique<CompositeCommand>("Delete Selection");
    for (EntityId id : roots) {
        warnDeleteInsideInstance(scene, state, id);
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        HierarchyOperations::destroyHierarchy(scene, id);
        batch->add(std::make_unique<DestroySubtreeCommand>(std::move(snap), priorSel, "Delete Entity"));
    }
    if (batch->empty()) return;

    state.pushStep(std::move(batch));
}

void selectAll(const Scene& scene, EditorState& state) {
    const EntityId active = state.selectedEntity;
    state.clearSelection();
    scene.forEachEntity([&](EntityId id) { state.addToSelection(id); });
    if (scene.isAlive(active)) state.makeActive(active);
}

void copySelection(const Scene& scene, const ResourceManager& resources, EditorState& state) {
    // The copies, each a root at its world pose, written as a scene is.
    std::vector<SubtreeSnapshot> trees;
    std::vector<EntityId>        treeParents;
    std::vector<EntityId>        instances;
    for (EntityId id : state.selection) {
        if (!scene.isAlive(id) || hasSelectedAncestor(scene, state, id)) continue;
        if (scene.has<PrefabInstance>(id)) {
            instances.push_back(id);
            continue;
        }
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        if (snap.nodes.empty()) continue;
        readyCopy(snap);
        if (snap.nodes.front().snap.transform) snap.nodes.front().snap.transform = worldPoseOf(scene, id);
        trees.push_back(std::move(snap));
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        treeParents.push_back(node ? node->parent : EntityId{});
    }

    Scene taken;
    auto clip   = std::make_shared<EntityClipboard>();
    clip->world = scene.epoch();
    // Together, so what one copy names of another is kept between them.
    const std::vector<EntityId> roots = SubtreeSnapshot::applyTogether(taken, trees);
    for (size_t i = 0; i < roots.size(); ++i) {
        if (roots[i]) clip->parents.emplace_back(roots[i].slot(), treeParents[i]);
    }
    for (EntityId id : instances) {
        // A reference, as a scene stores one; the paste builds it from its file.
        const EntityId root = taken.createEntity();
        taken.add(root, Transform{worldPoseOf(scene, id)});
        taken.add(root, PrefabInstance{scene.get<PrefabInstance>(id)});
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        clip->parents.emplace_back(root.slot(), node ? node->parent : EntityId{});
    }
    if (clip->parents.empty()) return;
    clip->scene = SceneSerializer::saveToString(taken, resources);
    state.entityClipboard = std::move(clip);
}

void cutSelection(Scene& scene, const ResourceManager& resources, EditorState& state) {
    copySelection(scene, resources, state);
    deleteSelection(scene, state);
}

void carryClipboard(EditorState& state, uint64_t from, const Scene& scene) {
    const std::shared_ptr<const EntityClipboard>& held = state.entityClipboard;
    if (!held || held->world != from) return;
    auto carried   = std::make_shared<EntityClipboard>(*held);
    carried->world = scene.epoch();
    for (auto& [slot, parent] : carried->parents) {
        if (parent) parent = scene.entityAt(parent.slot());
    }
    state.entityClipboard = std::move(carried);
}

void paste(Scene& scene, ResourceManager& resources, EditorState& state) {
    // Its own hold: nothing below may drop it mid-paste.
    const std::shared_ptr<const EntityClipboard> clip = state.entityClipboard;
    if (!clip || clip->parents.empty()) return;
    Scene taken;
    if (!SceneSerializer::loadFromString(clip->scene, taken, resources)) {
        state.pushToast(ToastKind::Error, "Could not paste");
        return;
    }

    // Where each lands: under the same parent while copyParent keeps it, else in the world.
    const bool sameWorld = clip->world == scene.epoch();
    struct Landing {
        EntityId  copy;    ///< In `taken`.
        EntityId  parent;  ///< In the scene; null for the root.
        Transform at;      ///< Its pose under that parent.
    };
    std::vector<Landing> landings;
    for (const auto& [slot, wasUnder] : clip->parents) {
        const EntityId copy = taken.entityAt(slot);
        if (!taken.isAlive(copy)) continue;
        const EntityId   parent = sameWorld ? copyParent(scene, wasUnder) : EntityId{};
        const Transform* pose   = taken.tryGet<Transform>(copy);
        Transform at = pose ? *pose : Transform{};
        if (parent && scene.has<Transform>(parent)) {
            const glm::mat4 under = HierarchyOperations::computeWorldMatrix(scene, parent);
            at = Transform::fromModelMatrix(glm::inverse(under) * Transform::computeModelMatrix(at));
        }
        landings.push_back({copy, parent, at});
    }

    auto batch = std::make_unique<CompositeCommand>("Paste");
    std::vector<EntityId> pasted;
    const auto keep = [&](Duplicate placed) {
        if (!placed.entity) return;
        batch->add(std::move(placed.step));
        pasted.push_back(placed.entity);
    };
    // The subtrees together, so what one names of another is kept between their copies.
    std::vector<SubtreeSnapshot> trees;
    std::vector<const Landing*>  treeLandings;
    for (const Landing& landing : landings) {
        if (const PrefabInstance* held = taken.tryGet<PrefabInstance>(landing.copy)) {
            const PrefabInstance instance = *held;
            const EntityId under = landing.parent;
            keep(placeInstance(scene, resources, state, instance, landing.at, under, "Paste Entity"));
            continue;
        }
        SubtreeSnapshot snap = SubtreeSnapshot::capture(taken, landing.copy);
        if (snap.nodes.empty()) continue;
        if (snap.nodes.front().snap.transform) snap.nodes.front().snap.transform = landing.at;
        trees.push_back(std::move(snap));
        treeLandings.push_back(&landing);
    }
    const std::vector<EntityId> made = SubtreeSnapshot::applyTogether(scene, trees);
    for (size_t i = 0; i < made.size(); ++i) {
        if (!made[i]) continue;
        const EntityId parent = treeLandings[i]->parent;
        if (parent) HierarchyOperations::setParent(scene, made[i], parent);
        SubtreeSnapshot placed = SubtreeSnapshot::capture(scene, made[i]);
        keep({made[i], std::make_unique<CreateSubtreeCommand>(std::move(placed), "Paste Entity")});
    }
    if (pasted.empty()) return;

    state.pushStep(std::move(batch));
    state.selectEntity(pasted.front());
    for (size_t i = 1; i < pasted.size(); ++i) state.addToSelection(pasted[i]);
}

namespace {
// A name as a filename: only safe characters, and a fallback over an unopenable one.
std::string prefabStem(const Scene& scene, EntityId entity) {
    const Name* named = scene.tryGet<Name>(entity);
    std::string stem = named ? named->value : "";
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return stem.empty() ? "prefab" : stem;
}

// First prefab file name not already on disk: `base`, then "base 2", ...
std::string uniquePrefabStem(const std::string& base) {
    std::error_code ec;
    std::string stem = base;
    for (int n = 2; std::filesystem::exists(ProjectPaths::prefabs() / (stem + PREFAB_EXTENSION), ec); ++n) {
        stem = base + " " + std::to_string(n);
    }
    return stem;
}
} // namespace

std::vector<std::string> listPrefabs() {
    std::vector<std::string> prefabs;
    std::error_code          ec;
    // Stepped with the error code: a range-for's covers the constructor alone.
    std::filesystem::recursive_directory_iterator walk(ProjectPaths::prefabs(), ec), end;
    for (; !ec && walk != end; walk.increment(ec)) {
        // Its own code, so one entry that cannot be read is skipped rather than ending the walk.
        std::error_code entry;
        if (!walk->is_regular_file(entry) || walk->path().extension() != PREFAB_EXTENSION) continue;
        prefabs.push_back(ProjectPaths::toProjectRelative(walk->path().string()));
    }
    std::sort(prefabs.begin(), prefabs.end());
    return prefabs;
}

void unpackPrefab(Scene& scene, EditorState& state, EntityId root) {
    if (!scene.has<PrefabInstance>(root)) return;
    SubtreeSnapshot before = SubtreeSnapshot::capture(scene, root);
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, root)) scene.remove<PrefabEntity>(id);
    scene.remove<PrefabInstance>(root);

    SubtreeSnapshot after = SubtreeSnapshot::capture(scene, root);
    auto step = std::make_unique<SubtreeReplaceCommand>(std::move(before), std::move(after), "Unpack Prefab");
    state.pushStep(std::move(step));
}

bool saveAsPrefab(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity) {
    if (!scene.isAlive(entity)) return false;

    // Prefab::save refuses this too; here the instance root can be named.
    const EntityId owner = Prefab::instanceRootOf(scene, entity);
    if (owner && owner != entity) {
        char rootName[64];
        getEntityDisplayName(scene, owner, rootName, sizeof(rootName));
        const std::string message = std::string("This entity belongs to a prefab - Save as Prefab on '")
            + rootName + "', the instance root, writes it with the rest";
        state.pushToast(ToastKind::Warning, message);
        return false;
    }

    // An instance saves over its own source, anything else takes a free name.
    // Project-relative: the instance stores it, and scenes move between machines.
    const PrefabInstance* instance = scene.tryGet<PrefabInstance>(entity);
    const bool editsItsOwn = instance && !instance->source.empty();
    std::string path = editsItsOwn ? instance->source : std::string{};
    if (!editsItsOwn) {
        const std::string file = uniquePrefabStem(prefabStem(scene, entity)) + PREFAB_EXTENSION;
        path = ProjectPaths::toProjectRelative((ProjectPaths::prefabs() / file).string());
    }

    // As a scene's save does, so a later session resolves what the prefab names.
    const AssetWrites writes = writeAssets(resources);

    const std::string shown = std::filesystem::path(path).stem().string();
    if (!Prefab::save(scene, entity, path, resources)) {
        state.pushToast(ToastKind::Error, "Could not save prefab '" + shown + "'");
        return false;
    }

    // The walk Prefab::save wrote with, so no undo step addresses a component the
    // scene stopped storing; see docs/reference/editor.md, "Save as Prefab".
    std::vector<uint32_t> subtreeSlots;
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, entity)) {
        subtreeSlots.push_back(id.slot());
    }
    state.commands.forget(subtreeSlots);
    state.markSceneDirty();
    if (!writes.terrains) {
        state.pushToast(ToastKind::Error, "Saved prefab '" + shown + "', but a terrain could not be written");
    }
    if (!writes.cooked) {
        state.pushToast(ToastKind::Error, "Saved prefab '" + shown + "', but some assets did not cook");
    }
    if (writes.terrains && writes.cooked) state.pushToast(ToastKind::Info, "Saved prefab '" + shown + "'");
    return true;
}

EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& path) {
    Transform at;
    at.position = ViewPlacement::placeInView(scene, resources, state.viewAim, 0.0f);
    const EntityId root = Prefab::instantiate(scene, resources, path, at);
    if (!root) {
        const std::string name = std::filesystem::path(path).filename().string();
        state.pushToast(ToastKind::Error, "Could not place prefab '" + name + "'");
        return {};
    }

    state.pushStep(
        std::make_unique<PlacePrefabCommand>(
            resources,
            scene.get<PrefabInstance>(root),
            root,
            at,
            "Place Prefab"
        )
    );
    state.selectEntity(root);
    return root;
}

std::vector<std::string> shippedEffects() {
    return ContentPack::prefabsIn(ContentPack::shipped("effects"), "effects");
}

EntityId placeEffect(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& file) {
    const std::filesystem::path pack = ContentPack::shipped("effects");
    std::string error;
    const int copied = ContentPack::bringInto(pack, ProjectPaths::projectRoot(), error);
    if (copied < 0) {
        state.pushToast(ToastKind::Error, "Could not bring the engine's effects in: " + error);
        return {};
    }
    if (copied > 0) {
        state.pushToast(
            ToastKind::Info,
            "Brought the engine's effects into the project (" + std::to_string(copied) + " files)"
        );
    }
    return placePrefab(scene, resources, state, "prefabs/effects/" + file);
}

void deleteEntity(Scene& scene, EditorState& state, EntityId entity) {
    warnDeleteInsideInstance(scene, state, entity);

    const EntityId priorSel = state.selectedEntity;
    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, entity);
    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    const bool hadChildren = node && node->firstChild;
    const char* label = hadChildren ? "Delete Subtree" : "Delete Entity";

    HierarchyOperations::destroyHierarchy(scene, entity);
    state.pushStep(std::make_unique<DestroySubtreeCommand>(std::move(snap), priorSel, label));
}

namespace {
/// The world matrix @p id's local Transform is relative to: its parent's, or the identity.
glm::mat4 parentWorldOf(const Scene& scene, EntityId id) {
    const Hierarchy* link = scene.tryGet<Hierarchy>(id);
    if (!link || !scene.isAlive(link->parent)) return glm::mat4(1.0f);
    return HierarchyOperations::computeWorldMatrix(scene, link->parent);
}
} // namespace

void alignWithView(Scene& scene, ResourceManager& resources, EditorState& state) {
    const EntityId id = state.selectedEntity;
    if (!scene.isAlive(id) || !scene.has<Transform>(id) || !state.viewAim.known) return;

    const glm::mat4 parent = parentWorldOf(scene, id);
    EditScope<Transform> pose(scene, resources, state, id, "Align With View");
    pose->position = glm::vec3(glm::inverse(parent) * glm::vec4(state.viewAim.ray.origin, 1.0f));
    pose->rotation = glm::normalize(glm::inverse(Math::worldRotationOf(parent)) * state.viewAim.rotation);
}

void dropToGround(Scene& scene, ResourceManager& resources, EditorState& state) {
    // Each selected entity a selected ancestor does not carry, lowest first, so one
    // stacked on another lands on it where it has fallen to.
    std::vector<std::pair<float, EntityId>> falling;
    for (const EntityId id : state.selection) {
        if (!scene.isAlive(id) || !scene.has<Transform>(id)) continue;
        if (hasSelectedAncestor(scene, state, id)) continue;
        falling.emplace_back(HierarchyOperations::computeWorldMatrix(scene, id)[3].y, id);
    }
    std::sort(falling.begin(), falling.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    auto batch = std::make_unique<CompositeCommand>("Drop to Ground");
    size_t floating = 0;
    for (const auto& entry : falling) {
        const EntityId id = entry.second;

        float drop = 0.0f;
        if (!ViewPlacement::dropBelow(scene, resources, id, drop)) {
            ++floating;
            continue;
        }
        if (drop == 0.0f) continue;

        Transform&      local  = scene.get<Transform>(id);
        const Transform before = local;
        const glm::mat4 parent = parentWorldOf(scene, id);
        local.position += glm::vec3(glm::inverse(parent) * glm::vec4(0.0f, -drop, 0.0f, 0.0f));
        batch->add(editStep<Transform>(scene, resources, id, before, local, "Drop to Ground"));
    }
    if (!batch->empty()) state.pushStep(std::move(batch));
    if (floating > 0) state.pushToast(ToastKind::Warning, "Nothing below to drop onto");
}

void undo(Scene& scene, EditorState& state) {
    // An empty history is not an edit: Ctrl+Z on a fresh scene must not mark it unsaved.
    if (!state.commands.canUndo()) return;
    state.commands.undo(scene, state);
    state.markSceneDirty();
}

void redo(Scene& scene, EditorState& state) {
    if (!state.commands.canRedo()) return;
    state.commands.redo(scene, state);
    state.markSceneDirty();
}

void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state, EntityId parent) {
    if (ImGui::BeginMenu(parent ? "Create Child" : "Create")) {
        auto item = [&](EditorIcon icon, const char* label, EntityKind k) {
            if (!iconMenuItem(icon, label)) return;
            const EntityId made = createEntity(scene, resources, state, k, parent);
            if (made) state.selectEntity(made);
        };
        item(EditorIcon::Empty, "Empty", EntityKind::Empty);
        ImGui::Separator();
        item(EditorIcon::Camera,   "Camera",            EntityKind::Camera);
        item(EditorIcon::Probe,    "Reflection Probe",  EntityKind::ReflectionProbe);
        item(EditorIcon::Probe,    "Planar Reflector",  EntityKind::PlanarReflector);
        item(EditorIcon::Volume,   "Irradiance Volume", EntityKind::IrradianceVolume);
        item(EditorIcon::Decal,    "Decal",             EntityKind::Decal);
        item(EditorIcon::Particle, "Particle Emitter",  EntityKind::ParticleEmitter);
        ImGui::Separator();
        item(EditorIcon::Audio,    "Audio Source",      EntityKind::AudioSource);
        item(EditorIcon::Listener, "Audio Listener",    EntityKind::AudioListener);
        ImGui::Separator();
        if (ImGui::BeginMenu("Light")) {
            item(EditorIcon::LightDir,   "Directional", EntityKind::DirectionalLight);
            item(EditorIcon::LightPoint, "Point",       EntityKind::PointLight);
            item(EditorIcon::LightSpot,  "Spot",        EntityKind::SpotLight);
            item(EditorIcon::LightRect,  "Rect",        EntityKind::RectLight);
            item(EditorIcon::LightDisk,  "Disk",        EntityKind::DiskLight);
            ImGui::EndMenu();
        }
        if (!parent && ImGui::BeginMenu("Effect")) {
            for (const std::string& file : shippedEffects()) {
                if (iconMenuItem(EditorIcon::Particle, effectLabel(file).c_str())) {
                    placeEffect(scene, resources, state, file);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Primitive")) {
            item(EditorIcon::Cube,     "Cube",     EntityKind::Cube);
            item(EditorIcon::Sphere,   "Sphere",   EntityKind::Sphere);
            item(EditorIcon::Plane,    "Plane",    EntityKind::Plane);
            item(EditorIcon::Cone,     "Cone",     EntityKind::Cone);
            item(EditorIcon::Pyramid,  "Pyramid",  EntityKind::Pyramid);
            item(EditorIcon::Triangle, "Triangle", EntityKind::Triangle);
            ImGui::EndMenu();
        }
        item(EditorIcon::Terrain, "Terrain", EntityKind::Terrain);
        item(EditorIcon::Plane,   "Water",   EntityKind::Water);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A pool on a material of its own. Set its Flow and stretch it: a river.");
        }
        item(EditorIcon::Plane,   "Sea",     EntityKind::Sea);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The scene's ocean: its waves and a surface for them");
        if (ImGui::BeginMenu("Physics")) {
            item(EditorIcon::Character, "Character",   EntityKind::Character);
            item(EditorIcon::Colliders, "Static Body", EntityKind::StaticBody);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI")) {
            item(EditorIcon::UICanvas, "Canvas", EntityKind::UICanvas);
            item(EditorIcon::UIImage,  "Panel",  EntityKind::UIPanel);
            item(EditorIcon::UIScroll, "Scroll View", EntityKind::UIScrollView);
            item(EditorIcon::UIText,   "Text",   EntityKind::UIText);
            item(EditorIcon::UIButton, "Button", EntityKind::UIButton);
            item(EditorIcon::UISlider, "Slider", EntityKind::UISlider);
            item(EditorIcon::UIToggle, "Toggle", EntityKind::UIToggle);
            item(EditorIcon::UIChoice, "Choice", EntityKind::UIChoice);
            ImGui::EndMenu();
        }
        // The menu closes on click, so the modals live in dialogs EditorSystem owns.
        if (!parent) {
            ImGui::Separator();
            if (iconMenuItem(EditorIcon::Prefab, "Prefab")) state.requestPlacePrefab = true;
            if (iconMenuItem(EditorIcon::Import, "Import Model")) state.requestModelImport = true;
        }
        ImGui::EndMenu();
    }
}

} // namespace EditorActions
} // namespace Vkm::Engine

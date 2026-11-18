#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

#include "base/container/storage_index.h"
#include "ecs/entity.h"
#include "resource/asset_type.h"
#include "ui/style/editor_icons.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

namespace EditorActions {

/**
 * @brief Every built-in entity kind, in the Create menu's order.
 *
 * X(kind, name, label, icon, group, tip, build): the new entity's Name; its line in the Create menu, with
 * its EditorIcon, under its CreateGroup, and its hover text or nullptr; and the function in
 * create_entity.cpp that makes it (the tips are named there too). A scroll view comes with a backing panel:
 * one with nothing drawn is an invisible hole. Add a kind by adding a row; the enum and the menu follow.
 */
#define VKM_ENTITY_KINDS(X)                                                                                                             \
    X(Empty,            "Empty",             "Empty",             Empty,      Top,       nullptr,   made<Transform>)                    \
    X(Camera,           "Camera",            "Camera",            Camera,     Scene,     nullptr,   camera)                             \
    X(ReflectionProbe,  "Reflection Probe",  "Reflection Probe",  Probe,      Scene,     nullptr,   made<Transform, ReflectionProbe>)   \
    X(IrradianceVolume, "Irradiance Volume", "Irradiance Volume", Volume,     Scene,     nullptr,   made<Transform, IrradianceVolume>)  \
    X(Decal,            "Decal",             "Decal",             Decal,      Scene,     nullptr,   made<Transform, Decal>)             \
    X(ParticleEmitter,  "Particle Emitter",  "Particle Emitter",  Particle,   Scene,     nullptr,   made<Transform, ParticleEmitter>)   \
    X(AudioSource,      "Audio Source",      "Audio Source",      Audio,      Audio,     nullptr,   made<Transform, AudioSource>)       \
    X(AudioListener,    "Audio Listener",    "Audio Listener",    Listener,   Audio,     nullptr,   made<Transform, AudioListener>)     \
    X(DirectionalLight, "Directional Light", "Directional",       LightDir,   Light,     nullptr,   light<LightType::Directional>)      \
    X(PointLight,       "Point Light",       "Point",             LightPoint, Light,     nullptr,   light<LightType::Point>)            \
    X(SpotLight,        "Spot Light",        "Spot",              LightSpot,  Light,     nullptr,   light<LightType::Spot>)             \
    X(Cube,             "Cube",              "Cube",              Cube,       Primitive, nullptr,   cube)                               \
    X(Sphere,           "Sphere",            "Sphere",            Sphere,     Primitive, nullptr,   sphere)                             \
    X(Plane,            "Plane",             "Plane",             Plane,      Primitive, nullptr,   plane)                              \
    X(Cone,             "Cone",              "Cone",              Cone,       Primitive, nullptr,   cone)                               \
    X(Pyramid,          "Pyramid",           "Pyramid",           Pyramid,    Primitive, nullptr,   pyramid)                            \
    X(Triangle,         "Triangle",          "Triangle",          Triangle,   Primitive, nullptr,   triangle)                           \
    X(Terrain,          "Terrain",           "Terrain",           Terrain,    Ground,    nullptr,   terrain)                            \
    X(Water,            "Water",             "Water",             Plane,      Ground,    WATER_TIP, water)                              \
    X(Sea,              "Sea",               "Sea",               Plane,      Ground,    SEA_TIP,   sea)                                \
    X(Character,        "Character",         "Character",         Character,  Physics,   nullptr,   character)                          \
    X(StaticBody,       "Static Body",       "Static Body",       Colliders,  Physics,   nullptr,   staticBody)                         \
    X(Trigger,          "Trigger",           "Trigger",           Colliders,  Physics,   SENSE_TIP, trigger)                            \
    X(UICanvas,         "UI Canvas",         "Canvas",            UICanvas,   UI,        nullptr,   made<UICanvas>)                     \
    X(UIPanel,          "UI Panel",          "Panel",             UIImage,    UI,        nullptr,   made<UIElement, UIImage>)           \
    X(UIScrollView,     "UI Scroll View",    "Scroll View",       UIScroll,   UI,        nullptr,   made<UIElement, UIImage, UIScroll>) \
    X(UIText,           "UI Text",           "Text",              UIText,     UI,        nullptr,   made<UIElement, UIText>)            \
    X(UIButton,         "UI Button",         "Button",            UIButton,   UI,        nullptr,   made<UIElement, UIButton>)          \
    X(UISlider,         "UI Slider",         "Slider",            UISlider,   UI,        nullptr,   slider)                             \
    X(UIToggle,         "UI Toggle",         "Toggle",            UIToggle,   UI,        nullptr,   toggle)                             \
    X(UIChoice,         "UI Choice",         "Choice",            UIChoice,   UI,        nullptr,   choice)

/**
 * @brief Built-in entity kinds, in VKM_ENTITY_KINDS's order; entityKinds() holds each one's row.
 */
enum class EntityKind {
#define VKM_ENTITY_KIND_ENUM(kind, ...) kind,
    VKM_ENTITY_KINDS(VKM_ENTITY_KIND_ENUM)
#undef VKM_ENTITY_KIND_ENUM
    Count
};

/**
 * @brief The part of the Create menu a kind is listed in; the menu lays the parts out (CreateMenu::draw).
 */
enum class CreateGroup { Top, Scene, Audio, Light, Primitive, Ground, Physics, UI };

struct CreateKit;

/**
 * @brief One built-in kind: the entity it makes and its line in the Create menu.
 */
struct EntityKindRow {
    EntityKind  kind;
    const char* name;   ///< The new entity's Name.
    const char* label;  ///< Its line in the Create menu, under its group.
    EditorIcon  icon;
    CreateGroup group;
    const char* tip;    ///< The line's hover text, or null.
    EntityId (*build)(CreateKit& kit);  ///< Makes the entity with its components, a control's parts too.
};

/**
 * @brief Every built-in kind's row, in EntityKind's order.
 *
 * @return The rows, indexed by EntityKind.
 */
const std::array<EntityKindRow, static_cast<size_t>(EntityKind::Count)>& entityKinds();

/**
 * @brief Create an entity of @p kind as one undo step.
 *
 * A root goes where the view looks (ViewPlacement::placeInView), a child at its parent's origin. A
 * child inside a prefab instance is refused with a toast: the instance is rebuilt from its prefab.
 *
 * @param scene Receives the entity.
 * @param resources Where the kind's meshes and materials are registered.
 * @param state Takes the step, the dirty flag and any refusal's toast.
 * @param kind Which built-in factory to run.
 * @param parent Puts it under this entity, or {} for the view or the selected canvas.
 * @return The new entity, or null when refused.
 */
EntityId createEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityKind kind,
    EntityId parent = {}
);

/**
 * @brief Whether createFromAsset makes an entity of an asset of @p type.
 *
 * @param type The dragged asset's kind.
 * @return True for a mesh, a sound and a terrain.
 */
bool makesEntity(AssetType type);

/**
 * @brief Make and select an entity of a dragged asset at @p at, as one undo step.
 *
 * A mesh is drawn with the default material and a sound gets an Audio Source; a terrain's ground
 * stands at the origin instead.
 *
 * @param scene Receives the entity.
 * @param resources Holds the asset.
 * @param state Takes the step and the selection.
 * @param type One makesEntity accepts; anything else makes nothing.
 * @param key The asset.
 * @param at World position of its origin.
 * @return The new entity, or null.
 */
EntityId createFromAsset(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    AssetType type,
    StorageIndex key,
    const glm::vec3& at
);

} // namespace EditorActions

} // namespace Vkm::Engine

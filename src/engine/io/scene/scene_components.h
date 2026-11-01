#pragma once

#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/buoyancy.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/effect/decal.h"
#include "ecs/component/render/outline.h"
#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/light/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/light/reflection_probe.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/landscape/water.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/net/net_local.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_stack.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/component/script/script_component.h"

/**
 * @brief Every component the scene format round-trips, one row each.
 *
 * P refers to nothing outside itself. R names assets, so save and load also take
 * the ResourceManager. E names other entities, so save takes an EntityNamer and
 * load an EntityResolver.
 *
 * The key is the format, not the type name: ScriptComponent is stored as "Script".
 * Hierarchy is not a row: SceneSerializer reads it in pass 2, because the parent
 * it names may not exist when the entity is read. The value rows are every row but
 * ScriptComponent, which is move-only and so cannot be held as a copy.
 */
#define VKM_SCENE_COMPONENTS(P, R, E)   \
    VKM_SCENE_VALUE_COMPONENTS(P, R, E) \
    P(ScriptComponent, "Script")

#define VKM_SCENE_VALUE_COMPONENTS(P, R, E)       \
    P(Name,                "Name")                \
    P(Transform,           "Transform")           \
    R(Mesh,                "Mesh")                \
    R(LOD,                 "LOD")                 \
    P(Light,               "Light")               \
    P(Camera,              "Camera")              \
    R(Decal,               "Decal")               \
    P(Outline,             "Outline")             \
    R(ParticleEmitter,     "ParticleEmitter")     \
    P(ReflectionProbe,     "ReflectionProbe")     \
    P(IrradianceVolume,    "IrradianceVolume")    \
    P(Water,               "Water")               \
    R(Terrain,             "Terrain")             \
    P(Rigidbody,           "Rigidbody")           \
    R(Collider,            "Collider")            \
    P(CharacterController, "CharacterController") \
    P(Buoyancy,            "Buoyancy")            \
    E(Joint,               "Joint")               \
    E(Ragdoll,             "Ragdoll")             \
    P(Animation,           "Animation")           \
    R(Animator,            "Animator")            \
    P(BoneSocket,          "BoneSocket")          \
    R(AudioSource,         "AudioSource")         \
    P(AudioListener,       "AudioListener")       \
    P(UICanvas,            "UICanvas")            \
    P(UIElement,           "UIElement")           \
    R(UIImage,             "UIImage")             \
    P(UIText,              "UIText")              \
    P(UIButton,            "UIButton")            \
    P(UIScroll,            "UIScroll")            \
    P(UIStack,             "UIStack")             \
    E(UISlider,            "UISlider")            \
    E(UIToggle,            "UIToggle")            \
    E(UIChoice,            "UIChoice")            \
    P(NetLocal,            "NetLocal")

namespace Vkm::Engine {

/// The key a scene or prefab file stores @p T under: its row's, and Hierarchy's own.
template<typename T>
constexpr const char* sceneKey();

#define VKM_SCENE_KEY_OF(Type, Key) \
    template<>                      \
    constexpr const char* sceneKey<Type>() { return Key; }
VKM_SCENE_COMPONENTS(VKM_SCENE_KEY_OF, VKM_SCENE_KEY_OF, VKM_SCENE_KEY_OF)
VKM_SCENE_KEY_OF(Hierarchy, "Hierarchy")
#undef VKM_SCENE_KEY_OF

} // namespace Vkm::Engine

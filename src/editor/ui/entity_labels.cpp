#include "ui/entity_labels.h"

#include <cstdio>
#include <iterator>

#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/name.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/planar_reflector.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/render/terrain.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/component/ui/ui_text.h"

namespace Vkm::Engine {

namespace {

struct EntityLabel {
    const char* name;
    EditorIcon  icon;
};

// One row per LightType, in order; the viewport marker wears the same glyph.
constexpr EntityLabel LIGHT_LABELS[] = {
    {"Dir Light",   EditorIcon::LightDir},
    {"Point Light", EditorIcon::LightPoint},
    {"Spot Light",  EditorIcon::LightSpot},
    {"Rect Light",  EditorIcon::LightRect},
    {"Disk Light",  EditorIcon::LightDisk},
};
static_assert(
    std::size(LIGHT_LABELS) == static_cast<size_t>(LightType::Count),
    "every light type needs a label and a glyph"
);

EntityLabel lightLabelOf(LightType type) {
    const auto index = static_cast<size_t>(type);
    return index < std::size(LIGHT_LABELS)
        ? LIGHT_LABELS[index]
        : EntityLabel{"Light", EditorIcon::LightPoint};
}

// Order is precedence. A Light answers by its type, and a Mesh carrying an
// Animation reads "Animated Mesh" while keeping the plain mesh glyph.
EntityLabel entityLabelOf(const Scene& scene, EntityId id) {
    // First: being a prefab constrains editing. Only the root holds PrefabInstance;
    // PrefabEntity is on the whole subtree.
    if (scene.has<PrefabInstance>(id)) return {"Prefab", EditorIcon::Prefab};
    if (scene.has<Camera>(id)) return {"Camera", EditorIcon::Camera};
    if (const Light* light = scene.tryGet<Light>(id)) return lightLabelOf(light->type);
    // Before Mesh: an Animator makes it the rig (its meshes are children); a socket likewise.
    if (scene.has<Animator>(id)) return {"Rig", EditorIcon::Anim};
    if (scene.has<BoneSocket>(id)) return {"Socket", EditorIcon::Socket};
    if (scene.has<Terrain>(id)) return {"Terrain", EditorIcon::Terrain};
    if (scene.has<Mesh>(id)) {
        if (scene.has<Animation>(id)) return {"Animated Mesh", EditorIcon::Mesh};
        if (scene.has<LOD>(id))       return {"LOD Mesh",      EditorIcon::Mesh};
        return {"Mesh", EditorIcon::Mesh};
    }
    if (scene.has<Animation>(id))        return {"Animation", EditorIcon::Anim};
    if (scene.has<ReflectionProbe>(id))  return {"Probe",     EditorIcon::Probe};
    if (scene.has<PlanarReflector>(id))  return {"Mirror",    EditorIcon::Probe};
    if (scene.has<IrradianceVolume>(id)) return {"GI Volume", EditorIcon::Volume};
    if (scene.has<Decal>(id))            return {"Decal",     EditorIcon::Decal};
    if (scene.has<ParticleEmitter>(id))  return {"Emitter",   EditorIcon::Particle};
    if (const AudioSource* source = scene.tryGet<AudioSource>(id)) {
        return source->spatial
            ? EntityLabel{"Sound",    EditorIcon::Audio}
            : EntityLabel{"2D Sound", EditorIcon::Audio2D};
    }
    if (scene.has<AudioListener>(id))    return {"Listener",  EditorIcon::Listener};
    if (scene.has<UISlider>(id))         return {"Slider",    EditorIcon::UISlider};
    if (scene.has<UIToggle>(id))         return {"Toggle",    EditorIcon::UIToggle};
    if (scene.has<UIChoice>(id))         return {"Choice",    EditorIcon::UIChoice};
    if (scene.has<UIButton>(id))         return {"Button",    EditorIcon::UIButton};
    if (scene.has<UIText>(id))           return {"Text",      EditorIcon::UIText};
    if (scene.has<UIScroll>(id))         return {"Scroll",    EditorIcon::UIScroll};
    if (scene.has<UIImage>(id))          return {"Panel",     EditorIcon::UIImage};
    if (scene.has<UICanvas>(id))         return {"Canvas",    EditorIcon::UICanvas};
    if (scene.has<UIElement>(id))        return {"Widget",    EditorIcon::UIWidget};
    // Below the visual rows: a crate keeps its mesh glyph; these catch what has no other face.
    if (scene.has<CharacterController>(id)) return {"Character", EditorIcon::Character};
    if (scene.has<Ragdoll>(id))          return {"Ragdoll",   EditorIcon::Ragdoll};
    if (scene.has<Joint>(id))            return {"Jointed Body", EditorIcon::Joint};
    if (scene.has<Collider>(id))         return {"Collider",  EditorIcon::Colliders};
    if (scene.has<Rigidbody>(id))        return {"Body",      EditorIcon::Colliders};
    return {"Entity", EditorIcon::Entity};
}

} // namespace

void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize) {
    const Name* name = scene.tryGet<Name>(id);
    if (name && name->value[0] != '\0') {
        snprintf(buf, bufSize, "%s", name->value);
        return;
    }
    snprintf(buf, bufSize, "%s %u", entityLabelOf(scene, id).name, id.slot());
}

EditorIcon entityIconKind(const Scene& scene, EntityId id) {
    return entityLabelOf(scene, id).icon;
}

EditorIcon lightIcon(LightType type) {
    return lightLabelOf(type).icon;
}

} // namespace Vkm::Engine

#define VKM_LOG_CATEGORY "IO"

#include "io/scene/component_serializer.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "vkm_log/logger.h"

#include "debug/error/engine_error_log.h"
#include "ecs/settings/environment.h"
#include "io/json/reflect_json.h"
#include "base/reflect/enum_names.h"
#include "resource/resource_manager.h"
#include "ecs/component/physics/mesh_collider.h"
namespace Vkm::Engine::ComponentSerializer {

namespace {

using ReflectJson::leafFromJson;
using ReflectJson::leafToJson;

/**
 * @brief The resource manager as ReflectJson's context for a save: a handle is its asset's name.
 */
struct AssetNames {
    const ResourceManager& resources;

    template<typename A>
    nlohmann::json handleToJson(const Handle<A>& handle) const {
        return handle ? resources.get(handle).name() : std::string{};
    }
};

/**
 * @brief The same for a load: a name is its asset's handle, and one no asset answers to is kept
 *        for the next save.
 */
struct AssetLookup {
    static constexpr bool KEEPS_WRONG_FIELDS = false;  ///< A drifted field fails the load, which names its
                                                       ///< entity and component.

    const ResourceManager&      resources;
    std::vector<UnresolvedRef>& unresolved;  ///< Each name that did not resolve, under its field's path.

    // A name that is no string is drift, and throws as a leaf does. An unresolved name leaves the slot
    // empty and goes through reportError, so a host's error sink shows it.
    template<typename A>
    void handleFromJson(const nlohmann::json& j, Handle<A>& handle, const char* field) const {
        const std::string name = j.get<std::string>();
        handle = name.empty() ? Handle<A>{} : resources.findByName<A>(name);
        if (handle || name.empty()) return;
        reportError(
            "Scene",
            std::string(Reflect::enumName(ASSET_TYPE<A>)) + " '" + name + "'",
            "reference left unresolved - the asset is not loaded, so the slot is empty"
        );
        unresolved.push_back({field, name, ASSET_TYPE<A>});
    }
};

} // namespace

template <typename T>
nlohmann::json save(const T& value, const ResourceManager& resources) {
    return ReflectJson::saveFields(value, AssetNames{resources});
}

template <typename T>
void load(
    const nlohmann::json& json,
    T& value,
    const ResourceManager& resources,
    std::vector<UnresolvedRef>& unresolved
) {
    ReflectJson::loadFields(json, value, AssetLookup{resources, unresolved});
}

#define VKM_SERIALIZER_SKIP(Type, Key)
#define VKM_SERIALIZER_R(Type, Key)                                                 \
    template nlohmann::json save<Type>(const Type&, const ResourceManager&);         \
    template void load<Type>(                                                        \
        const nlohmann::json&,                                                       \
        Type&,                                                                       \
        const ResourceManager&,                                                      \
        std::vector<UnresolvedRef>&                                                  \
    );
VKM_SCENE_COMPONENTS(VKM_SERIALIZER_SKIP, VKM_SERIALIZER_R, VKM_SERIALIZER_SKIP)
#undef VKM_SERIALIZER_R
#undef VKM_SERIALIZER_SKIP

void load(const nlohmann::json& j, Environment& env) {
    load<Environment>(j, env);

    // A phase asymmetry of one divides zero by zero in the fog, so it is clamped to what it can
    // draw, with a warning.
    const float g = std::clamp(env.fog.anisotropy, -FogSettings::MAX_ANISOTROPY, FogSettings::MAX_ANISOTROPY);
    if (g != env.fog.anisotropy) {
        LOG_WARNING(
            "Fog anisotropy %g is outside +-%g; using %g",
            env.fog.anisotropy,
            FogSettings::MAX_ANISOTROPY,
            g
        );
        env.fog.anisotropy = g;
    }
}

nlohmann::json save(const Joint& joint, const EntityNamer& name) {
    // The driver carries all but the entity reference, `type` included: it is
    // the one place that says what an unknown enum name means.
    nlohmann::json out = save(joint);
    out["connected"] = name(joint.connected);
    return out;
}
void load(const nlohmann::json& j, Joint& out, const EntityResolver& resolve) {
    load(j, out);
    out.connected = resolve(detail::numberAt(j, "connected", 0u));
}

nlohmann::json save(const Ragdoll& r, const EntityNamer& name) {
    nlohmann::json out = save(r);

    nlohmann::json bones = nlohmann::json::array();
    for (const RagdollBone& bone : r.bones) {
        nlohmann::json entry;
        entry["bone"] = bone.bone;
        entry["body"] = name(bone.body);
        nlohmann::json bodyFromBone = nlohmann::json::array();
        const float* m = glm::value_ptr(bone.bodyFromBone);
        for (int i = 0; i < 16; ++i) bodyFromBone.push_back(m[i]);
        entry["bodyFromBone"] = std::move(bodyFromBone);
        bones.push_back(std::move(entry));
    }
    out["bones"] = std::move(bones);
    out["root"]  = name(r.root);
    return out;
}
void load(const nlohmann::json& j, Ragdoll& r, const EntityResolver& resolve) {
    load(j, r);

    r.root = resolve(detail::numberAt(j, "root", 0u));

    r.bones.clear();
    const auto it = j.find("bones");
    if (it == j.end()) return;

    // Read as strictly as the driver reads a field: bones that are no list, a bone that is no object
    // or a matrix that is not sixteen numbers is drift, and throws.
    const nlohmann::json::array_t& bones = it->get_ref<const nlohmann::json::array_t&>();
    r.bones.reserve(bones.size());
    for (const nlohmann::json& entry : bones) {
        (void)entry.get_ref<const nlohmann::json::object_t&>();
        RagdollBone bone;
        bone.bone = detail::numberAt(entry, "bone", -1);
        bone.body = resolve(detail::numberAt(entry, "body", 0u));
        if (const auto matrix = entry.find("bodyFromBone"); matrix != entry.end()) {
            const nlohmann::json::array_t& values = matrix->get_ref<const nlohmann::json::array_t&>();
            if (values.size() != 16) {
                throw nlohmann::json::type_error::create(302, "expected an array of 16 numbers", &*matrix);
            }
            float* m = glm::value_ptr(bone.bodyFromBone);
            for (int i = 0; i < 16; ++i) m[i] = detail::jsonToNumber<float>(values[i]);
        }
        r.bones.push_back(bone);
    }
}

void load(
    const nlohmann::json& j,
    Collider& c,
    const ResourceManager& resources,
    std::vector<UnresolvedRef>& unresolved
) {
    load<Collider>(j, c, resources, unresolved);
    // Built here rather than by the first tick: a query is not a tick, and in
    // the editor nothing ticks.
    syncMeshCollider(c, resources);
}

nlohmann::json save(const UISlider& slider, const EntityNamer& name) {
    nlohmann::json out = save(slider);
    out["fill"] = name(slider.fill);
    out["knob"] = name(slider.knob);
    return out;
}
void load(const nlohmann::json& j, UISlider& slider, const EntityResolver& resolve) {
    load(j, slider);
    slider.fill = resolve(detail::numberAt(j, "fill", 0u));
    slider.knob = resolve(detail::numberAt(j, "knob", 0u));
}

nlohmann::json save(const UIToggle& toggle, const EntityNamer& name) {
    nlohmann::json out = save(toggle);
    out["knob"] = name(toggle.knob);
    return out;
}
void load(const nlohmann::json& j, UIToggle& toggle, const EntityResolver& resolve) {
    load(j, toggle);
    toggle.knob = resolve(detail::numberAt(j, "knob", 0u));
}

nlohmann::json save(const UIChoice& choice, const EntityNamer& name) {
    nlohmann::json out = save(choice);
    out["back"] = name(choice.back);
    out["next"] = name(choice.next);
    out["text"] = name(choice.text);
    return out;
}
void load(const nlohmann::json& j, UIChoice& choice, const EntityResolver& resolve) {
    load(j, choice);
    choice.back = resolve(detail::numberAt(j, "back", 0u));
    choice.next = resolve(detail::numberAt(j, "next", 0u));
    choice.text = resolve(detail::numberAt(j, "text", 0u));
}

nlohmann::json save(const Hierarchy& h) {
    return nlohmann::json{{"parent", h.parent.slot()}};
}
uint32_t loadParentIndex(const nlohmann::json& j) {
    return detail::numberAt(j, "parent", std::numeric_limits<uint32_t>::max());
}

namespace {

template<typename T>
nlohmann::json saveTrack(const AnimationTrack<T>& track) {
    nlohmann::json keyframes = nlohmann::json::array();
    const auto& times  = track.getTimes();
    const auto& values = track.getValues();
    for (size_t i = 0; i < times.size(); ++i) {
        keyframes.push_back({{"t", times[i]}, {"v", leafToJson(values[i])}});
    }
    return {
        {"easing",    leafToJson(track.getEasing())},
        {"keyframes", std::move(keyframes)},
    };
}

template<typename T>
void loadTrack(const nlohmann::json& j, AnimationTrack<T>& track) {
    // A track that is no object, or keys that are no list, is drift, and throws.
    (void)j.get_ref<const nlohmann::json::object_t&>();
    track.clear();
    if (j.contains("easing")) {
        Math::Easing easing = Math::Easing::Linear;
        leafFromJson(j["easing"], easing);
        track.setEasing(easing);
    }
    if (const auto keys = j.find("keyframes"); keys != j.end()) {
        for (const auto& kf : keys->get_ref<const nlohmann::json::array_t&>()) {
            // at(), not operator[]: const operator[] on a missing key only asserts,
            // then reads the map's end node in release. The scene loader catches the throw.
            T value{};
            leafFromJson(kf.at("v"), value);
            track.addKeyframe(detail::numberAt(kf, "t", 0.0f), value);
        }
    }
}

} // namespace

nlohmann::json save(const Animation& a) {
    return {
        {"position", saveTrack(a.positionTrack)},
        {"rotation", saveTrack(a.rotationTrack)},
        {"scale",    saveTrack(a.scaleTrack)},
        {"length",      a.length},
        {"speed",       a.speed},
        {"playOnStart", a.playOnStart},
        {"looping",     a.looping},
    };
}

void load(const nlohmann::json& j, Animation& a) {
    if (j.contains("position")) loadTrack(j["position"], a.positionTrack);
    if (j.contains("rotation")) loadTrack(j["rotation"], a.rotationTrack);
    if (j.contains("scale"))    loadTrack(j["scale"],    a.scaleTrack);
    a.length      = detail::numberAt(j, "length", a.length);
    a.speed       = detail::numberAt(j, "speed", a.speed);
    a.playOnStart = j.value("playOnStart", a.playOnStart);
    a.looping     = j.value("looping",     a.looping);
}

} // namespace Vkm::Engine::ComponentSerializer

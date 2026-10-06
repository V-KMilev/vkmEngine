#define VKM_LOG_CATEGORY "IO"

#include "io/scene/component_serializer.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "logger.h"

#include "debug/engine_error_log.h"
#include "ecs/scene.h"
#include "ecs/component/render/camera.h"
#include "ecs/environment.h"
#include "ecs/component/core/transform.h"
#include "io/json_vec.h"
#include "core/reflect.h"
#include "resource/resource_manager.h"
#include "system/physics/authoring/mesh_collider.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"

namespace Vkm::Engine::ComponentSerializer {

namespace {

using ::Vkm::Engine::detail::vec2ToJson;
using ::Vkm::Engine::detail::vec3ToJson;
using ::Vkm::Engine::detail::vec4ToJson;
using ::Vkm::Engine::detail::quatToJson;
using ::Vkm::Engine::detail::jsonToVec2;
using ::Vkm::Engine::detail::jsonToVec3;
using ::Vkm::Engine::detail::jsonToVec4;
using ::Vkm::Engine::detail::jsonToQuat;

// The leaves saveReflected / loadReflected forward each field to; a new field
// type adds a pair here.
inline nlohmann::json toJson(bool        v) { return v; }
inline nlohmann::json toJson(int         v) { return v; }
inline nlohmann::json toJson(uint32_t    v) { return v; }
inline nlohmann::json toJson(float       v) { return v; }
inline nlohmann::json toJson(const std::string& s) { return s; }
inline nlohmann::json toJson(const glm::vec2& v) { return vec2ToJson(v); }
inline nlohmann::json toJson(const glm::vec3& v) { return vec3ToJson(v); }
inline nlohmann::json toJson(const glm::vec4& v) { return vec4ToJson(v); }
inline nlohmann::json toJson(const glm::quat& q) { return quatToJson(q); }

inline void fromJson(const nlohmann::json& j, bool&     v) { v = j.get<bool>(); }
inline void fromJson(const nlohmann::json& j, int&      v) { v = j.get<int>(); }
inline void fromJson(const nlohmann::json& j, uint32_t& v) { v = j.get<uint32_t>(); }
inline void fromJson(const nlohmann::json& j, float&    v) { v = j.get<float>(); }
inline void fromJson(const nlohmann::json& j, std::string& s) { s = j.get<std::string>(); }
inline void fromJson(const nlohmann::json& j, glm::vec2& v) { v = jsonToVec2(j, v); }
inline void fromJson(const nlohmann::json& j, glm::vec3& v) { v = jsonToVec3(j, v); }
inline void fromJson(const nlohmann::json& j, glm::vec4& v) { v = jsonToVec4(j, v); }
inline void fromJson(const nlohmann::json& j, glm::quat& q) { q = jsonToQuat(j, q); }

// Fixed-size character buffers (Name::value).
template<std::size_t N>
inline nlohmann::json toJson(const char (&v)[N]) { return std::string(v); }
template<std::size_t N>
inline void fromJson(const nlohmann::json& j, char (&v)[N]) {
    const std::string s = j.get<std::string>();
    std::strncpy(v, s.c_str(), N - 1);
    v[N - 1] = '\0';
}

// Scoped enums registered via VKM_ENUM_NAMES. The SFINAE leaves non-enum types
// to the overloads above.
template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
inline nlohmann::json toJson(E v) { return Reflect::enumName(v); }
template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
inline void fromJson(const nlohmann::json& j, E& v) {
    const std::string name = j.get<std::string>();
    // An unknown name keeps the constructed default and warns, rather than
    // becoming a valid-looking enumerator zero.
    if (!Reflect::enumFromNameChecked(name, v)) {
        LOG_WARNING(
            "No enumerator called '%s' in this build; leaving the field at its default",
            name.c_str()
        );
    }
}

// A Handle is not a toJson leaf: it needs the ResourceManager to become its name
// and back.
template<typename T>  struct IsHandle                     : std::false_type {};
template<typename A>  struct IsHandle<Handle<A>>          : std::true_type  {};

// A vector is a JSON array of its elements, each through the same dispatch.
template<typename T>     struct IsVector                     : std::false_type {};
template<typename E, typename A> struct IsVector<std::vector<E, A>> : std::true_type {};

// Defined beside the unresolved-reference list it appends to.
template<typename Asset>
Handle<Asset> resolveAssetRef(
    const ResourceManager& r,
    const std::string& name,
    const char* what,
    const char* field
);

/**
 * @brief Stand-in context for a component that names no assets.
 *
 * Reaching a handle with this is a static_assert: the component needs the other
 * overload.
 */
struct NoResources {};

template<typename T, typename Ctx>
nlohmann::json saveReflected(const T& obj, const Ctx& ctx);
template<typename T, typename Ctx>
void loadReflected(const nlohmann::json& j, T& obj, const Ctx& ctx, bool inArray = false);

/**
 * @brief One value to JSON, whatever kind of value it is.
 *
 * A handle becomes its name, a vector an array of recursive calls, a reflected
 * struct an object, and anything else a toJson leaf.
 *
 * @tparam V   The value's type.
 * @tparam Ctx ResourceManager, or NoResources for a component that names no assets.
 * @param val  The value to write.
 * @param ctx  Resolves a handle to its name.
 * @return The value as JSON.
 */
template<typename V, typename Ctx>
nlohmann::json valueToJson(const V& val, const Ctx& ctx) {
    if constexpr (IsHandle<V>::value) {
        static_assert(
            !std::is_same_v<Ctx, NoResources>,
            "a component holding a Handle<T> serializes through the "
                "ResourceManager overload - the name is the identity"
        );
        return val ? ctx.get(val).name() : std::string{};
    } else if constexpr (IsVector<V>::value) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& element : val) arr.push_back(valueToJson(element, ctx));
        return arr;
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        return saveReflected(val, ctx);
    } else {
        return toJson(val);
    }
}

/**
 * @brief The same question in the other direction.
 *
 * @tparam V    The value's type.
 * @tparam Ctx  ResourceManager, or NoResources for a component that names no assets.
 * @param j     The JSON to read.
 * @param val   The value to fill.
 * @param ctx   Resolves a name back to a handle.
 * @param field Key the value is stored under, labelling an unresolved asset
 *              reference; null inside an array, where a kept name has nowhere to go.
 */
template<typename V, typename Ctx>
void valueFromJson(const nlohmann::json& j, V& val, const Ctx& ctx, const char* field) {
    if constexpr (IsHandle<V>::value) {
        static_assert(
            !std::is_same_v<Ctx, NoResources>,
            "a component holding a Handle<T> serializes through the "
                "ResourceManager overload - the name is the identity"
        );
        using Asset = typename V::resource_t;
        val = resolveAssetRef<Asset>(
            ctx,
            j.is_string() ? j.get<std::string>() : std::string{},
            Reflect::enumName(ASSET_TYPE<Asset>),
            field
        );
    } else if constexpr (IsVector<V>::value) {
        val.clear();
        if (!j.is_array()) return;
        val.reserve(j.size());
        for (const nlohmann::json& element : j) {
            typename V::value_type item{};
            // An array element has no key to restore a kept name into. See
            // resolveAssetRef.
            valueFromJson(element, item, ctx, nullptr);
            val.push_back(std::move(item));
        }
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        // A struct inside an array stays inside it: its keys address the element.
        const bool inArray = field == nullptr;
        loadReflected(j, val, ctx, inArray);
    } else {
        fromJson(j, val);
    }
}

template<typename T, typename Ctx>
nlohmann::json saveReflected(const T& obj, const Ctx& ctx) {
    nlohmann::json out = nlohmann::json::object();
    ::Vkm::Engine::Reflect::forEachField(obj, [&](std::string_view name, const auto& val) {
        out[std::string(name)] = valueToJson(val, ctx);
    });
    return out;
}

template<typename T, typename Ctx>
void loadReflected(const nlohmann::json& j, T& obj, const Ctx& ctx, bool inArray) {
    ::Vkm::Engine::Reflect::forEachField(obj, [&](std::string_view name, auto& val) {
        const std::string key(name);
        auto it = j.find(key);
        if (it != j.end()) valueFromJson(*it, val, ctx, inArray ? nullptr : key.c_str());
    });
}

template<typename T>
nlohmann::json saveReflected(const T& obj) { return saveReflected(obj, NoResources{}); }

template<typename T>
void loadReflected(const nlohmann::json& j, T& obj) { loadReflected(j, obj, NoResources{}); }

} // namespace

// The driver carries leaves, enums, nested reflected structs, vectors and
// Handle<T>; a component is hand-written only where it cannot reach. One that
// persists less than it holds reflects fewer fields.

nlohmann::json save(const Environment& env) {
    return saveReflected(env);
}

void load(const nlohmann::json& j, Environment& env) {
    loadReflected(j, env);

    // A phase asymmetry of one divides zero by zero in the sky bake and the fog,
    // so both are clamped to what they can draw, with a warning.
    const float mieG = std::clamp(env.sky.mieG, 0.0f, SkySettings::MAX_MIE_G);
    if (mieG != env.sky.mieG) {
        LOG_WARNING("Sky mieG %g is outside 0..%g; using %g", env.sky.mieG, SkySettings::MAX_MIE_G, mieG);
        env.sky.mieG = mieG;
    }
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

nlohmann::json save(const PhysicsSettings& p)          { return saveReflected(p); }
void load(const nlohmann::json& j, PhysicsSettings& p) { loadReflected(j, p); }

nlohmann::json save(const Name& n)          { return saveReflected(n); }
void load(const nlohmann::json& j, Name& n) { loadReflected(j, n); }

nlohmann::json save(const Transform& t)          { return saveReflected(t); }
void load(const nlohmann::json& j, Transform& t) { loadReflected(j, t); }

nlohmann::json save(const Camera& c)          { return saveReflected(c); }
void load(const nlohmann::json& j, Camera& c) { loadReflected(j, c); }

nlohmann::json save(const Light& l)          { return saveReflected(l); }
void load(const nlohmann::json& j, Light& l) { loadReflected(j, l); }

nlohmann::json save(const Rigidbody& rb)          { return saveReflected(rb); }
void load(const nlohmann::json& j, Rigidbody& rb) { loadReflected(j, rb); }

nlohmann::json save(const CharacterController& cc)          { return saveReflected(cc); }
void load(const nlohmann::json& j, CharacterController& cc) { loadReflected(j, cc); }

nlohmann::json save(const Joint& joint, const EntityNamer& name) {
    // The driver carries all but the entity reference, `type` included: it is
    // the one place that says what an unknown enum name means.
    nlohmann::json out = saveReflected(joint);
    out["connected"] = name(joint.connected);
    return out;
}
void load(const nlohmann::json& j, Joint& out, const EntityResolver& resolve) {
    loadReflected(j, out);
    out.connected = resolve(j.value("connected", 0u));
}

nlohmann::json save(const Ragdoll& r, const EntityNamer& name) {
    nlohmann::json out = saveReflected(r);

    nlohmann::json bones = nlohmann::json::array();
    for (const RagdollBone& bone : r.bones) {
        nlohmann::json entry;
        entry["bone"] = bone.bone;
        entry["body"] = name(bone.body);
        nlohmann::json offset = nlohmann::json::array();
        const float* m = glm::value_ptr(bone.bodyFromBone);
        for (int i = 0; i < 16; ++i) offset.push_back(m[i]);
        entry["offset"] = std::move(offset);
        bones.push_back(std::move(entry));
    }
    out["bones"] = std::move(bones);
    out["root"] = name(r.root);
    return out;
}
void load(const nlohmann::json& j, Ragdoll& r, const EntityResolver& resolve) {
    loadReflected(j, r);

    r.root = resolve(j.value("root", 0u));

    r.bones.clear();
    auto it = j.find("bones");
    if (it == j.end() || !it->is_array()) return;

    r.bones.reserve(it->size());
    for (const auto& entry : *it) {
        if (!entry.is_object()) continue;
        RagdollBone bone;
        bone.bone = entry.value("bone", -1);
        bone.body = resolve(entry.value("body", 0u));
        const auto offset = entry.find("offset");
        if (offset != entry.end() && offset->is_array() && offset->size() == 16) {
            float* m = glm::value_ptr(bone.bodyFromBone);
            for (int i = 0; i < 16; ++i) m[i] = (*offset)[i].get<float>();
        }
        r.bones.push_back(bone);
    }
}

namespace {

// Names the loaders could not resolve since takeUnresolvedRefs last drained them.
std::vector<UnresolvedRef> g_unresolved;

// Resolve a saved asset name to a handle. An unresolved name leaves the slot
// empty and goes through reportError, so a host's error sink shows it. It is kept
// under its field for a later save to write back, unless the field is null (a
// name read out of an array).
template<typename Asset>
Handle<Asset> resolveAssetRef(
    const ResourceManager& r,
    const std::string& name,
    const char* what,
    const char* field
) {
    if (name.empty()) return {};
    Handle<Asset> h = r.findByName<Asset>(name);
    if (!h) {
        reportError(
            "Scene",
            std::string(what) + " '" + name + "'",
            "reference left unresolved - the asset is not loaded, so the slot is empty"
        );
        if (field) g_unresolved.push_back({field, name, ASSET_TYPE<Asset>});
    }
    return h;
}

} // namespace

std::vector<UnresolvedRef> takeUnresolvedRefs() {
    std::vector<UnresolvedRef> taken;
    taken.swap(g_unresolved);
    return taken;
}

UnresolvedScope::~UnresolvedScope() { takeUnresolvedRefs(); }

nlohmann::json save(const Collider& c, const ResourceManager& resources) {
    return saveReflected(c, resources);
}
void load(const nlohmann::json& j, Collider& c, const ResourceManager& resources) {
    loadReflected(j, c, resources);
    // Built here rather than by the first tick: a query is not a tick, and in
    // the editor nothing ticks.
    syncMeshCollider(c, resources);
}
void emitAssetRefs(const Collider& c, AssetRefs& refs) {
    for (const ColliderPart& part : c.parts) {
        if (part.mesh) refs.meshes.push_back(part.mesh);
    }
}

nlohmann::json save(const Mesh& m, const ResourceManager& resources) {
    return saveReflected(m, resources);
}
void load(const nlohmann::json& j, Mesh& m, const ResourceManager& resources) {
    loadReflected(j, m, resources);
}
void emitAssetRefs(const Mesh& m, AssetRefs& refs) {
    if (m.mesh)     refs.meshes.push_back(m.mesh);
    if (m.material) refs.materials.push_back(m.material);
}

nlohmann::json save(const Animator& a, const ResourceManager& resources) {
    return saveReflected(a, resources);
}
void load(const nlohmann::json& j, Animator& a, const ResourceManager& resources) {
    loadReflected(j, a, resources);
}
void emitAssetRefs(const Animator& a, AssetRefs& refs) {
    // fadeFrom is runtime state no save writes.
    if (a.skeleton) refs.skeletons.push_back(a.skeleton);
    if (a.clip)     refs.clips.push_back(a.clip);
}

nlohmann::json save(const BoneSocket& s)          { return saveReflected(s); }
void load(const nlohmann::json& j, BoneSocket& s) { loadReflected(j, s); }

nlohmann::json save(const LOD& l, const ResourceManager& resources) {
    return saveReflected(l, resources);
}
void load(const nlohmann::json& j, LOD& l, const ResourceManager& resources) {
    loadReflected(j, l, resources);
}
void emitAssetRefs(const LOD& l, AssetRefs& refs) {
    for (const LODLevel& level : l.levels) {
        if (level.mesh) refs.meshes.push_back(level.mesh);
    }
}

nlohmann::json save(const Decal& d, const ResourceManager& resources) {
    return saveReflected(d, resources);
}
void load(const nlohmann::json& j, Decal& d, const ResourceManager& resources) {
    loadReflected(j, d, resources);
}
void emitAssetRefs(const Decal& d, AssetRefs& refs) {
    if (d.material) refs.materials.push_back(d.material);
}

nlohmann::json save(const ParticleEmitter& e)          { return saveReflected(e); }
void load(const nlohmann::json& j, ParticleEmitter& e) { loadReflected(j, e); }

nlohmann::json save(const ReflectionProbe& p)          { return saveReflected(p); }
void load(const nlohmann::json& j, ReflectionProbe& p) { loadReflected(j, p); }

nlohmann::json save(const AudioSource& s, const ResourceManager& resources) {
    return saveReflected(s, resources);
}
void load(const nlohmann::json& j, AudioSource& s, const ResourceManager& resources) {
    loadReflected(j, s, resources);
}
void emitAssetRefs(const AudioSource& s, AssetRefs& refs) {
    if (s.clip) refs.sounds.push_back(s.clip);
}

nlohmann::json save(const AudioListener& l)          { return saveReflected(l); }
void load(const nlohmann::json& j, AudioListener& l) { loadReflected(j, l); }

nlohmann::json save(const IrradianceVolume& v)          { return saveReflected(v); }
void load(const nlohmann::json& j, IrradianceVolume& v) { loadReflected(j, v); }

nlohmann::json save(const UICanvas& c)          { return saveReflected(c); }
void load(const nlohmann::json& j, UICanvas& c) { loadReflected(j, c); }

nlohmann::json save(const UIElement& e)          { return saveReflected(e); }
void load(const nlohmann::json& j, UIElement& e) { loadReflected(j, e); }

nlohmann::json save(const UIImage& i, const ResourceManager& resources) {
    return saveReflected(i, resources);
}
void load(const nlohmann::json& j, UIImage& i, const ResourceManager& resources) {
    loadReflected(j, i, resources);
}
void emitAssetRefs(const UIImage& i, AssetRefs& refs) {
    if (i.texture) refs.textures.push_back(i.texture);
}

nlohmann::json save(const UIText& t)          { return saveReflected(t); }
void load(const nlohmann::json& j, UIText& t) { loadReflected(j, t); }

nlohmann::json save(const UIButton& b)          { return saveReflected(b); }
void load(const nlohmann::json& j, UIButton& b) { loadReflected(j, b); }

nlohmann::json save(const UIScroll& s)          { return saveReflected(s); }
void load(const nlohmann::json& j, UIScroll& s) { loadReflected(j, s); }

nlohmann::json save(const Hierarchy& h) {
    return nlohmann::json{{"parent", h.parent.slot()}};
}
uint32_t loadParentIndex(const nlohmann::json& j) {
    return j.value("parent", std::numeric_limits<uint32_t>::max());
}

namespace {

template<typename T, typename ValueWriter>
nlohmann::json saveTrack(const AnimationTrack<T>& track, ValueWriter writeValue) {
    nlohmann::json keyframes = nlohmann::json::array();
    const auto& times  = track.getTimes();
    const auto& values = track.getValues();
    for (size_t i = 0; i < times.size(); ++i) {
        keyframes.push_back({{"t", times[i]}, {"v", writeValue(values[i])}});
    }
    return {
        {"easing",    toJson(track.getEasing())},
        {"keyframes", std::move(keyframes)},
    };
}

template<typename T, typename ValueReader>
void loadTrack(const nlohmann::json& j, AnimationTrack<T>& track, ValueReader readValue) {
    track.clear();
    if (j.contains("easing")) {
        Easing easing = Easing::Linear;
        fromJson(j["easing"], easing);
        track.setEasing(easing);
    }
    if (j.contains("keyframes") && j["keyframes"].is_array()) {
        for (const auto& kf : j["keyframes"]) {
            // at(), not operator[]: const operator[] on a missing key only asserts,
            // then reads the map's end node in release. The scene loader catches the throw.
            track.addKeyframe(kf.value("t", 0.0f), readValue(kf.at("v")));
        }
    }
}

} // namespace

nlohmann::json save(const Animation& a) {
    return {
        {"position", saveTrack(a.positionTrack, [](const glm::vec3& v) { return vec3ToJson(v); })},
        {"rotation", saveTrack(a.rotationTrack, [](const glm::quat& q) { return quatToJson(q); })},
        {"scale",    saveTrack(a.scaleTrack,    [](const glm::vec3& v) { return vec3ToJson(v); })},
        {"length",      a.length},
        {"speed",       a.speed},
        {"playOnStart", a.playOnStart},
        {"looping",     a.looping},
    };
}

void load(const nlohmann::json& j, Animation& a) {
    const auto readVec3 = [](const nlohmann::json& v) { return jsonToVec3(v); };
    const auto readQuat = [](const nlohmann::json& v) { return jsonToQuat(v); };
    if (j.contains("position")) loadTrack(j["position"], a.positionTrack, readVec3);
    if (j.contains("rotation")) loadTrack(j["rotation"], a.rotationTrack, readQuat);
    if (j.contains("scale"))    loadTrack(j["scale"],    a.scaleTrack,    readVec3);
    a.length      = j.value("length",      a.length);
    a.speed       = j.value("speed",       a.speed);
    a.playOnStart = j.value("playOnStart", a.playOnStart);
    a.looping     = j.value("looping",     a.looping);
}

namespace {

/**
 * @brief Writes each visited reflected field into a JSON object.
 *
 * Read-only on the behavior (see the const_cast note in save()). A nested struct
 * becomes a sub-object, pushed by beginStruct and popped by endStruct.
 */
class BehaviorJsonWriter : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonWriter(nlohmann::json& out) { m_scopes.push_back(&out); }
        ~BehaviorJsonWriter() override = default;

        BehaviorJsonWriter(const BehaviorJsonWriter& other) = delete;
        BehaviorJsonWriter& operator=(const BehaviorJsonWriter& other) = delete;

        BehaviorJsonWriter(BehaviorJsonWriter && other) = delete;
        BehaviorJsonWriter& operator=(BehaviorJsonWriter && other) = delete;

    public:
        void field(const char* name, float& v) override { cur()[name] = v; }
        void field(const char* name, int& v)   override { cur()[name] = v; }
        void field(const char* name, bool& v)  override { cur()[name] = v; }
        void field(const char* name, glm::vec2& v) override { cur()[name] = vec2ToJson(v); }
        void field(const char* name, glm::vec3& v) override { cur()[name] = vec3ToJson(v); }
        void field(const char* name, glm::vec4& v) override { cur()[name] = vec4ToJson(v); }
        void field(const char* name, glm::quat& v) override { cur()[name] = quatToJson(v); }
        void field(const char* name, std::string& v) override { cur()[name] = v; }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            // By name, not index, so reordering enum values keeps scenes valid
            // (matches Reflect::enumName).
            if (index >= 0 && static_cast<std::size_t>(index) < count) cur()[name] = names[index];
        }

        // Written as its name; which assets-block section it belongs in is
        // AssetSerializer::collectAssetRefs's to say.
        void assetField(const char* name, std::string& assetName, AssetType type) override {
            field(name, assetName);
        }

        bool beginStruct(const char* name) override {
            m_scopes.push_back(&(cur()[name] = nlohmann::json::object()));
            return true;
        }
        void endStruct() override { m_scopes.pop_back(); }

    private:
        // json objects are std::map-backed, so these pointers survive sibling inserts.
        nlohmann::json& cur() { return *m_scopes.back(); }

    private:
        std::vector<nlohmann::json*> m_scopes;
};

/**
 * @brief Reads each visited reflected field from a JSON object.
 *
 * A missing or malformed key keeps the field's current value; so does a missing
 * or mistyped sub-object (beginStruct returns false).
 */
class BehaviorJsonReader : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonReader(const nlohmann::json& in) { m_scopes.push_back(&in); }
        ~BehaviorJsonReader() override = default;

        BehaviorJsonReader(const BehaviorJsonReader& other) = delete;
        BehaviorJsonReader& operator=(const BehaviorJsonReader& other) = delete;

        BehaviorJsonReader(BehaviorJsonReader && other) = delete;
        BehaviorJsonReader& operator=(BehaviorJsonReader && other) = delete;

    public:
        // A wrong-typed value keeps the field's current value with a warning: a
        // throw here would cost the whole load rather than one value.
        void field(const char* name, float& v) override {
            if (const nlohmann::json* node = present(name)) {
                if (node->is_number()) v = node->get<float>();
                else keepCurrent(name, "a number", *node);
            }
        }
        void field(const char* name, int& v) override {
            if (const nlohmann::json* node = present(name)) {
                if (node->is_number()) v = node->get<int>();
                else keepCurrent(name, "a number", *node);
            }
        }
        void field(const char* name, bool& v) override {
            if (const nlohmann::json* node = present(name)) {
                if (node->is_boolean()) v = node->get<bool>();
                else keepCurrent(name, "true or false", *node);
            }
        }
        void field(const char* name, glm::vec3& v) override {
            // A malformed node keeps the current value; jsonToVec3 says so.
            if (const nlohmann::json* node = present(name)) v = jsonToVec3(*node, v);
        }
        void field(const char* name, glm::vec2& v) override {
            if (const nlohmann::json* node = present(name)) v = jsonToVec2(*node, v);
        }
        void field(const char* name, glm::vec4& v) override {
            if (const nlohmann::json* node = present(name)) v = jsonToVec4(*node, v);
        }
        void field(const char* name, glm::quat& v) override {
            if (const nlohmann::json* node = present(name)) v = jsonToQuat(*node, v);
        }
        void field(const char* name, std::string& v) override {
            if (const nlohmann::json* node = present(name)) {
                if (node->is_string()) v = node->get<std::string>();
                else keepCurrent(name, "a string", *node);
            }
        }

        void assetField(const char* name, std::string& assetName, AssetType type) override {
            field(name, assetName);
        }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            const nlohmann::json* node = present(name);
            if (!node) return;
            if (!node->is_string()) {
                keepCurrent(name, "a value's name", *node);
                return;
            }
            const std::string picked = node->get<std::string>();
            for (std::size_t i = 0; i < count; ++i) {
                if (picked == names[i]) {
                    index = static_cast<int>(i);
                    return;
                }
            }
            // A removed or renamed value.
            keepCurrent(name, "a value this build has", *node);
        }

        bool beginStruct(const char* name) override {
            if (!cur().contains(name) || !cur()[name].is_object()) return false;   // keep current
            m_scopes.push_back(&cur()[name]);
            return true;
        }
        void endStruct() override { m_scopes.pop_back(); }

    private:
        const nlohmann::json& cur() { return *m_scopes.back(); }

        // The node @p name holds in the current scope; null when absent, which
        // keeps the field's value silently.
        const nlohmann::json* present(const char* name) {
            const auto it = cur().find(name);
            return it == cur().end() ? nullptr : &*it;
        }

        static void keepCurrent(const char* name, const char* wanted, const nlohmann::json& got) {
            LOG_WARNING(
                "Behavior field '%s' wants %s and holds '%s'; keeping its current value",
                name,
                wanted,
                got.dump().c_str()
            );
        }

    private:
        std::vector<const nlohmann::json*> m_scopes;
};

} // namespace

nlohmann::json save(const ScriptComponent& sc) {
    nlohmann::json behaviors = nlohmann::json::array();

    // A held one goes back where it was read from, since order is run order, and
    // exactly as read: the module that understands its properties is missing.
    const auto emitHeldAt = [&](size_t position) {
        for (const UnknownBehavior& kept : sc.unknown) {
            if (kept.index != position) continue;
            nlohmann::json props = nlohmann::json::parse(kept.properties, nullptr, false);
            if (props.is_discarded()) props = nlohmann::json::object();
            behaviors.push_back({{"type", kept.type}, {"properties", std::move(props)}});
        }
    };

    for (const auto& behavior : sc.behaviors) {
        if (!behavior) continue;
        emitHeldAt(behaviors.size());
        nlohmann::json props = nlohmann::json::object();
        BehaviorJsonWriter writer(props);
        // The writer only reads, so casting away const for visitFields is safe.
        const_cast<Behavior&>(*behavior).visitFields(writer);
        behaviors.push_back({{"type", behavior->typeName()}, {"properties", std::move(props)}});
    }

    // Whatever sat past the last constructed one, in the order recorded.
    const size_t total = sc.behaviors.size() + sc.unknown.size();
    while (behaviors.size() < total) {
        const size_t before = behaviors.size();
        emitHeldAt(before);
        if (behaviors.size() == before) break;
    }
    return {{"behaviors", std::move(behaviors)}};
}

void load(const nlohmann::json& j, ScriptComponent& sc) {
    sc.behaviors.clear();
    sc.unknown.clear();
    if (!j.contains("behaviors") || !j["behaviors"].is_array()) return;
    for (const auto& entry : j["behaviors"]) {
        const std::string type = entry.value("type", std::string{});
        if (type.empty()) continue;

        const bool hasProperties = entry.contains("properties") && entry["properties"].is_object();
        const nlohmann::json* props = hasProperties ? &entry["properties"] : nullptr;

        auto behavior = BehaviorRegistry::get().create(type);
        if (!behavior) {
            // Kept: an unregistered type means the module is absent, not that
            // the scene stopped wanting the behavior. See UnknownBehavior.
            std::string properties = props ? props->dump() : std::string{"{}"};
            sc.unknown.push_back({type, std::move(properties), sc.behaviors.size() + sc.unknown.size()});
            continue;
        }
        if (props) {
            BehaviorJsonReader reader(*props);
            behavior->visitFields(reader);
        }
        sc.behaviors.push_back(std::move(behavior));
    }
}

} // namespace Vkm::Engine::ComponentSerializer

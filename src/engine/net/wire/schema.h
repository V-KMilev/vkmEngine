#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_network/bit_stream.h"

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "ecs/scene.h"

namespace Vkm::Engine {

inline constexpr uint32_t MAX_COMPONENT_BYTES = 512;  ///< The most one component may occupy on the wire, per
                                                      ///< entity per snapshot: a bound on the packet, as one
                                                      ///< big component would starve every other entity. Past
                                                      ///< it the component is dropped for that snapshot and
                                                      ///< logged. A spawn sets it: the longest path plus
                                                      ///< NET_SPAWN_MAX_SLOTS slots.

namespace detail {

/**
 * @brief One of the two buffers this thread's sameOnWire encodes into, kept for its capacity.
 *
 * @param second Which of the two.
 * @return The buffer.
 */
std::vector<uint8_t>& wireScratch(bool second);

/// Whether T brings a codec of its own, found here or by argument-dependent lookup.
template<typename T, typename = void>
inline constexpr bool HAS_OWN_CODEC = false;

template<typename T>
inline constexpr bool HAS_OWN_CODEC<
    T,
    std::void_t<decltype(netEncode(std::declval<const T&>(), std::declval<Network::BitWriter&>()))>
> = true;

/// The floats a vector or quaternion field travels as.
template<typename V>
inline constexpr int FLOATS = 0;
template<> inline constexpr int FLOATS<glm::vec2> = 2;
template<> inline constexpr int FLOATS<glm::vec3> = 3;
template<> inline constexpr int FLOATS<glm::vec4> = 4;
template<> inline constexpr int FLOATS<glm::quat> = 4;

/**
 * @brief Write one field of a type that travels field by field, as its kind travels.
 *
 * @tparam V A float, 32-bit int, bool, VKM_ENUM_NAMES enum, vector, quaternion, string or reflected
 *           struct; anything else fails to compile.
 */
template<typename V>
void encodeField(const V& value, Network::BitWriter& out) {
    if constexpr (std::is_same_v<V, bool>) {
        out.boolean(value);
    } else if constexpr (std::is_same_v<V, float>) {
        out.f32(value);
    } else if constexpr (std::is_integral_v<V> && sizeof(V) == 4) {
        out.u32(static_cast<uint32_t>(value));
    } else if constexpr (std::is_enum_v<V>) {
        static_assert(Reflect::HAS_ENUM_NAMES<V>, "an enum field travels as its index: VKM_ENUM_NAMES it");
        out.u8(static_cast<uint8_t>(value));
    } else if constexpr (FLOATS<V> > 0) {
        for (int i = 0; i < FLOATS<V>; ++i) out.f32(value[i]);
    } else if constexpr (std::is_same_v<V, std::string>) {
        const size_t length = std::min<size_t>(value.size(), 255);
        out.u8(static_cast<uint8_t>(length));
        for (size_t i = 0; i < length; ++i) out.u8(static_cast<uint8_t>(value[i]));
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        Reflect::forEachField(value, [&](std::string_view, const auto& field) { encodeField(field, out); });
    } else {
        static_assert(Reflect::DEPENDENT_FALSE<V>, "this field has no wire form: give the type a codec");
    }
}

/// Read one field encodeField wrote; an enum index this build has no value for keeps the field.
template<typename V>
void decodeField(V& value, Network::BitReader& in) {
    if constexpr (std::is_same_v<V, bool>) {
        value = in.boolean();
    } else if constexpr (std::is_same_v<V, float>) {
        value = in.f32();
    } else if constexpr (std::is_integral_v<V> && sizeof(V) == 4) {
        value = static_cast<V>(in.u32());
    } else if constexpr (std::is_enum_v<V>) {
        const uint8_t index = in.u8();
        if (index < Reflect::EnumNames<V>::count) value = static_cast<V>(index);
    } else if constexpr (FLOATS<V> > 0) {
        for (int i = 0; i < FLOATS<V>; ++i) value[i] = in.f32();
    } else if constexpr (std::is_same_v<V, std::string>) {
        value.resize(in.u8());
        for (char& c : value) c = static_cast<char>(in.u8());
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        Reflect::forEachField(value, [&](std::string_view, auto& field) { decodeField(field, in); });
    }
}

/// The letter layoutOf writes for a field of kind V: two kinds of one size read each other's bits wrong.
template<typename V>
constexpr char wireKind() {
    if constexpr (std::is_same_v<V, bool>)             return 'b';
    else if constexpr (std::is_same_v<V, float>)       return 'f';
    else if constexpr (std::is_enum_v<V>)              return 'e';
    else if constexpr (std::is_integral_v<V>)          return std::is_signed_v<V> ? 'i' : 'u';
    else if constexpr (std::is_same_v<V, glm::quat>)   return 'q';
    else if constexpr (std::is_same_v<V, std::string>) return 's';
    else                                               return 'v';
}

/// Each field's name, size and kind, and an enum's names in order: what two ends compare of a type that
/// travels field by field.
template<typename T>
void layoutOf(std::string& out) {
    const T sample{};
    Reflect::forEachField(sample, [&](std::string_view name, const auto& field) {
        using V = std::decay_t<decltype(field)>;
        out += name;
        if constexpr (Reflect::IS_REFLECTED<V>) {
            out += '{';
            layoutOf<V>(out);
            out += '}';
        } else {
            out += ':';
            out += std::to_string(sizeof(V));
            out += wireKind<V>();
        }
        // An enum travels as its index, which names another value once the order changes.
        if constexpr (std::is_enum_v<V>) {
            for (size_t i = 0; i < Reflect::EnumNames<V>::count; ++i) {
                out += ',';
                out += Reflect::EnumNames<V>::values[i];
            }
        }
        out += ';';
    });
}

} // namespace detail

/// Write @p value: by its own netEncode, else field by field from its reflect block.
template<typename T>
void encodeComponent(const T& value, Network::BitWriter& out) {
    if constexpr (detail::HAS_OWN_CODEC<T>) {
        netEncode(value, out);
    } else {
        static_assert(Reflect::IS_REFLECTED<T>, "a replicated type needs a codec or a reflect block");
        const auto each = [&](std::string_view, const auto& field) { detail::encodeField(field, out); };
        Reflect::forEachField(value, each);
    }
}

/// Read what encodeComponent wrote into @p value.
template<typename T>
void decodeComponent(T& value, Network::BitReader& in) {
    if constexpr (detail::HAS_OWN_CODEC<T>) {
        netDecode(value, in);
    } else {
        Reflect::forEachField(value, [&](std::string_view, auto& field) { detail::decodeField(field, in); });
    }
}

/**
 * @brief Whether @p a and @p b encode to the same bits: the wire cannot tell them apart.
 *
 * Asked of every component a snapshot brings that the client holds, so it encodes into buffers
 * kept per thread rather than made each time.
 *
 * @param a One value.
 * @param b The other.
 * @return True when their encodings match bit for bit.
 */
template <typename T>
bool sameOnWire(const T& a, const T& b) {
    std::vector<uint8_t>& first  = detail::wireScratch(false);
    std::vector<uint8_t>& second = detail::wireScratch(true);
    Network::BitWriter one(first, MAX_COMPONENT_BYTES);
    Network::BitWriter two(second, MAX_COMPONENT_BYTES);
    encodeComponent(a, one);
    encodeComponent(b, two);
    one.finish();
    two.finish();
    return one.bitCount() == two.bitCount() && first == second;
}

/**
 * @brief What a held value takes of an incoming one the wire cannot tell from it: nothing, unless
 *        the type's codec carries a field exactly, which a type says by an overload of its own.
 *
 * @param held     The value this end holds, kept.
 * @param incoming The value the snapshot carried.
 */
template <typename T>
void netKeep(T& held, const T& incoming) {
    (void)held;
    (void)incoming;
}

/**
 * @brief One replicated component type, reduced to what the wire needs.
 *
 * Type-erased: a game module registers types after the runtime was built.
 */
struct NetType {
    std::string name;  ///< The serializable identity, as everywhere else in the engine.

    std::string layout;  ///< Its fields' names and kinds when it travels field by field; empty when it has a
                         ///< codec.

    bool (*has)(const Scene&, EntityId) = nullptr;  ///< Does this entity carry one? Decides its bit in a
                                                    ///< snapshot's mask.

    void (*encode)(const Scene&, EntityId, Network::BitWriter&) = nullptr;
        ///< Encode this entity's component. Asked only of an entity @ref has said yes to.

    bool (*decode)(Scene&, EntityId, Network::BitReader&) = nullptr;  ///< Decode onto this entity, adding the
                                                             ///< component when it is missing.

    void (*remove)(Scene&, EntityId) = nullptr;  ///< Take this entity's component away: the server holds it
                                                 ///< no longer.
};

/**
 * @brief The ordered list of what replicates, agreed by both ends before play.
 *
 * A component's wire identity is its index, so both ends must build the same
 * list in order; a fingerprint mismatch is refused at join. Rebuilt by
 * ScriptModule::setupNetwork: the engine's types, then the project's from the
 * module's vkmSetupNetwork export.
 */
class NetSchema {
    public:
        static constexpr size_t MAX_TYPES = 32;  ///< Most component types one game may replicate. One bit
                                                 ///< each in a snapshot entry's 32-bit mask.

    public:
        NetSchema() = default;
        ~NetSchema() = default;

        NetSchema(const NetSchema& other) = delete;
        NetSchema& operator=(const NetSchema& other) = delete;

        NetSchema(NetSchema && other) = delete;
        NetSchema& operator=(NetSchema && other) = delete;

    public:
        /// The registry every end consults. Filled by ScriptModule::setupNetwork, read thereafter.
        static NetSchema& get();

    public:
        /**
         * @brief Add @p T to what replicates.
         *
         * By free `netEncode(const T&, Network::BitWriter&)` and `netDecode(T&, Network::BitReader&)`, visible here
         * or by argument-dependent lookup; without them, field by field from T's reflect block,
         * whose fields the fingerprint covers. Call it from vkmSetupNetwork alone: the schema is
         * rebuilt from nothing each time that entry runs, so a type added anywhere else is gone
         * before play.
         *
         * @tparam T   The component type.
         * @param name Wire and diagnostic identity; unique.
         */
        template <typename T>
        void replicate(std::string name);

        /// Everything registered, in wire order.
        const std::vector<NetType>& types() const { return m_types; }

        size_t size() const { return m_types.size(); }

        /**
         * @brief Wire index of @p name, or -1.
         *
         * A linear search, so not for a per-entity path.
         *
         * @param name The registered name.
         * @return The index, or -1 when nothing is registered under it.
         */
        int indexOf(std::string_view name) const;

        /**
         * @brief A number that differs when the lists differ, or the codecs'
         *        layout does.
         *
         * Over order, names, the quantizer's steps and widths, and each field of a
         * type that travels field by field (layoutOf). Catches honest mismatches;
         * not a security measure.
         *
         * @return The fingerprint the Hello carries.
         */
        uint32_t fingerprint() const;

        /**
         * @brief Every name in order, for the log line that follows a refusal.
         *
         * @return The names, comma-separated, or "nothing".
         */
        std::string describe() const;

        /**
         * @brief Drops everything.
         *
         * For a game module being (re)loaded. Never mid-session: renumbering
         * would decode every snapshot against the wrong type.
         */
        void clear() { m_types.clear(); }

    private:
        void add(NetType type);

    private:
        std::vector<NetType> m_types;
};

template <typename T>
void NetSchema::replicate(std::string name) {
    NetType type;
    type.name = std::move(name);
    if constexpr (!detail::HAS_OWN_CODEC<T>) detail::layoutOf<T>(type.layout);

    // Stateless thunks: plain function pointers, nothing owned.
    type.has = [](const Scene& scene, EntityId entity) { return scene.has<T>(entity); };
    type.encode = [](const Scene& scene, EntityId entity, Network::BitWriter& writer) {
        encodeComponent(scene.get<T>(entity), writer);
    };
    type.decode = [](Scene& scene, EntityId entity, Network::BitReader& reader) {
        T* held = scene.tryGet<T>(entity);
        if (!held) {
            // Added, not required: a client learns of a component by being sent one.
            decodeComponent(scene.add<T>(entity, T{}), reader);
            return !reader.failed();
        }
        // A value the wire cannot tell from the one held stays as held: a scene's authored
        // position is exact, and its quantized copy would only move it by a rounding.
        T incoming = *held;
        decodeComponent(incoming, reader);
        if (reader.failed()) return false;
        if (sameOnWire(*held, incoming)) netKeep(*held, incoming);
        else                             *held = std::move(incoming);
        return true;
    };
    type.remove = [](Scene& scene, EntityId entity) { scene.remove<T>(entity); };

    add(std::move(type));
}

} // namespace Vkm::Engine

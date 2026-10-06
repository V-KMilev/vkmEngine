#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/scene.h"
#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

/**
 * @brief One replicated component type, reduced to what the wire needs.
 *
 * Type-erased: a game module registers types after the runtime was built.
 */
struct NetType {
    std::string name;  ///< The serializable identity, as everywhere else in the engine.

    /// Does this entity carry one? Decides its bit in a snapshot's mask.
    bool (*has)(const Scene&, EntityId) = nullptr;

    /// Encode this entity's component. Asked only of an entity @ref has said yes to.
    void (*encode)(const Scene&, EntityId, BitWriter&) = nullptr;

    /// Decode onto this entity, adding the component when it is missing.
    bool (*decode)(Scene&, EntityId, BitReader&) = nullptr;
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
        /**
         * @brief Most component types one game may replicate.
         *
         * One bit each in a snapshot entry's 32-bit mask.
         */
        static constexpr size_t MAX_TYPES = 32;

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
         * Requires free `netEncode(const T&, BitWriter&)` and `netDecode(T&,
         * BitReader&)` visible here, or by argument-dependent lookup.
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
         * Over order, names and the quantiser's steps and widths. Catches
         * honest mismatches; not a security measure.
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

    // Stateless thunks: plain function pointers, nothing owned.
    type.has = [](const Scene& scene, EntityId entity) { return scene.has<T>(entity); };
    type.encode = [](const Scene& scene, EntityId entity, BitWriter& writer) {
        netEncode(scene.get<T>(entity), writer);
    };
    type.decode = [](Scene& scene, EntityId entity, BitReader& reader) {
        // Added, not required: a client learns of a component by being sent one.
        T* held = scene.tryGet<T>(entity);
        if (!held) held = &scene.add<T>(entity, T{});
        netDecode(*held, reader);
        return !reader.failed();
    };

    add(std::move(type));
}

} // namespace Vkm::Engine

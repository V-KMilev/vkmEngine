#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/scene.h"
#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

/// Who a replicated component is sent to.
enum class NetPolicy : uint8_t {
    /**
     * @brief Everyone.
     *
     * What the world looks like is the same question for every player, so this is the
     * default and almost everything is this.
     */
    World,

    /**
     * @brief Only the connection that owns the entity.
     *
     * For state that exists to make one player's own prediction converge and means
     * nothing to anybody else.
     */
    OwnerOnly,
};

/**
 * @brief One replicated component type, reduced to what the wire needs.
 *
 * Type-erased on purpose: `Scene` is open and registration happens from a game
 * module loaded after the runtime was built, so the set of replicated types is
 * not knowable at compile time and cannot be a template parameter of anything
 * that outlives startup.
 */
struct NetType {
    std::string name;   ///< The serializable identity, as everywhere else in the engine.
    NetPolicy   policy = NetPolicy::World;

    /// Does this entity carry one? What decides its bit in a snapshot's mask.
    bool (*has)(const Scene&, EntityId) = nullptr;

    /// Encode this entity's component. False when the entity does not have one.
    bool (*encode)(const Scene&, EntityId, BitWriter&) = nullptr;

    /// Decode onto this entity, adding the component when it is missing.
    bool (*decode)(Scene&, EntityId, BitReader&) = nullptr;
};

/**
 * @brief The ordered list of what replicates, agreed by both ends before play.
 *
 * A component's wire identity is its index here, so both ends must build the
 * same list in the same order. They do, when they run the same build of the
 * same project - which is the only case that can work anyway, since gameplay
 * code decides what these components mean. The handshake carries a fingerprint
 * of the list so a mismatch is refused at join with a message, rather than
 * discovered later as a world that decodes into nonsense.
 *
 * There is one, and it is filled once at startup: by the engine for its own
 * types, then by the project's module through the registration seam that
 * already exists for behaviors.
 */
class NetSchema {
    public:
        NetSchema() = default;
        ~NetSchema() = default;

        NetSchema(const NetSchema& other) = delete;
        NetSchema& operator=(const NetSchema& other) = delete;

        NetSchema(NetSchema && other) = delete;
        NetSchema& operator=(NetSchema && other) = delete;

        /**
         * @brief Most component types one game may replicate.
         *
         * A snapshot names which components an entry carries as one bit each in
         * a 32-bit mask, so the thirty-third would shift past the end of it.
         */
        static constexpr size_t MAX_TYPES = 32;

    public:
        /// The registry every end consults. Filled at startup, read thereafter.
        static NetSchema& get();

    public:
        /**
         * @brief Add @p T to what replicates.
         *
         * Requires `netEncode(const T&, BitWriter&)` and `netDecode(T&,
         * BitReader&)` to be visible - as free functions beside the component,
         * found by ordinary lookup or by argument-dependent lookup for a
         * project's own types. A component that has no codec is a compile
         * error at the registration line, which is where the mistake is.
         *
         * @param name   The wire and diagnostic identity. Must be unique.
         * @param policy Who receives it.
         */
        template <typename T>
        void replicate(std::string name, NetPolicy policy = NetPolicy::World);

        /// Everything registered, in wire order.
        const std::vector<NetType>& types() const { return m_types; }

        size_t size() const { return m_types.size(); }

        /**
         * @brief Wire index of @p name, or -1.
         *
         * For diagnostics and tests; the frame path uses the index directly.
         */
        int indexOf(std::string_view name) const;

        /**
         * @brief A number that differs when the lists differ.
         *
         * Order-sensitive and name-sensitive, because both are what the wire
         * indices depend on. Not a security measure - it catches the honest
         * mistake of two ends built from different source, which is the only
         * mismatch that happens.
         */
        uint32_t fingerprint() const;

        /**
         * @brief Every name in order, for the log line that follows a refusal.
         *
         * The two ends print this and a developer diffs two lines.
         */
        std::string describe() const;

        /**
         * @brief Drops everything.
         *
         * Called where a game module is loaded or reloaded: the module names
         * what it replicates, so its types go when it does. A session that has
         * already joined never does this - the wire index is the identity, and
         * renumbering mid-game would decode every snapshot against the wrong
         * type.
         */
        void clear() { m_types.clear(); }

    private:
        void add(NetType type);

    private:
        std::vector<NetType> m_types;
};

template <typename T>
void NetSchema::replicate(std::string name, NetPolicy policy) {
    NetType type;
    type.name   = std::move(name);
    type.policy = policy;

    // Thunks, so the frame path calls one function pointer and never a virtual
    // through a type it cannot name. Stateless, so they are plain code
    // addresses rather than anything that has to be owned.
    type.has = [](const Scene& scene, EntityId entity) { return scene.has<T>(entity); };
    type.encode = [](const Scene& scene, EntityId entity, BitWriter& writer) {
        if (!scene.has<T>(entity)) return false;
        netEncode(scene.get<T>(entity), writer);
        return true;
    };
    type.decode = [](Scene& scene, EntityId entity, BitReader& reader) {
        // Added rather than required: a client learns about a component by
        // being sent one, and refusing would leave the two worlds permanently
        // different in a way nothing later corrects.
        if (!scene.has<T>(entity)) scene.add<T>(entity, T{});
        netDecode(scene.get<T>(entity), reader);
        return !reader.failed();
    };

    add(std::move(type));
}

} // namespace Vkm::Engine

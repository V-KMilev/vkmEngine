#define VKM_LOG_CATEGORY "NET"

#include "net/net_session.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "l_assert.h"
#include "logger.h"

#include "debug/profiler.h"
#include "ecs/component/prefab/net_spawn.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"
#include "net/wire/codecs.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Returned for an entity no player drives; reads as nothing held.
 */
const InputCommand NO_COMMAND{};

} // namespace

void sendBare(NetCore& core, NetConnection& connection, NetMessage message, uint64_t token) {
    BitWriter writer(core.payload, 16);
    writeMessageHeader(writer, message, token);
    writer.finish();
    connection.frame(core.payload.data(), core.payload.size(), core.datagram);
    core.socket.send(connection.peer(), core.datagram.data(), core.datagram.size());
}

BitReader receivedPayload(const NetCore& core) {
    const size_t header = NetConnection::HEADER_BYTES;
    if (core.datagram.size() < header) return BitReader(core.datagram.data(), 0);
    return BitReader(core.datagram.data() + header, core.datagram.size() - header);
}

NetSession::NetSession()
    : m_server(m_core)
    , m_client(m_core)
{
}

NetSession::~NetSession() = default;

bool NetSession::acceptsTickRate(uint32_t tickRate) {
    if (tickRate > 0 && tickRate <= NET_MAX_TICK_RATE) return true;
    m_core.lastError = "the project ticks faster than a networked game runs";
    LOG_ERROR(
        "Cannot open a session at %u ticks a second: a networked game runs at most %u "
            "- see NET_MAX_TICK_RATE",
        tickRate,
        NET_MAX_TICK_RATE
    );
    return false;
}

bool NetSession::host(uint16_t port, uint32_t maxPlayers, uint32_t tickRate) {
    close();

    if (maxPlayers == 0) {
        m_core.lastError = "this project is for no players";
        LOG_ERROR("Cannot host: the project says it has no seats");
        return false;
    }
    if (!acceptsTickRate(tickRate)) return false;
    if (!m_core.socket.open(port)) {
        m_core.lastError = "could not bind the port";
        LOG_ERROR("Cannot host on port %u: the port is in use or not permitted", port);
        return false;
    }
    m_role          = NetRole::Server;
    m_core.tickRate = tickRate;
    m_server.open(maxPlayers);
    m_core.lastError.clear();
    LOG_INFO(
        "Hosting on port %u, %u seat(s), schema: %s",
        m_core.socket.localAddress().port,
        maxPlayers,
        NetSchema::get().describe().c_str()
    );
    return true;
}

bool NetSession::connect(const NetAddress& server, uint32_t tickRate) {
    close();
    if (!acceptsTickRate(tickRate)) return false;
    if (!m_core.socket.open(0)) {
        m_core.lastError = "could not open a socket";
        LOG_ERROR("Cannot join %s: no socket could be opened", server.toString().c_str());
        return false;
    }
    m_role          = NetRole::Client;
    m_core.tickRate = tickRate;
    m_client.open(server);
    m_core.lastError.clear();

    // Nothing is sent yet: actions are defined inside the first tick, so a
    // hello from here would carry a count of zero.
    LOG_INFO("Joining %s", server.toString().c_str());
    return true;
}

void NetSession::close() {
    if (m_role == NetRole::Offline) return;

    // Each half says goodbye before the socket goes.
    m_client.close();
    m_server.close();
    m_core.socket.close();

    m_role            = NetRole::Offline;
    m_sendAccumulator = 1.0f;
    m_core.serverTick = 0;
}

void NetSession::disconnect() {
    close();
    m_role = NetRole::Disconnected;
}

void NetSession::onSpawn(NetServer::SpawnPlayer spawn, NetServer::DespawnPlayer despawn) {
    m_server.onSpawn(std::move(spawn), std::move(despawn));
}

bool NetSession::isPlaying() const {
    if (m_role == NetRole::Server)  return true;
    if (m_role == NetRole::Offline) return false;
    return m_client.isPlaying();
}

float NetSession::pacing() const {
    return m_role == NetRole::Client && isPlaying() ? m_client.pacing() : 1.0f;
}

float NetSession::roundTrip() const {
    return m_role == NetRole::Client ? m_client.roundTrip() : 0.0f;
}

size_t NetSession::playerCount() const {
    if (m_role == NetRole::Server) return m_server.playerCount();
    return isPlaying() ? 1u : 0u;
}

bool NetSession::simulates(EntityId entity) const {
    if (isAuthority(m_role)) return true;
    return m_client.simulates(entity);
}

bool NetSession::isMine(EntityId entity) const {
    // A server holds no local entity, so it answers no.
    if (m_role == NetRole::Offline) return true;
    return entity && entity == m_client.localEntity();
}

const InputCommand& NetSession::commandFor(EntityId entity) const {
    if (entity && entity == m_client.localEntity()) return m_currentCommand;

    if (m_role == NetRole::Server) {
        const InputCommand* sent = m_server.commandFor(entity);
        return sent ? *sent : NO_COMMAND;
    }
    if (m_role == NetRole::Offline) return m_currentCommand;
    return NO_COMMAND;
}

void NetSession::beginTick(
    uint32_t tick,
    const InputCommand& command,
    uint32_t actionCount,
    uint64_t actionNames
) {
    m_currentCommand = command;
    m_core.ticked    = true;
    m_client.ageLeases();

    // A server's count comes from the joining client: its own behaviors may
    // not have started, and a zero would misdecode every command.
    if (m_role != NetRole::Server) m_core.actionCount = actionCount;

    // Every role: a server resolves commands through its own map too.
    if (actionNames != 0) m_core.actionNames = actionNames;

    if (m_role == NetRole::Client)      m_client.beginTick(command);
    else if (m_role == NetRole::Server) m_server.beginTick(tick);
}

void NetSession::beginReplayTick(const InputCommand& command) {
    m_currentCommand = command;
    m_client.beginReplayTick();
}

void NetSession::endTick(Scene& scene, uint32_t tick) {
    // A client learns the server's tick from snapshot headers only.
    if (isAuthority(m_role)) m_core.serverTick = tick;

    if (m_role == NetRole::Server)      m_server.endTick(scene, tick);
    else if (m_role == NetRole::Client) m_client.endTick(scene, tick);
}

NetSession::Drawn::Drawn(NetSession& session, Scene& scene, float deltaTime)
    : m_session(session)
    , m_scene(scene)
{
    if (m_session.m_role == NetRole::Client) m_session.m_client.applyDrawCorrection(m_scene, deltaTime);
}

NetSession::Drawn::~Drawn() {
    if (m_session.m_role == NetRole::Client) m_session.m_client.removeDrawCorrection(m_scene);
}

void NetSession::interpolate(Scene& scene, float deltaTime) {
    if (m_role != NetRole::Client) return;
    PROFILE_SCOPE("NetSession::interpolate");
    m_client.interpolate(scene, deltaTime);
}

float NetSession::renderDelay() const {
    return m_role == NetRole::Client ? m_client.renderDelay() : 0.0f;
}

void NetSession::advance(float seconds) {
    m_sendAccumulator += seconds;
    m_server.advance(seconds);
    m_client.advance(seconds);
}

void NetSession::forgetWorld() {
    m_client.forgetWorld();
    m_server.forgetWorld();
}

void NetSession::receive(Scene& scene, ResourceManager& resources) {
    if (m_role == NetRole::Offline) return;
    PROFILE_SCOPE("NetSession::receive");

    if (scene.epoch() != m_worldEpoch) {
        if (m_worldEpoch != 0) {
            LOG_WARNING(
                "The world was replaced while a session was live; "
                "what this end remembered about it is dropped"
            );
            forgetWorld();
        }
        m_worldEpoch = scene.epoch();
    }

    if (m_role == NetRole::Server) {
        m_server.receive(scene, resources);
    } else if (!m_client.receive(scene, resources)) {
        disconnect();
    }
}

void NetSession::send(Scene& scene, uint32_t tick) {
    if (m_role == NetRole::Offline) return;
    PROFILE_SCOPE("NetSession::send");

    const float rate = m_role == NetRole::Server ? NET_SNAPSHOT_RATE : NET_COMMAND_RATE;
    const float interval = 1.0f / rate;
    if (m_sendAccumulator < interval) return;

    // Carried over so a long frame does not drift slow; capped so a stall sends
    // once rather than a burst.
    m_sendAccumulator = std::min(m_sendAccumulator - interval, interval);

    if (m_role == NetRole::Server) m_server.send(scene, tick);
    else                           m_client.send();
}

EntityId NetSession::spawn(
    Scene& scene,
    ResourceManager& resources,
    const std::string& prefab,
    const Transform& at
) {
    if (!isAuthority(m_role)) {
        LOG_ERROR("A client asked to spawn '%s'; what exists is the server's to decide", prefab.c_str());
        return EntityId{};
    }

    // Refused before building: a root no client can be told of splits the world.
    NetSpawn said{prefab, at, {}};
    if (m_role == NetRole::Server && !isSayable(said)) {
        LOG_ERROR(
            "Cannot spawn '%s': a client can be told a relative .json path of at "
                "most %zu characters inside the project, and a finite pose with a "
                "rotation and a scale within %.0f",
            prefab.c_str(),
            NET_PREFAB_PATH_MAX,
            static_cast<double>(NET_MAX_SCALE)
        );
        return EntityId{};
    }

    const EntityId root = Prefab::instantiate(scene, resources, prefab, at);
    if (!root) {
        LOG_ERROR("Could not build '%s'", prefab.c_str());
        return EntityId{};
    }
    if (m_role != NetRole::Server) return root;

    // So every client builds each entity in the same slot.
    said.slots = Prefab::builtSlotsOf(scene, root);
    if (said.slots.size() > NET_SPAWN_MAX_SLOTS) {
        LOG_WARNING(
            "'%s' builds %zu entities, past the %zu a spawn names the slots of; a client builds it "
                "wherever its own slots are free",
            prefab.c_str(),
            said.slots.size(),
            NET_SPAWN_MAX_SLOTS
        );
        said.slots.clear();
    }
    scene.add(root, std::move(said));
    return root;
}

NetSession::Rewind::Rewind(NetSession& session, Scene& scene, EntityId shooter)
    : m_session(session)
    , m_scene(scene)
{
    m_session.beginRewind(m_scene, shooter);
}

NetSession::Rewind::~Rewind() {
    m_session.endRewind(m_scene);
}

void NetSession::beginRewind(Scene& scene, EntityId shooter) {
    // Nested: starting again would save the rewound poses as the present.
    if (m_rewindDepth++ > 0) {
        VKM_ASSERT(
            shooter == m_rewoundFor,
            "a rewind scope opened inside another for a "
                "different shooter; the world can be at one moment at a time"
        );
        return;
    }
    m_rewoundFor = shooter;
    if (m_role == NetRole::Server) m_server.rewind(scene, shooter);
}

void NetSession::endRewind(Scene& scene) {
    if (m_rewindDepth == 0 || --m_rewindDepth > 0) return;
    m_server.restore(scene);
}

std::string NetSession::describe() const {
    if (m_role == NetRole::Offline) return "offline";
    if (m_role == NetRole::Server) return m_server.describe();

    if (m_role == NetRole::Disconnected) {
        char line[320];
        std::snprintf(
            line,
            sizeof(line),
            "disconnected - %s; the world is holding still",
            m_core.lastError.empty() ? "no reason given" : m_core.lastError.c_str()
        );
        return line;
    }
    return m_client.describe();
}

} // namespace Vkm::Engine

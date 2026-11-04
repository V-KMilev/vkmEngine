#pragma once

// A networked game in one process: a server and a client, each a Game, that load the same world, play
// the same characters and take the same keys.

#include <cstdint>
#include <vector>


#include "ecs/component/physics/character_controller.h"
#include "net/wire/codecs.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

#include "harness/lockstep.h"
#include "harness/world.h"
#include "physics/bodies.h"

namespace Vkm::Test {

/**
 * @brief A player's character: walks by its command's Move actions and jumps by Jump, wherever it runs.
 */
class Walker : public Behavior<Walker> {
    public:
        void onFixedUpdate(float) override {
            if (!isSimulated()) return;
            // A client's own runs are its prediction and replays; the server's are what counts.
            if (!isMine() && pressed("Jump")) ++serverPresses;
            CharacterController& walker = get<CharacterController>();
            walker.moveInput     = glm::vec3(axis("Move/Right"), 0.0f, -axis("Move/Forward")) * 4.0f;
            walker.jumpRequested = pressed("Jump");
        }

        static inline int serverPresses = 0;  ///< Jump presses the server ran, any character's
};

constexpr uint32_t TICK_RATE = 60;

/// The keys every end of a test game takes.
inline void bindKeys(InputMap& input) {
    input.setActions({
        {"Move/Forward", {InputBinding::key(Platform::Key::W), InputBinding::key(Platform::Key::S, -1.0f)}},
        {"Move/Right", {InputBinding::key(Platform::Key::D), InputBinding::key(Platform::Key::A, -1.0f)}},
        {"Jump", {InputBinding::key(Platform::Key::Space)}},
    });
}

/// A standing character with a Walker, at @p at.
inline EntityId addWalker(Scene& scene, const glm::vec3& at) {
    const EntityId id = addBody(scene, at + glm::vec3(0.0f, 0.9f, 0.0f), capsule(0.3f, 0.6f));
    Rigidbody& body   = bodyOf(scene, id);
    body.mass           = 70.0f;
    body.freezeRotation = true;
    body.canSleep       = false;
    scene.add(id, CharacterController{});
    addBehavior<Walker>(scene, id);
    return id;
}

/**
 * @brief A server with a client joined to it, both on the same world: a floor, a row of crates, and two
 *        characters, which the first two players to join take.
 *
 * Every end closes its session as its run ends, so a test reads a session on its last frame.
 */
struct Match {
    Game                  server;
    Game                  client;
    Game                  second;   // a second client, for the tests that need two players
    std::vector<EntityId> crates;   // the server's; the clients hold the same slots
    std::vector<EntityId> walkers;  // the same: the first player's at x = -2, the second's at x = -4

    /// The world as every end loads it: a character is scene content, so each end has its collider.
    void lay(Game& game, uint64_t fingerprint) {
        Scene& scene = game.scene();
        addFloor(scene);
        crates.clear();
        for (int i = 0; i < 3; ++i) crates.push_back(addCrate(scene, {2.0f + 1.5f * i, 0.5f, -4.0f}));
        walkers = {addWalker(scene, {-2.0f, 0.0f, 0.0f}), addWalker(scene, {-4.0f, 0.0f, 0.0f})};
        bindKeys(game.engine.getInput());
        game.engine.getNet().setWorld(fingerprint, scene);
    }

    /**
     * @param clientFingerprint What the client's world is, as its file hashes; the server's is 1.
     */
    explicit Match(uint64_t clientFingerprint = 1, uint32_t clientTickRate = TICK_RATE) {
        registerEngineNetTypes();
        lay(server, 1);
        lay(client, clientFingerprint);
        lay(second, 1);
        NetSession& net = server.engine.getNet();
        net.onSpawn(
            [this, &net](Scene&, ResourceManager&, PlayerId) {
                for (const EntityId walker : walkers) {
                    if (!net.isDriven(walker)) return walker;
                }
                return EntityId{};
            },
            [](Scene&, ResourceManager&, PlayerId, EntityId) {}
        );
        REQUIRE(server.host(0, 4, TICK_RATE));
        port       = net.localAddress().port;
        clientTick = clientTickRate;
    }

    /// Join, directly or through @p relay, unless @p join is false, and run @p frames frames.
    void play(
        int frames,
        const std::function<void(int)>& each = {},
        Relay* relay = nullptr,
        bool join = true
    ) {
        if (join && !joined) {
            REQUIRE(client.join(relay ? relay->address() : loopback(port), clientTick));
            joined = true;
        }
        runLockstep(server, client, frames, each, relay);
    }

    /// Join both clients, the second at @p secondJoins, and run @p frames frames of all three.
    void playTwo(int frames, const std::function<void(int)>& each, int secondJoins = 0) {
        REQUIRE(client.join(loopback(port), clientTick));
        joined = true;
        runLockstep({&server, &client, &second}, frames, [&](int frame) {
            if (frame == secondJoins) REQUIRE(second.join(loopback(port), TICK_RATE));
            if (each) each(frame);
        });
    }

    /// Hold @p key down on the client from now on, or let it go.
    void hold(Platform::Key key, bool down = true, Game* on = nullptr) {
        (on ? on : &client)->engine.getWindow().getInputHandle().onKeyEvent(key, down);
    }

    NetSession& serverNet() { return server.engine.getNet(); }
    NetSession& clientNet() { return client.engine.getNet(); }

    uint16_t port       = 0;
    uint32_t clientTick = TICK_RATE;
    bool     joined     = false;
};

} // namespace Vkm::Test

VKM_BEHAVIOR(::Vkm::Test::Walker)
VKM_REFLECT_END()

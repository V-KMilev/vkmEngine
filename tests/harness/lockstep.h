#pragma once

// A server and a client in one process: two Games, each on a thread of its own, taking turns a frame
// at a time, server first. Only one runs at once, and a datagram on loopback is waiting before
// sendto returns, so every packet crosses at the same turn every run.

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "vkm_network/address.h"
#include "vkm_network/udp_socket.h"

#include "harness/world.h"

namespace Vkm::Test {

/**
 * @brief A link between a client and a server that loses, holds or damages datagrams on a fixed
 *        pattern, pumped at every turn of a lockstep run.
 *
 * The client connects to address(); the server sees the relay's back socket as the client.
 */
class Relay {
    public:
        /**
         * @brief What happens to one datagram: false drops it; it may change the bytes.
         *
         * @param index Datagrams before it the same way, from 0.
         * @param up    True from client to server.
         * @param bytes The datagram.
         */
        using Rule = std::function<bool(size_t index, bool up, std::vector<uint8_t>& bytes)>;

        explicit Relay(uint16_t serverPort, Rule rule = {}, int holdTurns = 0);

        /// Where the client connects.
        Network::Address address() const;

        /// Pass on what has arrived and what is due, each by the rule.
        void pump();

        /// Stop passing anything, either way: the link is cut.
        void cut() { m_cut = true; }

        /// Pass every datagram from the client twice.
        void duplicateUp(bool twice) { m_twice = twice; }

        size_t passed() const { return m_passed; }

    private:
        struct Held {
            int                  due = 0;
            bool                 up  = false;
            std::vector<uint8_t> bytes;
        };

    private:
        Network::UdpSocket m_front;
        Network::UdpSocket m_back;
        Network::Address   m_server;
        Network::Address   m_client;
        Rule               m_rule;
        int                m_hold     = 0;
        int                m_turn     = 0;
        size_t             m_index[2] = {0, 0};
        size_t             m_passed   = 0;
        bool               m_cut      = false;
        bool               m_twice    = false;
        std::deque<Held>   m_held;
};

/**
 * @brief Run @p server and @p client a frame each in turn for @p frames frames, one tick a frame.
 *
 * @param server The Game hosting; host() was called on its session.
 * @param client The Game joining; connect() was called on its session.
 * @param frames Frames each runs.
 * @param each   Called after both ran frame k, with k from 1, while neither runs.
 * @param relay  Pumped at every turn, when the client talks through one.
 */
void runLockstep(
    Game& server,
    Game& client,
    int frames,
    const std::function<void(int frame)>& each = {},
    Relay* relay = nullptr
);

/// The same for a server and several clients, @p ends[0] the server, each taking its turn in order.
void runLockstep(
    const std::vector<Game*>& ends,
    int frames,
    const std::function<void(int frame)>& each = {},
    Relay* relay = nullptr
);

/// Where a client reaches a server on this machine.
Network::Address loopback(uint16_t port);

} // namespace Vkm::Test

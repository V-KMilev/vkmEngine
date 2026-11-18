#pragma once

#include <cstdint>
#include <string>

#include "vkm_network/bit_stream.h"

namespace Vkm::Engine {

// What a message carries past its header (writeMessageHeader), each layout written and read in one place
// so the two ends cannot disagree on it, and read only at this build's protocol version. A Challenge's echo
// and a Refuse's reason byte, which keep their place right after the header, are not among them:
// NetServer::challenge and NetServer::refuse write them, and NetClient::receive and NetClient::onRefuse
// read them.

/**
 * @brief A Hello's: what a client is, so the server can refuse a build or a world that differs.
 */
struct HelloBody {
    uint32_t schema      = 0;  ///< NetSchema::fingerprint(): what this build replicates.
    uint8_t  actions     = 0;  ///< The project's action count.
    uint64_t actionNames = 0;  ///< InputMap::actionFingerprint() of those actions.
    uint64_t world       = 0;  ///< The fingerprint of the world the client loaded.
    uint16_t tickRate    = 0;  ///< Ticks a second the client runs at.

    /**
     * @brief Write it after a Hello's header.
     */
    void write(Network::BitWriter& out) const;

    /**
     * @brief Read what write() wrote; the reader fails on a body cut short.
     */
    void read(Network::BitReader& in);
};

/**
 * @brief A Welcome's: the seat a client was given.
 */
struct WelcomeBody {
    uint16_t player          = 0;      ///< The client's PlayerId; never NO_PLAYER from a server.
    uint32_t slot            = 0;      ///< The entity slot it drives, in the server's world.
    uint8_t  worldGeneration = 0;      ///< The server's world now.
    bool     current         = false;  ///< Whether the client already holds that world; if not, a World
                                       ///< message names it.

    /**
     * @brief Write it after a Welcome's header.
     */
    void write(Network::BitWriter& out) const;

    /**
     * @brief Read what write() wrote; the reader fails on a body cut short.
     */
    void read(Network::BitReader& in);
};

/**
 * @brief A World's: the scene the server moved to, which a client loads.
 */
struct WorldBody {
    uint8_t     generation  = 0;  ///< Which world it is, counted by the session.
    uint64_t    fingerprint = 0;  ///< The scene's, so the client can tell it loaded the same one.
    uint32_t    slot        = 0;  ///< The entity slot the client drives in it.
    std::string path;             ///< The scene, project-relative (writeProjectPath).

    /**
     * @brief Write it after a World's header.
     */
    void write(Network::BitWriter& out) const;

    /**
     * @brief Read what write() wrote; the reader fails on a body cut short.
     */
    void read(Network::BitReader& in);
};

/**
 * @brief What a Snapshot opens with, before the world's changes.
 */
struct SnapshotHeader {
    uint8_t  worldGeneration = 0;  ///< The world it describes, so one sent before a change is not read
                                   ///< into the next.
    uint32_t serverTick      = 0;  ///< The tick it is of.
    uint32_t confirmedTick   = 0;  ///< The client's newest command run, which it reconciles against.
    uint32_t heard           = 0;  ///< The newest command sequence arrived, run or not, so the client
                                   ///< stops resending it.
    uint32_t lowWater        = 0;  ///< How low the client's command queue ran since the last snapshot,
                                   ///< for its pacing; at most NET_QUEUE_DEPTH_MAX.
    uint16_t messages        = 0;  ///< The newest of the client's messages handed on, so it stops
                                   ///< sending it.

    /**
     * @brief Write it after a Snapshot's header.
     */
    void write(Network::BitWriter& out) const;

    /**
     * @brief Read what write() wrote; the reader fails on a header cut short.
     */
    void read(Network::BitReader& in);
};

/**
 * @brief What a Command packet opens with, before its commands (NET_COMMAND_PREAMBLE_BITS).
 */
struct CommandPreamble {
    uint8_t worldGeneration = 0;    ///< The world the client holds, which the server waits on after a
                                    ///< change.
    double  drawnTick       = 0.0;  ///< The server tick the client was drawing, a fraction past the
                                    ///< whole in parts of NET_DRAWN_FRACTION_MAX: when it saw what it
                                    ///< aimed at, for the server's rewind.

    /**
     * @brief Write it after a Command's header; a tick before zero is written as zero.
     */
    void write(Network::BitWriter& out) const;

    /**
     * @brief Read what write() wrote; the reader fails on a preamble cut short.
     */
    void read(Network::BitReader& in);
};

} // namespace Vkm::Engine

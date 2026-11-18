#include "harness/lockstep.h"

#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#include "harness/check.h"

namespace Vkm::Test {

namespace {

/**
 * @brief Whose turn it is.
 *
 * Only the holder's engine runs.
 */
struct Baton {
    std::mutex              mutex;
    std::condition_variable changed;
    int                     holder = 0;      // whose turn: the server is 0, the clients after it
    bool                    over   = false;  // one end stopped short: the other stops too

    void end() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            over = true;
        }
        changed.notify_all();
    }

    void pass(int to) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            holder = to;
        }
        changed.notify_all();
    }

    void await(int who) {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return holder == who || over; });
    }
};

/**
 * @brief Last in each engine's frame: asks for the next tick, then gives the other end its turn.
 */
class Turn : public System {
    public:
        Turn(Baton& baton, int self, int ends, int frames, std::function<void(int)> afterAll, Relay* relay)
            : m_baton(baton)
            , m_self(self)
            , m_ends(ends)
            , m_frames(frames)
            , m_afterAll(std::move(afterAll))
            , m_relay(relay)
        {}

        void update(FrameContext& ctx) override {
            ++m_frame;
            // One frame past the test's, in which the ends close in turn: the server's goodbye is
            // never what a test's last look sees.
            const bool last  = m_frame > m_frames;
            const bool final = m_self == m_ends - 1;
            if (final && m_afterAll && !last) m_afterAll(m_frame);
            if (last) ctx.window.requestClose();
            else ctx.clock.requestStep(1);
            if (m_relay) m_relay->pump();
            // A last turn ends with its run, which passes the baton itself (runLockstep).
            if (last) return;
            m_baton.pass(final ? 0 : m_self + 1);
            m_baton.await(m_self);
            if (m_baton.over) ctx.window.requestClose();
            if (m_relay) m_relay->pump();
        }

    private:
        Baton&                   m_baton;
        int                      m_self   = 0;
        int                      m_ends   = 0;
        int                      m_frames = 0;
        int                      m_frame  = 0;
        std::function<void(int)> m_afterAll;
        Relay*                   m_relay  = nullptr;
};

} // namespace

Network::Address loopback(uint16_t port) { return Network::Address{0x7F000001u, port}; }

Relay::Relay(uint16_t serverPort, Rule rule, int holdTurns)
    : m_server(loopback(serverPort))
    , m_rule(std::move(rule))
    , m_hold(holdTurns)
{
    REQUIRE(m_front.open(0));
    REQUIRE(m_back.open(0));
}

Network::Address Relay::address() const { return loopback(m_front.localAddress().port); }

void Relay::pump() {
    ++m_turn;
    std::vector<uint8_t> bytes;
    Network::Address from;
    const auto take = [&](Network::UdpSocket& socket, bool up) {
        while (socket.receive(bytes, from)) {
            if (up) m_client = from;
            if (m_cut) continue;
            const size_t index = m_index[up]++;
            if (m_rule && !m_rule(index, up, bytes)) continue;
            m_held.push_back({m_turn + m_hold, up, bytes});
            if (up && m_twice) m_held.push_back({m_turn + m_hold, up, bytes});
        }
    };
    take(m_front, true);
    take(m_back, false);
    while (!m_held.empty() && m_held.front().due <= m_turn) {
        const Held& held = m_held.front();
        if (held.up) m_back.send(m_server, held.bytes.data(), held.bytes.size());
        else if (m_client) m_front.send(m_client, held.bytes.data(), held.bytes.size());
        ++m_passed;
        m_held.pop_front();
    }
}

void runLockstep(
    const std::vector<Game*>& ends,
    int frames,
    const std::function<void(int frame)>& each,
    Relay* relay
) {
    Baton baton;
    const int count = static_cast<int>(ends.size());
    for (int i = 0; i < count; ++i) {
        ends[i]->engine.addSystem<Turn>(
            SystemStage::Editor,
            baton,
            i,
            count,
            frames,
            i == count - 1 ? each : nullptr,
            relay
        );
        ends[i]->clock().setPaused(true);
        ends[i]->clock().requestStep(1);
    }

    // Each runs on its turn; on its last frame it runs to the end before the next takes its last
    // turn, so no two engines ever run at once. A check that stops the test (REQUIRE) on any thread
    // ends every run; it is rethrown here, once all have stopped.
    std::vector<std::exception_ptr> failed(ends.size());
    std::vector<std::thread> threads;
    for (int i = 0; i < count; ++i) {
        threads.emplace_back([&, i] {
            baton.await(i);
            try {
                ends[i]->engine.run();
            } catch (...) {
                failed[i] = std::current_exception();
                baton.end();
            }
            baton.pass((i + 1) % count);
        });
    }
    for (std::thread& thread : threads) thread.join();
    for (const std::exception_ptr& error : failed) {
        if (error) std::rethrow_exception(error);
    }
}

void runLockstep(
    Game& server,
    Game& client,
    int frames,
    const std::function<void(int frame)>& each,
    Relay* relay
) {
    runLockstep({&server, &client}, frames, each, relay);
}

} // namespace Vkm::Test

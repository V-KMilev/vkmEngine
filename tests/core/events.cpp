// The event bus: when a listener hears an event, and which listeners hear it.

#include <stdexcept>
#include <string>
#include <vector>

#include "base/event/event_bus.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

struct Ping {
    int value = 0;
};

struct Pong {
    int value = 0;
};

} // namespace

TEST("an event emitted is heard at once, and one enqueued waits for the flush, in order") {
    EventBus bus;
    std::vector<int> heard;
    bus.subscribe<Ping>([&](const Ping& ping) {
        heard.push_back(ping.value);
        // What a listener enqueues goes out with the next flush, not this one.
        if (ping.value == 2) bus.enqueue(Ping{9});
    });

    bus.emit(Ping{1});
    CHECK(heard == std::vector<int>{1});

    bus.enqueue(Ping{2});
    bus.enqueue(Ping{3});
    CHECK(heard.size() == 1u);
    bus.flush();
    CHECK(heard == std::vector<int>{1, 2, 3});
    bus.flush();
    CHECK(heard == std::vector<int>{1, 2, 3, 9});
    bus.flush();
    CHECK(heard.size() == 4u);
}

TEST("a listener dropped while an event is delivered is not called again, and one added hears the next") {
    EventBus bus;
    std::vector<std::string> heard;
    ListenerId second = 0;
    ListenerId late   = 0;
    bus.subscribe<Ping>([&](const Ping&) {
        heard.push_back("first");
        bus.unsubscribe<Ping>(second);
        if (!late) late = bus.subscribe<Ping>([&](const Ping&) { heard.push_back("late"); });
    });
    second = bus.subscribe<Ping>([&](const Ping&) { heard.push_back("second"); });

    bus.emit(Ping{});
    CHECK(heard == std::vector<std::string>{"first"});
    bus.emit(Ping{});
    CHECK(heard == std::vector<std::string>{"first", "first", "late"});

    // Gone for good, and only the one it named.
    CHECK(!bus.unsubscribe<Ping>(second));
    CHECK(bus.unsubscribe<Ping>(late));
    bus.emit(Ping{});
    CHECK(heard.size() == 4u);
}

TEST("a listener that throws stops that delivery, and the bus goes on working") {
    EventBus bus;
    int pongs = 0;
    int pings = 0;
    bus.subscribe<Ping>([&](const Ping& ping) {
        if (ping.value < 0) throw std::runtime_error("bad ping");
        ++pings;
    });
    bus.subscribe<Pong>([&](const Pong&) { ++pongs; });

    bool threw = false;
    try {
        bus.emit(Ping{-1});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // A listener added after it is heard from as one added before it.
    int later = 0;
    bus.subscribe<Ping>([&](const Ping&) { ++later; });
    bus.emit(Ping{1});
    bus.enqueue(Pong{});
    bus.flush();
    CHECK(pings == 1);
    CHECK(later == 1);
    CHECK(pongs == 1);
}

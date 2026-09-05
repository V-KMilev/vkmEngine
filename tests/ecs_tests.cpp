#include "support.h"

#include "core/memory/slot_allocator.h"

namespace {

// A handle is only as good as its generation. These pin the two ways that can
// go wrong: a stale handle that still matches, and a slot handed to two owners.
void testAFreedHandleNeverComesBackToLife() {
    std::printf("What a handle means after the slot under it is freed:\n");

    SlotAllocator slots;
    const StorageIndex first  = slots.allocate();
    const StorageIndex second = slots.allocate();

    check("slot zero is never handed out", first.index != 0 && second.index != 0);
    check("  and two allocations are two slots", first.index != second.index);
    check("  both alive", slots.has(first) && slots.has(second));
    check("  and counted", slots.size() == 2);

    const StorageIndex stale = first;
    slots.free(first);
    check("a freed handle is not alive", !slots.has(stale));
    check("  and the count says so", slots.size() == 1);

    // The slot itself comes back; the handle to it must not. This is the whole
    // reason a handle carries a generation.
    const StorageIndex reused = slots.allocate();
    check("the slot is recycled", reused.index == stale.index);
    check("  under a new generation", reused.generation != stale.generation);
    check("  so the old handle stays dead", !slots.has(stale));
    check("  while the new one lives", slots.has(reused));
    check("  and they are not the same handle", !(reused == stale));
}

// The defect this pins is silent: two owners of one slot, each certain it is
// theirs, and nothing reports anything until their data has been interleaved.
void testASlotClaimedByIndexIsNotHandedOutAgain() {
    std::printf("A slot taken by index while it sat on the free list:\n");

    SlotAllocator slots;
    const StorageIndex first  = slots.allocate();
    const StorageIndex second = slots.allocate();
    const StorageIndex third  = slots.allocate();
    (void)second;

    // Freed in this order on purpose: the list is last-in-first-out, so the
    // slot claimed below has to be the one the next allocation would reach for.
    slots.free(third);
    slots.free(first);
    check("two slots are free", slots.size() == 1);

    // Deserialization asks for a named slot, and that slot is on the free list.
    // Nothing removes it from there, so the list now names a live slot.
    const StorageIndex claimed = slots.allocateAt(first.index);
    check("a freed slot can be claimed by index", slots.has(claimed));
    check("  at the index asked for", claimed.index == first.index);

    // The next ordinary allocation must not be handed the slot that was just
    // claimed, however the free list is ordered.
    const StorageIndex next = slots.allocate();
    check("the next allocation is not the claimed slot", next.index != claimed.index);
    check("  and both are alive at once", slots.has(claimed) && slots.has(next));
    check("  with the count agreeing", slots.size() == 3);
}

void testAllocatingPastTheEndFillsTheGap() {
    std::printf("A slot claimed far past the end:\n");

    SlotAllocator slots;
    const StorageIndex high = slots.allocateAt(6);
    check("the far slot is alive", slots.has(high) && high.index == 6);
    check("  and it is the only live one", slots.size() == 1);

    // Everything skipped over is free rather than lost, or a scene that names
    // one high slot would strand every index below it for the session.
    std::vector<uint32_t> handed;
    for (int i = 0; i < 5; ++i) handed.push_back(slots.allocate().index);
    check("the skipped slots are handed out, not stranded", handed.size() == 5);

    bool reusedTheClaimed = false;
    for (const uint32_t idx : handed) reusedTheClaimed |= (idx == 6);
    check("  and never the one already claimed", !reusedTheClaimed);
    check("  every one below it", *std::max_element(handed.begin(), handed.end()) < 6);
}

void testForEachVisitsTheLivingOnly() {
    std::printf("What a walk over the allocator sees:\n");

    SlotAllocator slots;
    std::vector<StorageIndex> made;
    for (int i = 0; i < 5; ++i) made.push_back(slots.allocate());
    slots.free(made[1]);
    slots.free(made[3]);

    std::vector<uint32_t> seen;
    slots.forEach([&](uint32_t index) { seen.push_back(index); });

    check("a walk sees exactly the living", seen.size() == 3);
    check("  never slot zero", std::find(seen.begin(), seen.end(), 0u) == seen.end());
    check("  never a freed one",
          std::find(seen.begin(), seen.end(), made[1].index) == seen.end());
    check("  ascending by index", std::is_sorted(seen.begin(), seen.end()));

    slots.clear();
    check("clear empties it", slots.size() == 0);
    check("  and every handle from before is dead", !slots.has(made[0]));
}

struct Ping { int value = 0; };
struct Pong { int value = 0; };

void testAQueuedEventIsDeliveredOnceAndThenGone() {
    std::printf("What a flush delivers, and what it leaves behind:\n");

    EventBus bus;
    std::vector<int> heard;
    bus.subscribe<Ping>([&](const Ping& e) { heard.push_back(e.value); });

    bus.enqueue(Ping{1});
    bus.enqueue(Ping{2});
    check("nothing is delivered before the flush", heard.empty());

    bus.flush();
    check("both arrive", heard.size() == 2);
    check("  in the order they were queued", heard[0] == 1 && heard[1] == 2);

    // Drained, not merely read: a queue that kept its contents would deliver
    // every event again on every frame for the life of the process.
    bus.flush();
    check("a second flush delivers nothing", heard.size() == 2);
}

void testAnEventQueuedDuringAFlushWaitsForTheNext() {
    std::printf("An event queued by a listener that is mid-flush:\n");

    EventBus bus;
    std::vector<int> order;

    // The reentrant case: a listener reacting by queueing more work. Delivered
    // inside this flush it would be an unbounded loop for a listener that
    // answers itself, and a caller cannot see it coming.
    bus.subscribe<Ping>([&](const Ping& e) {
        order.push_back(e.value);
        if (e.value < 3) bus.enqueue(Ping{e.value + 1});
    });

    bus.enqueue(Ping{1});
    bus.flush();
    check("the flush delivers only what it started with", order.size() == 1);

    bus.flush();
    check("what the listener queued arrives on the next one", order.size() == 2);
    check("  and in order", order[0] == 1 && order[1] == 2);
}

void testAListenerStopsBeingCalledOnceItIsGone() {
    std::printf("A listener that unsubscribes:\n");

    EventBus bus;
    int first = 0;
    int second = 0;
    const ListenerId id = bus.subscribe<Pong>([&](const Pong&) { ++first; });
    bus.subscribe<Pong>([&](const Pong&) { ++second; });

    bus.enqueue(Pong{});
    bus.flush();
    check("both listeners hear it", first == 1 && second == 1);

    check("unsubscribing reports it found the listener", bus.unsubscribe<Pong>(id));
    bus.enqueue(Pong{});
    bus.flush();
    check("the one that left hears nothing more", first == 1);
    check("  and the other still does", second == 2);

    check("unsubscribing twice reports nothing to remove", !bus.unsubscribe<Pong>(id));
}

void testEachEventTypeHasItsOwnQueue() {
    std::printf("Two event types through one bus:\n");

    EventBus bus;
    int pings = 0;
    int pongs = 0;
    bus.subscribe<Ping>([&](const Ping&) { ++pings; });
    bus.subscribe<Pong>([&](const Pong&) { ++pongs; });

    bus.enqueue(Ping{});
    bus.enqueue(Ping{});
    bus.enqueue(Pong{});
    bus.flush();

    check("each listener hears only its own type", pings == 2 && pongs == 1);
}

// A one-shot listener retires itself the moment it fires. The walk holds an
// index into the listener vector, so the entry has to be emptied rather than
// erased - erasing under the walk skips whoever moves into the freed slot.
void testAListenerThatRetiresItself() {
    std::printf("A listener that unsubscribes from inside its own callback:\n");

    EventBus bus;
    int firstHeard = 0;
    int secondHeard = 0;

    ListenerId first = 0;
    first = bus.subscribe<Pong>([&](const Pong&) {
        ++firstHeard;
        bus.unsubscribe<Pong>(first);
    });
    bus.subscribe<Pong>([&](const Pong&) { ++secondHeard; });

    bus.enqueue(Pong{});
    bus.flush();
    check("both listeners hear the first event", firstHeard == 1 && secondHeard == 1);

    bus.enqueue(Pong{});
    bus.flush();
    check("the one that retired itself hears no more", firstHeard == 1);
    // The listener that stayed is the assertion that matters: an erase under the
    // walk would have moved it into the freed slot and skipped it.
    check("and the one that stayed still hears",       secondHeard == 2);

    bus.enqueue(Pong{});
    bus.flush();
    check("and goes on hearing",                       secondHeard == 3);
}

// A listener that subscribes during a dispatch is waiting in the pending list
// rather than the live one, and a remove that only looked at the live list said
// "nothing to remove" and then admitted it anyway - a subscription nobody could
// cancel.
void testAListenerRemovedBeforeItIsEverAdmitted() {
    std::printf("A listener subscribed and removed inside the same dispatch:\n");

    EventBus bus;
    int lateHeard = 0;
    ListenerId late = 0;
    bool removed = false;

    bus.subscribe<Pong>([&](const Pong&) {
        if (late) return;
        late    = bus.subscribe<Pong>([&](const Pong&) { ++lateHeard; });
        removed = bus.unsubscribe<Pong>(late);
    });

    bus.enqueue(Pong{});
    bus.flush();
    check("removing it reports it was found", removed);

    bus.enqueue(Pong{});
    bus.flush();
    check("and it is never called", lateHeard == 0);
}

} // namespace

void runEcsTests() {
    testAListenerThatRetiresItself();
    testAListenerRemovedBeforeItIsEverAdmitted();
    testAFreedHandleNeverComesBackToLife();
    testASlotClaimedByIndexIsNotHandedOutAgain();
    testAllocatingPastTheEndFillsTheGap();
    testForEachVisitsTheLivingOnly();
    testAQueuedEventIsDeliveredOnceAndThenGone();
    testAnEventQueuedDuringAFlushWaitsForTheNext();
    testAListenerStopsBeingCalledOnceItIsGone();
    testEachEventTypeHasItsOwnQueue();
}

#include "support.h"

#include "core/memory/slot_allocator.h"

namespace {

struct TypeIdProbe { int value = 0; };

} // namespace

// Twin of core_tests.cpp's, outside the anonymous namespace, so only the linkage of
// the const holding it makes it this file's own.
const auto TYPE_ID_OF_CONSTANTS_PROBE = [] {
    struct Probe { int value = 0; };
    return typeId<Probe>();
};

namespace {

// A TypeRegistry - a scene's component sets among them - is indexed by this id, so
// two types sharing one are two components in one set. The id is keyed on the type's
// name to outlive the module that first asked; an anonymous-namespace name is the
// same in every file and must not make two types one.
void testATypeIdIsTheTypesAlone() {
    std::printf("What a type id tells apart:\n");

    check("the same type asked twice is one id", typeId<StorageIndex>() == typeId<StorageIndex>());
    check("  and two types are two ids", typeId<StorageIndex>() != typeId<GenerationIndex>());
    check("  a type and a pointer to it are two ids", typeId<StorageIndex>() != typeId<StorageIndex*>());
    check(
        "an anonymous type in another file, spelled the same, is another id",
        typeIdOfCoreTestsProbe() != typeId<TypeIdProbe>()
    );
    check(
        "  and each keeps its own",
        typeIdOfCoreTestsProbe() == typeIdOfCoreTestsProbe() && typeId<TypeIdProbe>() == typeId<TypeIdProbe>()
    );
    check(
        "a class local to a lambda a const holds, in another file, is another id",
        typeIdOfCoreTestsConstantsProbe() != TYPE_ID_OF_CONSTANTS_PROBE()
    );
    check(
        "  each still one id however often it is asked",
        typeIdOfCoreTestsConstantsProbe() == typeIdOfCoreTestsConstantsProbe()
            && TYPE_ID_OF_CONSTANTS_PROBE() == TYPE_ID_OF_CONSTANTS_PROBE()
    );
}

// The two ways a generation can fail: a stale handle that still matches, and a slot
// handed to two owners.
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

    // The slot comes back; the handle to it must not - why a handle carries a generation.
    const StorageIndex reused = slots.allocate();
    check("the slot is recycled", reused.index == stale.index);
    check("  under a new generation", reused.generation != stale.generation);
    check("  so the old handle stays dead", !slots.has(stale));
    check("  while the new one lives", slots.has(reused));
    check("  and they are not the same handle", !(reused == stale));
}

// Silent when wrong: two owners of one slot, nothing reporting until their data interleaves.
void testASlotClaimedByIndexIsNotHandedOutAgain() {
    std::printf("A slot taken by index while it sat on the free list:\n");

    SlotAllocator slots;
    const StorageIndex first  = slots.allocate();
    const StorageIndex second = slots.allocate();
    const StorageIndex third  = slots.allocate();
    (void)second;

    // Freed in this order on purpose: the list is LIFO, so the slot claimed below is
    // the one the next allocation would reach for.
    slots.free(third);
    slots.free(first);
    check("two slots are free", slots.size() == 1);

    // Deserialization claims a named slot that is on the free list, and nothing
    // removes it from there.
    const StorageIndex claimed = slots.allocateAt(first.index);
    check("a freed slot can be claimed by index", slots.has(claimed));
    check("  at the index asked for", claimed.index == first.index);

    // The next allocation must not get the claimed slot, however the free list is ordered.
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

    // Skipped slots are free, not lost, or one high slot in a scene would strand every
    // index below it for the session.
    std::vector<uint32_t> handed;
    for (int i = 0; i < 5; ++i) handed.push_back(slots.allocate().index);
    check("the skipped slots are handed out, not stranded", handed.size() == 5);

    bool reusedTheClaimed = false;
    for (const uint32_t idx : handed) reusedTheClaimed |= (idx == 6);
    check("  and never the one already claimed", !reusedTheClaimed);
    check("  every one below it", *std::max_element(handed.begin(), handed.end()) < 6);
    check(
        "  lowest first, as a new allocator hands them out",
        handed == std::vector<uint32_t>({1, 2, 3, 4, 5})
    );
}

// A cleared allocator is File > New's: the next entities sit where a fresh one would
// put them, not at the top of the last scene's table.
void testAClearedAllocatorStartsFromTheBottom() {
    std::printf("What a cleared allocator hands out next:\n");

    SlotAllocator slots;
    for (int i = 0; i < 8; ++i) slots.allocate();
    slots.clear();

    std::vector<uint32_t> handed;
    for (int i = 0; i < 3; ++i) handed.push_back(slots.allocate().index);
    check("slots 1, 2, 3, in that order", handed == std::vector<uint32_t>({1, 2, 3}));
}

// A claimed index usually comes from a scene file or a datagram, so how far it may
// grow the table is the allocator's to say once, not each reader's to remember.
void testAClaimPastTheBoundIsRefused() {
    std::printf("A slot claimed by an index nobody should hold:\n");

    SlotAllocator slots;
    check("slot zero is refused", !slots.allocateAt(0));
    check("  and so is one past the bound", !slots.allocateAt(SlotAllocator::MAX_CLAIMED_INDEX + 1));
    check("  without growing the table toward it", slots.extent() <= 1 && slots.size() == 0);
    check("  while one inside it is claimed", slots.has(slots.allocateAt(6)));

    Scene scene;
    check("a scene refuses the same index", !scene.createEntityAt(SlotAllocator::MAX_CLAIMED_INDEX + 1));
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
    check("  never a freed one", std::find(seen.begin(), seen.end(), made[1].index) == seen.end());
    check("  ascending by index", std::is_sorted(seen.begin(), seen.end()));

    slots.clear();
    check("clear empties it", slots.size() == 0);
    check("  and every handle from before is dead", !slots.has(made[0]));
}

struct Ping { int value = 0; };
struct Pong { int value = 0; };
struct Pang { int value = 0; };

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

    // Drained, not merely read, or every event would be delivered again every frame.
    bus.flush();
    check("a second flush delivers nothing", heard.size() == 2);
}

void testAnEventQueuedDuringAFlushWaitsForTheNext() {
    std::printf("An event queued by a listener that is mid-flush:\n");

    EventBus bus;
    std::vector<int> order;

    // A listener queueing more work: delivered inside this flush, a listener that
    // answers itself would loop unboundedly.
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

// The same rule across types: an event a listener enqueues to another type's bus
// also waits for the next flush. Drained one bus at a time, it would go out in the
// same flush only if its bus came later in TypeRegistry's first-touch order - so the
// same gameplay code would deliver this frame in one host and the next in another.
//
// Both directions, so neither type-id ordering passes by luck.
void testAnEventOfAnotherTypeAlsoWaitsForTheNextFlush() {
    std::printf("An event a listener enqueues of a type that is not its own:\n");

    EventBus bus;
    int heardPing = 0;
    int heardPang = 0;

    // Pong's type id precedes Pang's, so Pong -> Pang crosses forwards into a bus a
    // one-at-a-time drain would not have reached yet.
    bus.subscribe<Pong>([&](const Pong&) { ++heardPing; bus.enqueue(Pang{}); });
    bus.subscribe<Pang>([&](const Pang&) { ++heardPang; });

    bus.enqueue(Pong{});
    bus.flush();
    check("the listener that was queued for is heard", heardPing == 1);
    check("  and what it enqueued is not, whichever type it was", heardPang == 0);

    bus.flush();
    check("  it arrives on the next flush instead", heardPang == 1);

    // The other way round.
    EventBus reverse;
    int forwarded = 0;
    reverse.subscribe<Pang>([&](const Pang&) { reverse.enqueue(Pong{}); });
    reverse.subscribe<Pong>([&](const Pong&) { ++forwarded; });

    reverse.enqueue(Pang{});
    reverse.flush();
    check("and the same going the other way", forwarded == 0);
    reverse.flush();
    check("  arriving on the next flush too", forwarded == 1);
}

// A listener can reach the bus (gameplay holds one through BehaviorContext) and call
// flush mid-flush. Nested, the inner call would take the batch the outer one walks:
// takeQueue clears the vector deliver is standing in.
//
// Ignored rather than forbidden, like the nested emit the per-bus depth counter
// absorbs. Pinned: the outer delivery finishes, and what the inner call would have
// delivered still is - by the running flush or the next.
void testAFlushInsideAFlushIsIgnored() {
    std::printf("A listener that calls flush:\n");

    EventBus bus;
    int heard = 0;
    int after = 0;

    bus.subscribe<Pong>([&](const Pong&) {
        ++heard;
        bus.enqueue(Pang{});
        bus.flush();          // the nested call
    });
    bus.subscribe<Pang>([&](const Pang&) { ++after; });

    bus.enqueue(Pong{});
    bus.enqueue(Pong{});
    bus.flush();

    check("every event of the batch it was walking is still delivered", heard == 2);
    check("  and what it enqueued waits, as it would have anyway",      after == 0);

    bus.flush();
    check("  arriving on the next flush, one per delivery",             after == 2);
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

// A one-shot listener retires itself as it fires. The walk holds an index into the
// listener vector, so the entry is emptied, not erased - erasing skips whoever moves in.
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
    // The assertion that matters: an erase under the walk would have skipped this one.
    check("and the one that stayed still hears",       secondHeard == 2);

    bus.enqueue(Pong{});
    bus.flush();
    check("and goes on hearing",                       secondHeard == 3);
}

// What the callback does *after* retiring. The test above unsubscribes last, so a bus
// that destroyed the closure under it looks identical to one that did not. This one
// keeps going and watches from outside the closure - a file-scope counter and a stack
// local only, since reading a capture is exactly what would be undefined.
int g_closureDestructions = 0;

struct DestructionTripwire {
    DestructionTripwire()                                           = default;
    DestructionTripwire(const DestructionTripwire& other)            = default;
    DestructionTripwire& operator=(const DestructionTripwire& other) = default;
    ~DestructionTripwire() { ++g_closureDestructions; }
};

void testAListenerThatKeepsWorkingAfterRetiringItself() {
    std::printf("A listener that unsubscribes itself and then carries on:\n");

    EventBus bus;
    bool destroyedMidCall = true;
    int  heard            = 0;

    ListenerId id = 0;
    id = bus.subscribe<Pong>([&, tripwire = DestructionTripwire{}](const Pong&) {
        (void)tripwire;
        // A stack local, not a capture: readable whatever happens to the closure.
        const int before = g_closureDestructions;
        bus.unsubscribe<Pong>(id);
        destroyedMidCall = (g_closureDestructions != before);
        ++heard;
    });

    bus.enqueue(Pong{});
    bus.flush();

    // Clearing the std::function to mark the entry dead would run ~DestructionTripwire
    // on the closure still executing three lines up the stack, and the rest of the
    // callback would read freed captures.
    check("its closure outlives the call that removed it", !destroyedMidCall);
    check("and the rest of the callback still ran",        heard == 1);

    bus.enqueue(Pong{});
    bus.flush();
    check("it hears nothing after the dispatch unwinds",   heard == 1);
    // Dead and reaped, not merely skipped: a second remove finds nothing.
    check("and the entry is gone, not just silenced",      !bus.unsubscribe<Pong>(id));
}

// A listener subscribed during a dispatch waits in the pending list; a remove that
// looked only at the live list would miss it and admit it anyway - uncancellable.
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

// A throwing listener unwinds through the dispatch. The bus's bookkeeping must
// survive: left mid-dispatch, a later subscribe waits for an unwind that already
// happened, and a later flush returns before delivering anything.
void testAThrowingListenerLeavesTheBusWorking() {
    std::printf("What a listener that throws leaves behind:\n");

    EventBus bus;
    bus.subscribe<Ping>([](const Ping& e) {
        if (e.value < 0) throw std::runtime_error("listener");
    });

    bool threw = false;
    try {
        bus.emit(Ping{-1});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check("the throw reaches the emitter", threw);

    int heard = 0;
    bus.subscribe<Ping>([&](const Ping& e) { heard += e.value; });
    bus.emit(Ping{2});
    check("  a listener subscribed afterwards is admitted at once", heard == 2);

    bus.enqueue(Ping{-1});
    threw = false;
    try {
        bus.flush();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check("a flush lets the throw through too", threw);

    bus.enqueue(Ping{8});
    bus.flush();
    check("  and the next flush still delivers", heard == 10);
}

// A slot is reused as soon as its entity dies, and a component written through a
// stale id outlives it. Scene::add asserts on a dead entity, but a build without
// asserts has none; the inheriting entity's own add() would find the slot occupied,
// return the leftover and drop its value - a wrong value on an unrelated entity, silently.
void testARecycledSlotComesBackEmpty() {
    std::printf("What a recycled entity slot carries:\n");

    Scene scene;
    const EntityId first = scene.createEntity();
    scene.add(first, makeName("First"));
    scene.destroyEntity(first);
    check("destroying an entity takes its components", scene.count<Name>() == 0);

    // A stale id written to after the destroy, straight to the storage: Scene::add
    // would assert, so this pins the behaviour of a build without asserts.
    scene.storage<Name>()->add(first.slot(), makeName("Ghost"));
    check(
        "a write through a stale id lands on a slot with no entity",
        scene.count<Name>() == 1 && !scene.has<Name>(first)
    );

    const EntityId reused = scene.createEntity();
    check("the slot is handed out again", reused.slot() == first.slot());
    check("  and the entity taking it inherits nothing", !scene.has<Name>(reused));

    // What makes it dangerous: add onto an occupied slot returns what is there, so the
    // new entity would silently read as "Ghost".
    const Name& added = scene.add(reused, makeName("Second"));
    check(
        "  so its own component is the one it was given",
        std::string(added.value) == "Second" && std::string(scene.get<Name>(reused).value) == "Second"
    );

    // The same guarantee on the load path, which claims slots by index.
    Scene loaded;
    const EntityId placed = loaded.createEntityAt(7);
    loaded.add(placed, makeName("Placed"));   // so the storage exists at all
    loaded.destroyEntity(placed);
    loaded.storage<Name>()->add(7, makeName("Stale"));
    const EntityId again = loaded.createEntityAt(7);
    check("a slot claimed by index comes back empty too", bool(again) && !loaded.has<Name>(again));
}

// A component set's vtable lives in whichever binary first added the type - the
// gameplay module, for a set it filled. A clear that only emptied the sets would
// hold them past a project switch's unload, and the next destroyEntity would call
// code that is gone - on Windows, where nothing interposes.
void testAClearedSceneHoldsNoComponentSets() {
    std::printf("What a cleared scene still holds:\n");

    Scene scene;
    const EntityId held = scene.createEntity();
    scene.add(held, makeName("Held"));
    scene.clear();
    check(
        "clearing a scene drops its component sets, not only what is in them",
        scene.storage<Name>() == nullptr
    );

    const EntityId after = scene.createEntity();
    scene.add(after, makeName("After"));
    check(
        "  and a set asked for afterwards is made fresh",
        scene.count<Name>() == 1 && scene.has<Name>(after)
    );
}

} // namespace

void runEcsTests() {
    testATypeIdIsTheTypesAlone();
    testAListenerThatRetiresItself();
    testAListenerThatKeepsWorkingAfterRetiringItself();
    testAListenerRemovedBeforeItIsEverAdmitted();
    testAFreedHandleNeverComesBackToLife();
    testASlotClaimedByIndexIsNotHandedOutAgain();
    testAllocatingPastTheEndFillsTheGap();
    testAClearedAllocatorStartsFromTheBottom();
    testAClaimPastTheBoundIsRefused();
    testForEachVisitsTheLivingOnly();
    testAQueuedEventIsDeliveredOnceAndThenGone();
    testAnEventQueuedDuringAFlushWaitsForTheNext();
    testAListenerStopsBeingCalledOnceItIsGone();
    testEachEventTypeHasItsOwnQueue();
    testAnEventOfAnotherTypeAlsoWaitsForTheNextFlush();
    testAFlushInsideAFlushIsIgnored();
    testAThrowingListenerLeavesTheBusWorking();
    testARecycledSlotComesBackEmpty();
    testAClearedSceneHoldsNoComponentSets();
}

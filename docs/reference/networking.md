# Networking

Authoritative server with client-side prediction. One machine decides what is true; every
other machine predicts its own player at once and is told about everything else a fraction
of a second late. The same executables run a game networked or single-player, and a session
nobody joined behaves exactly as no session.

> The one idea: **one predicate decides who moves what.** `simulates(scene, entity)` answers
> yes to everything on a server and offline, and on a client for the character it plays,
> what that character is pushing, what the server never names and anything at or under a
> `NetLocal`. Props staying consistent, characters not being corrected every frame and
> single-player being unchanged all follow from that one answer.

## Running one

```
vkm_server  [project] [--port N]             # host the game, no window
vkm_runtime [project] --connect host[:port]  # join a game
vkm_runtime [project]                        # single player
```

`project.json` says how many players the game is for and its port:

```json
{ "maxPlayers": 4, "netPort": 27750 }
```

`--port` overrides the port, and an address given with no port falls back to the project's.
An unrecognized argument is refused, so a mistyped `--connect` is never a silent
single-player game.

**`vkm_server` is the only host, and it does not play.** It owns no entity, so `isMine()`
answers no everywhere, and every player is a client; to host and play on one machine, join it
from a second process (`vkm run --players N` starts a server and N players). The server opens
no window and installs no render backend, but runs the runtime's system stack: an authority
that simulated differently from its clients would not be one. It still needs the glfw shared
library ([building.md](building.md#cmake-targets)).

**The editor never hosts or joins**: its session is Offline for the life of the process. It
still runs `vkmSetupNetwork` on every module load, so its Project Settings can show the schema
and fingerprint a refused join prints.

## What a project writes

### What a player is

An optional module entry, beside `vkmRegisterBehaviors`:

```cpp
VKM_MODULE_ENTRY
void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    session.onSpawn(
        [](Scene& scene, ResourceManager& resources, PlayerId id) -> EntityId {
            return /* the entity this player drives, or a null id to refuse */;
        },
        [](Scene& scene, ResourceManager&, PlayerId id, EntityId entity) {
            /* the player left */
        });
}
```

Without the entry a project runs single-player in the runtime and editor, and `vkm_server`
refuses to serve it. Returning a null id refuses the join (`NetRefusal::Declined`) - how a
game says "the match has started".

### The five questions

On `BehaviorBase`, so a networked behavior reads like a single-player one:

| Ask | When | Offline |
|-----|------|---------|
| `isSimulated()` | before writing a transform, a velocity, anything the world can see | always true |
| `isMine()` | before touching the camera, the pointer, the HUD - anything about *this* player | always true |
| `command()` | instead of reading the device inside a fixed update | the local command |
| `isReplaying()` | before anything that *presents* - a clip, an effect - or that a tick asks once (`playSound`, `playAnimation`, `after` and `destroyAfter` ask themselves) | always false |
| `isAuthority()` | before deciding what is shared - a score, a door - or `instantiateShared` | always true |

`isSimulated()` and `isMine()` differ: a server simulates every player and owns none of
them. Offline both answer yes for every character, so a scene authored with four would move
all four from one keyboard; the game picks the one played offline, as the lab's
`isOfflineSeat` does (`examples/the_lab/src/lab_seat.h`).

### Messages

What a player says that must arrive - a move, a name, a choice in a lobby - is a message:

```cpp
const uint8_t move[] = {MOVE, from, to};
net().sendMessage(move, sizeof move);

// on the server
subscribe([this](const PlayerMessageEvent& said) { apply(said.player, said.bytes); });
```

On the server each arrives at most once, in the order its player sent it, as a
`PlayerMessageEvent` (`net/net_events.h`). `NetSession::sendMessage` says when it refuses
one; when the link is not keeping up, the game decides what to drop.

- **Messages go up only.** The server answers in replicated components; there is no channel
  to tell one player something no component holds.
- **Input held from tick to tick** beside the actions (which item a player's cursor is over)
  is not a message but the command's payload ([input.md](input.md#game-state-in-the-command)).

### What replicates

`Transform`, `Rigidbody`, `CharacterController`, `Ragdoll` and `NetSpawn` are registered by
the engine (`registerEngineNetTypes`).

- **`CharacterController` sends `grounded`, `jumping` and `crouched` only.** An end that does
  not simulate the character never decides `crouched`, and applies no gravity so never finds
  the ground or leaves it. Input, step-up state and slope normal stay local: sent, they would
  overwrite the owner's prediction with an answer a round trip old.
- **`Ragdoll` sends `active` only**; it decides whether the bones travel (below).

A project adds its own from `vkmSetupNetwork`, and only there (`NetSchema::replicate` says
why):

```cpp
NetSchema::get().replicate<Health>("Health");
```

A reflect block is enough; a codec of its own sends less (`NetSchema::replicate`). It writes into
the transport's `Network::BitWriter`, with its coders (`Network::toSigned` keeps a zero exact); a file
that names them says `using namespace Vkm::Network;` beside `Vkm::Engine`, as one naming keys does
`Vkm::Platform`. Two builds whose fields differ - a name, a size, a kind, an enum's order - refuse
each other.

**State the owner advances does not predict well.** On the owner's own entity only
`Transform`, `Rigidbody` and `CharacterController` are predicted
([Prediction and correction](#prediction-and-correction)); every other replicated component
takes the server's round-trip-old value at each snapshot. Harmless for a value the owner's
systems recompute before anything reads it; wrong for a cooldown, a charge or a count, which
steps back a round trip every snapshot. Keep such state out of the schema, or on the server
alone and shown rather than predicted.

### An effect the server counts

A client's behaviors start before its first snapshot, so one that plays an effect when a
replicated value changes would take whatever the server counted before the join as news. The
server raises a count; every end plays the effect when a `CountWatch` (`net/replication/count_watch.h`)
sees it rise, which it never says of the first value it sees.

### What does not replicate

One rule: **replicate causes, not effects.** State every end recomputes from something
already on the wire would arrive as a second writer for a value that already had one.

- **A ragdoll bone whose ragdoll is inactive** (`isPosedByAnimation`). The bones are posed
  from the clip each end plays. `Animator` does not replicate, so the game picks the clip
  from replicated state (a velocity, a flag), and both ends pose alike. Active, the solver
  drives them and every one travels.
- **A static body** (`isStaticBody`). Both ends read it from the same scene file; a quantized
  copy would only replace an exact value. A kinematic body still travels.
- **A fixed mesh with no body** (`isFixedMesh`, `Mesh::movable` off). It promises never to
  move, so it is the scene file's on both ends; a quantized copy would move it, which the cull
  takes for a broken promise.
- **An entity at or below a `NetLocal`** - what every end works out for itself: scenery a
  behavior moves by the clock, parts of a table a screen animates. Nothing at or under it is
  sent or drawn from a snapshot, and `simulates()` answers yes for it on a client too, outside
  a replay. Move it by `NetSession::worldTime` and every end moves it alike.
- **An entity inside a prefab instance** (`Prefab::isInsideInstance`). Both ends build it
  from the same file and it follows its root. An animator poses it the same on both ends; a
  ragdoll does not, so a ragdoll belongs on a character the scene author placed.

A spawned root's `NetSpawn` is sent even when silenced. `netSilence` asks the five for one
entity; a server classifies the world once per send round by the same rule (`NetSilenceMap`).

### Spawning

`NetSession::spawn` - a behavior's `instantiateShared(prefab, at)` - builds a prefab on the
server and on every client, including later joiners; offline it is an ordinary instantiate.
The root's `NetSpawn` carries the path, the pose and the slot each entity below it took, so a
client builds the same file into the same slots and the subtree's state is never on the wire.

**There is no despawn.** Destroy the root with its subtree -
`HierarchyOperations::destroyHierarchy` or `BehaviorBase::destroy` - and clients drop it.
`Scene::destroyEntity` on the root alone hands its children up to the top level, where they
start to replicate as entities the client never built.

**A client opens the path on the server's word**, so a spawned prefab must lie inside the
project; both ends refuse anything else (`isSayable`, `net/wire/codecs.h`, says why).

### Changing scene

A server that loads another scene mid-session (`BehaviorBase::loadScene`) takes its players
along. It spawns every seated player again through `SpawnPlayer`, keeping their player
numbers; no `DespawnPlayer` runs for the old world, whose entities went with it, and a spawn
that seats players on scene characters asks `NetSession::isDriven` which are taken. Each
client loads the scene it is told and plays on in it; one that cannot leaves the session.

- **The world is numbered** (`NetCore::worldGeneration`): a client drops a snapshot of a world
  it does not hold, and the server runs no command made for the world before.
- **The path must name a JSON file inside the project** (`isProjectJsonPath`), as a client
  opens it on the server's word. A scene that differs from the server's (by fingerprint) ends
  the session.
- **A player the game declines in the new world is let go** with a `Goodbye`.
- **A late joiner** whose entry scene is not the server's current one is seated anyway, and
  told which to load; it drives nothing until it has.
- **A player still loading is waited for longer** than a silent one: a load can hold its
  frames, and every packet with them.

### Lag compensation

A player aims at what they saw, a moment already past. On the server, `NetSession::Rewind`
puts every other player's past pose back for one query; the shooter stays in the present:

```cpp
{
    NetSession::Rewind rewound(net(), scene(), entity());
    RayHit hit;
    if (raycast(scene(), origin, Math::computeForward(command().view), range, hit, filter)) { ... }
}
```

Read inside the braces, act outside: the end restores what it moved and discards anything
written to it. Offline and on a client the scope does nothing.

- **Only players' roots are rewound**; `NetSession::Rewind` says what that leaves in the
  present.
- **The moment is not estimated from ping.** A client's command packets say which server tick
  it drew, which already accounts for its latency, interpolation delay and frame time. It is
  a client's claim, so it is clamped to the history, `NetRewind::HISTORY_TICKS` ticks: how
  far back a shot reaches depends on the tick rate.

### Limits

- **A lost connection leaves the world holding still.** The session goes to
  `NetRole::Disconnected` - not Offline, which would hand a dropped client a world it had only
  been shown. `isDisconnected()` and `lastError()` say what happened; what to do is the
  game's. A refused join ends the same way.
- **At most `NET_MAX_TICK_RATE` (128 Hz).** `host` and `connect` refuse a project that ticks
  faster: the queue depth, resend window and cushion are counted in commands.
- **No fragmentation.** A datagram over `Network::UdpSocket::MAX_DATAGRAM` (1200 bytes) is refused, not
  split; the snapshot budget keeps under it.

---

## How it works inside

### The pieces

`NetSession` is owned by `Engine` by value and reached through `FrameContext`. It is not a
`System`: it brackets the frame. `NetServer` holds the seats, the handshake, the command
queues and outgoing snapshots; `NetClient` the connection, prediction, correction and
interpolation; `NetCore` the socket, the buffers and what the two ends agreed. Both halves
always exist; the role decides which one is asked.

`Engine::run` (`core/engine.cpp`) brackets the frame with the session. Before the ticks it
advances by the wall clock, so a paused game still notices a peer leaving, applies what arrived,
seats whoever joined, delivers the players' messages on a server, sets the pacing ([Pacing](#pacing)),
interpolates what this end is only told about ([Interpolation](#interpolation)) and replays the
ticks the server disagreed about ([Replay](#replay)); each new tick then takes its command. The
systems that draw run inside `NetSession::Drawn`, which holds a correction's outstanding offset
in the scene for them alone ([Prediction and correction](#prediction-and-correction)), and last
`send` puts snapshots down or commands up.

### Identity is the server's slot

An entity is named on the wire by its slot in the server's `Scene`. Both ends load the same
scene file, which records the slot (`SceneSerializer` restores through `createEntityAt`), and
the host tells the session the world it opened (`NetSession::setWorld`) before anything runs.

A client makes entities of its own freely (UI, effects), so it keeps a table of what the
server's slots name here (`NetNames`). Whatever the server names that this end has not heard
of is made at that same slot when free, elsewhere when the client's own entity holds it. While it
plays, a client's own entities take no slot below twice its world's and a margin
(`Scene::keepSlotsBelow`), so the server's next entities find their slots free here, a spawned
prefab's every entity among them; only an entity made before it joined, or a server grown past
that many, sends one elsewhere.
Keeping the slot matters: the simulation walks entities in slot order, and a replay must visit
predicted bodies in the server's order. A replicated component that names another entity
sends the server's slot, which `NetSession::entityFor` turns into this end's entity.

### Snapshots

A snapshot carries a component only when its quantized bytes differ from what that connection
has **confirmed receiving** - confirmed, not sent, or a client that missed a packet is never
told again - so a joiner's first snapshot is the whole world by the same rule, and a settled
world goes quiet (`NetBaseline`). What has gone is reported by occupant, not by slot, so an
entity destroyed and replaced between two sends is not missed. A value the wire cannot tell
from the one held is kept as held (`sameOnWire`), so a joiner keeps the exact pose its scene
file gave an entity the server never moved. The connection's own entity is written every
snapshot and never deferred: its receiver reconciles against it.

Precision per field is in `net/wire/quantize.h`. Two properties are load-bearing: **zero is
exact for every signed field** (`Network::toSigned`; an inexact zero tilts every unrotated
body and drifts every resting one), and **a rotation component takes `Quantize::ROTATION_BITS`**,
because a rotation error is an angle and lifts the far end of a long floor.

### Commands

A command packet repeats every command of the newest `NET_COMMAND_REDUNDANCY` the server has
not acknowledged. Redundancy, not retransmission: `pressed` is true for one command only, so a
lost datagram would lose that jump, and a retransmit would arrive after its tick. The server
runs one command per tick, in order, never twice (`NetCommandBuffer`); the ends count ticks on
separate clocks, so a command is not addressed by tick number.

### Messages: once, in order

Messages ride the command packet in the room the commands leave, each riding every packet
until acknowledged (`Network::Outbox`), and the server hands on only the next in order (`Network::Inbox`).
Both are vkmNetwork's; `modules/vkmNetwork/docs/how-it-works.md` says how they keep the order.

### Pacing

Two clocks at one nominal rate drift, and the server's command queue depth is that drift added
up. So every snapshot says how low the queue ran, and the client steers it toward a cushion by
bending how fast its clock turns wall time into ticks (`NetPacing`). **Only the pacing
bends**: the step is `Clock::getFixedStep()` on every end, so a replay re-runs exactly what the
server ran.

### Prediction and correction

A client runs its own character at once and holds what it predicted for each tick. When a
snapshot judges a tick, a position difference under `CORRECTION_FLOOR` (quantization plus
float noise) is ignored; above it the server's answer stands and **every tick since is run
again from it** ([Replay](#replay)).

The simulation takes the correction at once; the picture does not. The jump becomes an offset
that decays over a fraction of a second, applied inside `NetSession::Drawn` (around the systems
that draw) and taken back off in its destructor, so no tick simulates from a position chosen
to look nice. There is one offset per predicted body, so a leased crate and the character
pushing it ease together.

Only `Transform`, `Rigidbody` and `CharacterController` of what this end predicts - its
character and the bodies it leases - are set aside and restored when the prediction stands
(`NetClient::holdPredicted`), and only the character's position decides; a disagreement
elsewhere never causes a replay. `Collider` does not replicate, so a replay runs with the
present capsule: a crouch changed within a round trip can be mispredicted, and a
lag-compensated hit meets the present crouch.

### Replay

A replayed tick must be a function of the world and the command, and doing it twice must
equal doing it once. `System::isReplayed()` says which systems are; only those run in the
replay pass.

| Replayed | Not replayed |
|---|---|
| `BehaviorSystem` - turns a command into movement | `AnimationSystem` - a clip advanced twice gains a tick |
| `PhysicsSystem` | `SkeletalAnimationSystem` - a marker crossed twice is a footstep played for one step |
| `CharacterControllerSystem` | `RagdollSystem` - writes bone state from the pose, which both ends do for themselves |
| `WaterSystem` - builds the water and waves the tick floats on, a client's at the drawn moment, not the replayed tick's; floats what is in it | |

The replay pass also runs **no `EventBus::flush()`** (it would deliver the live queue twice),
**no `InputMap::beginTick()`** (the session hands over the original command) and reports **no
collision or trigger events** ([physics.md](physics.md#events)). Behaviors tick only for what
this end simulates, and a body it does not decide is immovable for the pass. Unreplicated
per-tick state - the controller's step latch and `walkSpeed`, animation, a behavior's
members - is not rewound. Nor is the ground a character stood on: a snapshot says whether it
stood, so a replay's first tick measures a crest and a moving floor's carry from the present's
ground.

### Leasing

A client also simulates the bodies its character touches, and what those touch, so a push is
not predicted as walking into a wall. `PhysicsSystem` computes the owned body's island over
the tick's contacts and joints and reports it to the session (`NetSession::lease`):

- **Never through an immovable body**, or the floor would lease the world.
- **Never onto another character** (a body with a `CharacterController`) - what steers it is
  not this end's input.
- **Joints are edges**, so a shoved ragdoll leases its jointed limbs.
- **Only the server's bodies are leased**: one the client runs already (its own, or at or
  below a `NetLocal`) would be run again by every replay.

`simulates()` gates physics' gather and writeback, so a lease is one tick late. A leased body
is corrected with the character - both predictions put back or neither.

### Interpolation

What the client does not own is drawn between two poses the server gave, behind the newest
by a delay that grows with jitter (`NetInterpolation`). The draw clock never passes the newest
sample, so a stall shows as the world pausing.

### The connection and rates

Every packet carries its sequence and acknowledges what came back; nothing is retransmitted
(why UDP is a row in [decisions.md](decisions.md#what-has-been-decided)). The connection, the socket and
the token are vkmNetwork's (`Network::Connection`, `Network::UdpSocket`, `Network::JoinCookie`);
`modules/vkmNetwork/docs/how-it-works.md` says how each works. Snapshots and
commands go out at fixed rates by wall clock (`snapshotRate`, which is `NET_SNAPSHOT_RATE` or
one a tick where ticks are fewer, and `NET_COMMAND_RATE`), at most one packet a frame, so an
overrun never becomes a burst.

### The handshake

`Hello` carries the schema fingerprint, the action count and the action-slot fingerprint
(`InputMap::actionFingerprint`), the entry scene's fingerprint and the `tickRate`. Commands
carry actions by slot and entities are named by slot, so any difference is refused with
`NetRefusal::Mismatch`, both ends printing what they hold; a different tick rate is
`NetRefusal::TickRate`. The actions are `project.json`'s, slotted in name order by every host
before its world starts and given to `host` and `connect` with the tick rate, so a client
listing other actions is refused and one listing the same in another order is the same game. A different entry scene is seated once the server has
moved on to another ([Changing scene](#changing-scene)).

A `Hello` at another version of the wire is challenged as any is and, once it echoes its token,
gets `Mismatch`; anything else from it is dropped unread. The header and the kinds `Hello`,
`Challenge` and `Refuse` read alike at every version (`readMessageHeader`); a `Refuse`'s reason
follows the header, as in 1.1.0, and a `Challenge`'s echo from version 15 on; and a `Challenge` is
written at the version of the `Hello` it answers, so each end is told the other differs. This
build's client is told by any server: by one of this build or later once it echoes, and at once by
1.1.0's, which refuses another version's `Hello` unchallenged, echoing its token. 1.1.0's client
reads a `Challenge` only at its own version, and takes the server's token from its header, before
the echo it does not read; so it echoes the token too, and is refused. A build at the same version
is vetted like any join, by the fingerprints above.

Snapshots wait for the first `Command`, so a client's behaviors start before anything the
server holds has arrived ([An effect the server counts](#an-effect-the-server-counts)).

**Security.** Every message carries a 64-bit token, and the token is the seat; a source address
is trivially forged. A `Hello` without the right token gets a `Challenge` holding one and the
server keeps nothing and says nothing else, as a refusal to a forged address would turn its real
client away; only a `Hello` echoing it is vetted and seated. The token is a keyed hash of the
address and a time window (`Network::JoinCookie`), not a record, so a flood of forged Hellos crowds
out no real join. A client's `Hello` carries a token it drew until a `Challenge` gives it one. A
`Challenge` echoes the token of the `Hello` it answers, and the client takes one only echoing the
token it holds, as proof that the server heard it; it takes a `Refuse` only carrying that token.
Neither is known to a forger who never saw its `Hello`, so a `Challenge` or a `Refuse` forged in
the server's name is dropped: else the one could give it a token for the other to carry. **The
token is checked before the connection sees the datagram**, in both directions: accepting a packet
moves the window and confirms its acknowledgements, so a forged one must never reach it.

### Seats

Everything about a player - connection, baseline, command queue - is allocated and destroyed
with them; reused per-seat state would hand the next player deltas against a world they never
held. A player number is taken only by a join the game accepted, and is not reused while its
player is seated.

## Key files

- `src/engine/net/net_session.{h,cpp}` - `NetSession`, the API
- `src/engine/net/net_core.{h,cpp}` - `NetCore`, what the session's two halves share
- `src/engine/net/net_role.h`, `src/engine/net/player_id.h` - `NetRole`, `isAuthority`, `PlayerId`
- `src/engine/net/net_server.{h,cpp}` and `net_client.{h,cpp}` - the authority's half and a guest's
- `src/engine/net/message_bodies.{h,cpp}` - each message's body, written and read in one place
- `src/engine/net/net_events.h` - `PlayerMessageEvent`
- `src/engine/net/replication/count_watch.h` - `CountWatch`
- `net/wire/` - `quantize.{h,cpp}` (the engine's ranges), `protocol.{h,cpp}`, `schema.{h,cpp}` (`NetSchema`),
  `codecs.{h,cpp}` (the engine's codecs and `isSayable`), `project_path.{h,cpp}` (`isProjectJsonPath`)
- `net/replication/` - `snapshot.{h,cpp}` (`NetBaseline`), `names.{h,cpp}` (`NetNames`),
  `silence.{h,cpp}` (`netSilence`, `NetSilenceMap`)
- `net/latency/` - `command_packet.{h,cpp}` (a command packet), `command_buffer.{h,cpp}`
  (`NetCommandBuffer`), `interpolation.{h,cpp}`,
  `pacing.{h,cpp}` (`NetPacing`), `rewind.{h,cpp}`

vkmNetwork, `modules/vkmNetwork/src/vkm_network/` (its `README.md` maps it and `docs/how-it-works.md`
says how the transport works): `udp_socket.h/.cpp` (`Network::UdpSocket`), `address.h/.cpp`
(`Network::Address`), `connection.h/.cpp` (`Network::Connection`), `reliable_messages.h/.cpp`
(`Network::Outbox` / `Network::Inbox`), `join_cookie.h/.cpp` (`Network::JoinCookie`), `bit_stream.h`,
`quantize.h/.cpp` (the coders the engine's `Quantize::` ranges write through); its byte order and
Winsock headers are private to it, under `modules/vkmNetwork/private/vkm_network/`.

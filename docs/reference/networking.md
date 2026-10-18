# Networking

Authoritative server with client-side prediction. One machine decides what is
true; every other machine predicts its own player at once and is told about
everything else a fraction of a second late. The same executables run a game
networked or single-player, and a session nobody joined behaves exactly as no
session.

> The one idea: **one predicate decides who moves what.** `simulates(scene,
> entity)` answers yes to everything on a server and offline, and on a client for
> the character it plays, what that character is pushing, what the server never
> names and anything at or under a `NetLocal`. Props staying consistent,
> characters not being corrected every frame and single-player being unchanged
> all follow from that one answer.

## Running one

```
vkm_server  <project> [--port N]             # host the game, no window
vkm_runtime <project> --connect host[:port]  # join a game
vkm_runtime <project>                        # single player
```

`project.json` says how many players the game is for and its port:

```json
{ "maxPlayers": 4, "netPort": 27750 }
```

`--port` overrides the port,
and an address given with no port falls back to the project's. An unrecognised
argument is refused before anything loads, so `--conect 10.0.0.4` is an error,
not a silent single-player game.

**`vkm_server` is the only host, and it does not play.** It owns no entity, so
`isMine()` answers no everywhere, and every player is a client. To host and play
on one machine, run `vkm_server` and join it with a second process
(`vkm run --players N` builds and cooks the project, then starts a server and N
players on it). The server opens no window and installs no render backend, but
runs the runtime's system stack: an authority that simulated differently from
its clients would not be one. It still needs the glfw shared library
([building.md](building.md)).

## What a project writes

### What a player is

An optional module entry, beside `vkmRegisterBehaviors` and `vkmBuildScene`:

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

Without the entry a project runs single-player in the runtime and editor, and
`vkm_server` refuses to serve it. Returning a null id refuses the join
(`NetRefusal::Declined`) - how a game says "the match has started".

### The four questions

On `Behavior`, so a networked behavior reads like a single-player one:

| Ask | When | Offline |
|-----|------|---------|
| `isSimulated()` | before writing a transform, a velocity, anything the world can see | always true |
| `isMine()` | before touching the camera, the pointer, the HUD - anything about *this* player | always true |
| `command()` | instead of reading the device inside a fixed update | the local command |
| `isReplaying()` | before anything that *presents* - a clip, a sound, an effect | always false |

`isSimulated()` and `isMine()` differ: a server simulates every player and owns
none of them. `command()` is the local input offline and on the owning client,
and on a server what that entity's player sent, in order; an entity no player
drives reads as nothing held.

Offline `isSimulated()` and `isMine()` answer yes for every character, so a
scene authored with four would move all four from one keyboard. the_lab
answers it with a question of its own (`isOfflineSeat`,
`examples/the_lab/src/lab_seat.h`).

### Messages

What a player says that must arrive - a move, a name, a choice in a lobby - is a
message:

```cpp
const uint8_t move[] = {MOVE, from, to};
net().sendMessage(move, sizeof move);

// on the server
subscribe([this](const PlayerMessageEvent& said) { apply(said.player, said.bytes); });
```

On the server each arrives at most once, in the order its player sent it, as a
`PlayerMessageEvent` (`net/player_message.h`) at the event flush after it arrived. What was in flight when a player
left goes with their seat.

- Up to `NET_MESSAGE_MAX_BYTES` (255) bytes each.
- `sendMessage` returns false and sends nothing anywhere but on a playing client
  (offline, on a server, before the Welcome), and while `NET_MESSAGE_WINDOW` (64)
  are unacknowledged - the link is not keeping up, and the game decides what to
  drop. Offline the game is its own authority and applies a choice where it makes
  it.
- Messages go up only. The server answers in replicated components; there is no
  channel to tell one player something no component holds.
- Input held from tick to tick beside the actions (which item a player's cursor
  is over) is not a message but the command's payload
  ([input.md](input.md#game-state-in-the-command)).

### What replicates

`Transform`, `Rigidbody`, `CharacterController`, `Ragdoll` and `NetSpawn` are
registered by the engine (`registerEngineNetTypes`).

- **`CharacterController` sends `grounded` and `crouched` only.** An end that
  does not simulate the character never decides `crouched`, and applies no
  gravity so never finds the ground. Input, step-up state and slope normal stay
  local: sent, they would overwrite the owner's prediction with an answer a round
  trip old.
- **`Ragdoll` sends `active` only**; it decides whether the bones travel (below).

A project adds its own from `vkmSetupNetwork`, and only there - the schema is
rebuilt from nothing each time that entry runs, so a type registered from
`vkmRegisterBehaviors` is wiped before play:

```cpp
NetSchema::get().replicate<Health>("Health");
```

This needs free functions `netEncode(const Health&, BitWriter&)` and
`netDecode(Health&, BitReader&)` visible beside the component; without them the
registration line does not compile.

**State the owner advances does not predict well.** On the owner's own entity
only `Transform` and `Rigidbody` are predicted (see *Prediction and
correction*); every other replicated component takes the server's round-trip-old
value at each snapshot. Harmless for a value the owner's systems recompute before
anything reads it; wrong for a cooldown, a charge or a count, which steps back a
round trip every snapshot. Keep such state out of the schema, or on the server
alone and shown rather than predicted.

### What does not replicate

One rule: **replicate causes, not effects.** State every end recomputes from
something already on the wire would arrive as a second writer for a value that
already had one.

- **A ragdoll bone whose ragdoll is inactive** (`isPosedByAnimation`). The bones
  are posed from the clip each end plays. `Animator` does not replicate, so the
  game picks the clip from replicated state (a velocity, a flag), and both ends
  pose alike. Active, the solver drives them and every one travels.
- **A static body** (`isStaticBody`, `RigidbodyMotion::Static`). Both ends read
  it from the same scene file; a quantised copy would only replace an exact value.
  A kinematic body still travels.
- **An entity at or below a `NetLocal`** - what every end works out for itself:
  scenery a behavior moves by the clock, parts of a table a screen animates.
  Nothing at or under it is sent or drawn from a snapshot, and `simulates()`
  answers yes for it on a client too, outside a replay. Move it by
  `NetSession::worldTime` and every end moves it alike.
- **An entity inside a prefab instance** (`Prefab::isInsideInstance`). Both ends
  build it from the same file and it follows its root. An animator poses it the
  same on both ends; a ragdoll does not, so a ragdoll belongs on a character the
  scene author placed.

A spawned root's `NetSpawn` is sent even when silenced. `netSilence` asks the
four for one entity (the editor's inspector shows it); a server classifies the
world once per send round through the same rule (`NetSilenceMap`).

### Spawning

`NetSession::spawn(scene, resources, prefab, at)` builds a prefab on the server
and on every client, including later joiners; offline it is an ordinary
instantiate. On a client it is refused - the server would never hear of it - and
a client's own entity is a plain `Prefab::instantiate`.

**There is no despawn.** Destroy the root with its subtree -
`HierarchyOperations::destroyHierarchy` or `Behavior::destroy` - and clients drop
it. `Scene::destroyEntity` on the root alone hands its children up to the top
level, where they start to replicate as entities the client never built.

The server refuses, before building, what a client could not safely be told
(`isSayable`, `net/wire/codecs.h`), and the decoder refuses the same: clients
open the path on the server's word, so it must name a prefab inside the project -
otherwise a hostile server names a network share and a Windows client hands it
its login hash.

How it travels: the root gets a `NetSpawn` - the prefab path, the pose
(unquantised, since a static body is never described again) and the slot each
entity below it took (`Prefab::BuiltSlots`), for up to `NET_SPAWN_MAX_SLOTS`; a
bigger instance is sent with none, so its slots can differ between the ends. The
client builds the same file into those slots, so the subtree's state is never on
the wire.

### Lag compensation

A player aims at what they saw, a moment already past. On the server,
`NetSession::Rewind` puts every other player's past pose back for one query;
the shooter stays in the present:

```cpp
{
    NetSession::Rewind rewound(net(), scene(), entity());
    RayHit hit;
    if (raycast(scene(), origin, Math::computeForward(command().view), range, hit, filter)) { ... }
}
```

Read inside the braces, act outside: the end restores what it moved and
discards anything written to it. Offline and on a client the scope does nothing.
A nested scope changes nothing; the outermost one's end restores the present.

- **Only players are rewound** - a platform, a door or a thrown crate is judged
  against the present - and only a player's **root**: its bones follow it, but a
  crouch or lean is the present one.
- **The moment is not estimated from ping.** A client's command packets say which
  server tick it drew, which already accounts for its latency, interpolation delay
  and frame time; the server rewinds to that less how long the command queued.
  It is a client's claim, so it is clamped to the history: `NetRewind::HISTORY_TICKS`
  (64) ticks, so how far back a shot reaches depends on the tick rate.

### Limits

- **A lost connection leaves the world holding still.** The session goes to
  `NetRole::Disconnected` - not Offline, which would hand a dropped client a world
  it had only been shown. `isDisconnected()` and `lastError()` say what happened;
  what to do is the game's. A refused join ends the same way.
- **At most `NET_MAX_TICK_RATE` (128 Hz).** `host` and `connect` refuse a project
  that ticks faster: the queue depth, resend window and cushion are counted in
  commands.
- **No fragmentation.** A datagram over `UdpSocket::MAX_DATAGRAM` (1200 bytes) is
  refused, not split; the snapshot budget keeps under it.

---

## How it works inside

### The pieces

`NetSession` is owned by `Engine` by value and reached through `FrameContext`. It
is not a `System`: it brackets the frame. `NetServer` holds the seats, the
handshake, the command queues and outgoing snapshots; `NetClient` the
connection, prediction, correction and interpolation; `NetCore` the socket, the
buffers and what the two ends agreed. Both halves always exist; the role decides
which one is asked.

```
advance(dt)              wall clock, so a paused game still notices a peer leaving
receive(scene, res)      apply what arrived, reconcile, seat whoever joined,
                         build what the server spawned
deliver(events)          players' messages onto the bus, on a server
pacing()                 how fast the clock owes ticks from the next frame on
interpolate(...)         draw what this end is only told about
replayCommands()         ticks the server disagreed about, run again
  beginReplayTick(...)   the command that tick originally ran
  ... replayed systems ...
  endTick(...)           remember what was predicted, again
endReplay(scene)         measure what the picture now owes for each of them
per fixed step:
  beginTick(...)         take this tick's command
  ... systems ...
  endTick(...)           remember what was predicted
NetSession::Drawn {      what is drawn catches up with what is simulated
  ... systems ...        drawn where the player sees it
}                        and left where it actually is
send(scene, tick)        snapshots down, or commands up
```

### Identity is the server's slot

An entity is named on the wire by its slot in the server's `Scene`. Both ends
load the same scene file, which records the slot (`SceneSerializer` restores
through `createEntityAt`), and the host tells the session the world it opened
(`NetSession::setWorld`, from `bootProjectWorld`) before anything runs.

A client makes entities of its own freely (UI, effects), so it keeps a table of
what the server's slots name here (`NetNames`). Whatever the server names that
this end has not heard of is made at that same slot when free, elsewhere when
the client's own entity holds it. Keeping the slot matters: the simulation walks
entities in slot order, and a replay must visit predicted bodies in the server's
order. When the session closes, what it made leaves with it; a session lost
rather than closed keeps it, holding still.

A replicated component that names another entity sends the server's slot; ask
`NetSession::entityFor(scene, slot)` what that names here (on a server and
offline, the slot itself).

### Snapshots

A snapshot carries a component only when its quantised bytes differ from what
that connection has **confirmed receiving** - confirmed, not sent, or a client
that missed a packet is never told again. The delta is on presence, never on
values: every entry decodes from the packet alone. Joining and playing are one
code path: a connection that has confirmed nothing gets the whole world. A
settled world goes quiet.

- **What has gone is reported by occupant, not by slot**, so an entity destroyed
  and replaced between two sends is not missed. The client applies losses before
  entries.
- **A component taken away is said gone**: an entry carries the types the
  connection holds and the entity no longer does, and the client removes them.
  A removal is confirmed like any change, so a late acknowledgement of an older
  snapshot cannot have the server believe the client holds it again.
- **A value the wire cannot tell from the one held is kept as held**
  (`sameOnWire`), so a joiner keeps the exact pose its scene file gave an entity
  the server never moved, rather than its quantised copy. A type whose codec
  carries a field exactly takes that field all the same (`netKeep`; a
  `Transform` takes its scale).
- **The connection's own entity is written every snapshot**, unchanged or not,
  and never deferred: the receiver holds its own prediction, not what it was
  told, and reconciles against it.
- **The budget.** `NetBudget::bytes` is a ceiling (default
  `NetConnection::MAX_PAYLOAD`). What does not fit is deferred, and a deferred
  entity's priority rises until it goes.

Precision per field is in `net/wire/quantize.h`. Two properties are
load-bearing: **zero is exact for every signed field** (`Quantize::toSigned`;
an inexact zero tilts every unrotated body and drifts every resting one), and
**rotations are eleven bits a component**, because a rotation error is an angle
and lifts the far end of a long floor. A scale travels as three raw floats when
not one, bounded on both ends by `netScale` (`NET_MAX_SCALE`); a server applies
the bound to every Transform it describes before writing, so what it simulates is
what clients are told.

### Commands

A command packet carries every one of the newest `NET_COMMAND_REDUNDANCY`
commands the server has not acknowledged, and before those any command no packet
has carried yet (`firstCommandToSend`), up to `NET_MAX_PACKET_COMMANDS` a packet.
Redundancy, not retransmission: `pressed` is true for one command only, so a lost
datagram would lose that jump, and a retransmit would arrive after its tick.

The server runs one command per tick, in order, never twice. Commands are **not**
addressed by tick number - the ends count ticks on separate clocks; the client's
tick rides along and comes back in the snapshot so it knows which prediction was
judged. The queue's edges:

- **Too deep** (past `NetCommandBuffer::MAX_DEPTH`): the oldest are skipped,
  their edges folded into the command that runs.
- **Empty**: the tick repeats the last command's axes with edges cleared, and
  after `NetCommandBuffer::REPEAT_TICKS` dry ticks stops the body.

### Messages: once, in order

Messages ride the command packet after the commands, in the room left
(`NetOutbox`, `net/transport/messages.h`). Each is numbered; every packet carries
every unacknowledged one that fits, oldest first, until a snapshot header's
sixteen-bit number (the newest the server handed on) acknowledges it and all
before it. The server's `NetInbox` hands on only the next in order - a copy is
skipped, one past a gap waits - and at most a window's worth between two
acknowledgements, so flooding costs the game nothing extra.

### Pacing

Two clocks at one nominal rate drift, and the server's command queue depth is
that drift added up. So every snapshot says how low the queue ran
(`NetCommandBuffer::lowWater`), and the client steers it toward a target by
bending how fast its clock turns wall time into ticks, by at most
`NetPacing::MAX_DILATION` either way (`NetPacing`). A jittery link keeps a deeper
cushion.

**Only the pacing bends.** The step is `Clock::getFixedStep()` on every end, so a
replay re-runs exactly what the server ran. `Engine::run` hands
`NetSession::pacing()` to `Clock::setPacing` each frame; it is exactly one
offline, on a server, while joining and once disconnected.

### Prediction and correction

A client runs its own character at once and holds what it predicted for each
tick. When a snapshot judges a tick, a position difference under
`CORRECTION_FLOOR` (quantisation plus float noise) is ignored; above it the
server's answer stands and **every tick since is run again from it** (*Replay*).

The simulation takes the correction at once; the picture does not. The jump
becomes an offset that decays over a fraction of a second, applied inside
`NetSession::Drawn` (around the systems that draw) and taken back off in its
destructor, so no tick simulates from a position chosen to look nice. There is
one offset per predicted body, so a leased crate and the character pushing it
ease together.

Only `Transform` and `Rigidbody` of what this end predicts - its character and
the bodies it leases - are set aside and restored when the prediction stands
(`holdPredicted` / `restoreHeld`), and only the character's position decides; a
disagreement elsewhere never causes a replay.

### Replay

A replayed tick must be a function of the world and the command, and doing it
twice must equal doing it once. `System::isReplayed()` says which systems are;
only those run in the replay pass.

| Replayed | Not replayed |
|---|---|
| `BehaviorSystem` - turns a command into movement | `AnimationSystem` - a clip advanced twice gains a tick |
| `PhysicsSystem` | `SkeletalAnimationSystem` - a marker crossed twice is a footstep played for one step |
| `CharacterControllerSystem` | `RagdollSystem` - writes bone state from the pose, which both ends do for themselves |

The replay pass also runs **no `EventBus::flush()`** (it would deliver the live
queue twice), **no `InputMap::beginTick()`** (the session hands over the original
command) and reports **no collision or trigger events**
([physics.md](physics.md#events)). Behaviors tick only for what this end
simulates, and a body it does not decide is immovable for the pass.
Unreplicated per-tick state - the controller's step latch and `walkSpeed`,
animation, a behavior's members - is not rewound.

### Leasing

A client also simulates the bodies its character touches, and what those touch,
so a push is not predicted as walking into a wall. `PhysicsSystem` computes the
owned body's island over the tick's contacts and joints and reports it to the
session:

- **Never through an immovable body**, or the floor would lease the world.
- **Never onto another character** (a body with a `CharacterController`) - what
  steers it is not this end's input.
- **Joints are edges**, so a shoved ragdoll leases its jointed limbs.

A lease lasts `LEASE_TICKS` after it was last reported, so it does not lapse
on each bounce. `simulates()` gates physics' gather and writeback, so the lease
is one tick late. A leased body is corrected with the character - both
predictions put back or neither.

### Interpolation

What the client does not own is drawn between two poses the server gave, behind
the newest news by a delay that grows with measured jitter (`NetInterpolation`,
`net/prediction/interpolation.h`); the draw clock eases toward it rather than
jumping, and never passes the newest sample, so a stall shows as the world
pausing. What the server said is put back before each snapshot is
read, or "unchanged" would mean unchanged from the smoothed value.

### The connection and rates

Every packet carries its sequence and acknowledges what came back; nothing is
sent for acknowledgement and nothing is retransmitted (why UDP is a row in
[engine.md](../guides/engine.md#4-what-has-already-been-decided)). Sequences are
sixteen bits and wrap, so newer-than is a distance, not `a > b`. A packet older
than one accepted is refused.

Snapshots (`NET_SNAPSHOT_RATE`) and commands (`NET_COMMAND_RATE`) go out 64 times
a second by wall clock, at most one packet a frame; an overrun carries forward
but never becomes a burst. Lowering `NET_SNAPSHOT_RATE` saves bandwidth and makes
other players look later, since the interpolation delay is counted in snapshots.

### The handshake

`Hello` carries the schema fingerprint (the ordered replicated type names,
folded with the quantiser's steps and widths), the action count, the
action-slot fingerprint (`InputMap::actionFingerprint`), the entry scene's
fingerprint (`setWorld`) and the `tickRate`. Commands carry actions by slot and
entities are named by slot, so any difference is refused with
`NetRefusal::Mismatch`, both ends printing what they hold; a fingerprint of zero
(a generated world, or an end that has not ticked yet) is compared against
nothing. A different tick rate is `NetRefusal::TickRate`. A full server (`Full`)
and a game that declined (`Declined`) are refused the same way, with a reason a
player can be shown. A message's header, the kinds `Hello` and `Refuse` and the
refusal reasons are fixed across protocol versions, so another build's `Hello`
gets `Mismatch` and anything else from it is dropped unread.

The client says hello from the frame loop, not `connect()`, because actions are
defined in a behavior's `onStart`, inside the first tick; it repeats at the
command rate until answered. Snapshots wait for the first `Command`. So a
client's behaviors start before anything the server holds has arrived: one that
acts on a replicated value changing takes the first value it sees as where it
stands, or what the server counted before the join arrives as news.

**Security.** Every message carries a 64-bit token, and the token is the seat; a
source address is trivially forged. A `Hello` without the right token gets a
`Challenge` holding one and the server keeps nothing; only a `Hello` echoing it
is seated and welcomed. The token is not a record but a keyed hash of the
address and a time window (`NetJoinCookie`), so a flood of forged Hellos crowds
out no real join. **The token is checked before the connection sees the
datagram**, in both directions: accepting a packet moves the window and confirms
its acknowledgements, so a forged one must never reach it.

### Seats

Everything about a player - connection, baseline, command queue - is allocated
and destroyed with them; reused per-seat state would hand the next player deltas
against a world they never held. A player number is taken only by a join the game
accepted, and is not reused while its player is seated.

### In the editor

The editor's session is Offline for the life of the process; it never hosts or
joins (`vkm_server` boots `project.entryScene` from disk, the editor plays a
scene in memory). **File > Project > Settings...** edits the player count and
port, and its Multiplayer card shows the schema and fingerprint a refused join
prints. The inspector's identity header shows the selected entity's wire slot and
which components replicate, or which silence rule keeps it off the wire. Both
build the schema through `vkmSetupNetwork`, on every module load and reload.

## Key files

- `src/engine/net/net_session.{h,cpp}` - `NetSession`, the API, and `NetCore`
- `src/engine/net/net_server.{h,cpp}` and `net_client.{h,cpp}` - the authority's half and a guest's
- `src/engine/net/player_message.h` - `PlayerMessageEvent`
- `net/wire/` - `quantize.{h,cpp}`, `protocol.{h,cpp}`, `schema.{h,cpp}` (`NetSchema`),
  `codecs.{h,cpp}` (the engine's codecs and `isSayable`)
- `net/transport/` - `connection.{h,cpp}`, `messages.{h,cpp}` (`NetOutbox` / `NetInbox`),
  `join_cookie.{h,cpp}` (`NetJoinCookie`)
- `net/replication/` - `snapshot.{h,cpp}`, `names.{h,cpp}` (`NetNames`),
  `silence.{h,cpp}` (`netSilence`, `NetSilenceMap`)
- `net/prediction/` - `command.{h,cpp}` (`NetCommandBuffer`), `interpolation.{h,cpp}`,
  `pacing.{h,cpp}` (`NetPacing`), `rewind.{h,cpp}`
- `src/engine/platform/net/` - `udp_socket.{h,cpp}`, `net_address.{h,cpp}`, `winsock_init.{h,cpp}`

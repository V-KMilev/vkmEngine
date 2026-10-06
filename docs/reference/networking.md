# Networking

Authoritative server with client-side prediction. One machine decides what is
true; every other machine predicts its own player at once and is told about
everything else a fraction of a second late. The same executables run a game
networked or single-player, and a project that never opens a session pays
nothing for the code being there.

> The one idea: **one predicate decides who moves what.** `simulates(entity)`
> answers yes to everything on a server and offline, and on a client for what
> that client owns plus what its character is currently pushing. Everything else - why props stay consistent between
> players, why a character does not get corrected every frame, why single-player
> is unchanged - follows from that one answer.

## Running one

```
vkm_server  <project> [--port N]             # host the game, no window
vkm_runtime <project> --connect host[:port]  # join a game
vkm_runtime <project>                        # single player, unchanged
```

To host and play, serve the project and join it - two processes, and the second
of them a real client:

```
vkm_server  examples/physics_lab &
vkm_runtime examples/physics_lab --connect 127.0.0.1
```

A project says how many players it is for and which port it is served on, in
`project.json`:

```json
{ "maxPlayers": 4, "netPort": 27750 }
```

How many players a game is for is a property of the game - a scene with four
characters authored into it is a four-player game wherever it is served - so it
lives with the game and `NetSession::host` takes no default for it. Where a
particular run listens is deployment data, so `--port` overrides it, and an
address given with no port falls back to the project's, which is what lets
serving a project and joining it agree with nothing typed.

No flag is positional, so the project directory
stays `argv[1]`. An argument a host does not recognise is refused before it opens
a window or loads anything - skipping it is what would make `--conect 10.0.0.4`
a silent single-player game.

**There is one way to host, and the runtime is not it.** A dedicated server is
`vkm_server`, a host of its own rather than a flag, and it referees without
playing: it owns no entity, so `isMine()` answers no everywhere with no special
case for it, and every player in the game is a client - predicted, interpolated
and corrected like every other. Playing on the machine that serves is a second
process, joining it. `--host` is refused by name rather than as an unknown
argument, and says what to run instead.

`vkm_server` opens no window, installs no render backend, and runs where
`DISPLAY` and `WAYLAND_DISPLAY` are unset - on a machine with no display at all.
It still needs the GL and X11 *shared libraries* present, because `vkm_core`
links glfw and GLEW.

The system stack is identical to the runtime's, and that is the point: an
authority that simulated differently from the clients it corrects would not be
an authority. What is skipped is the window, the icon and the GL debug switch,
none of which a simulation consults - and the frame cap is *not* skipped with
them, because with no window there is no vsync and nothing to block on, so an
uncapped loop spins a core for frames nobody draws.

Measured on physics_lab: 9.5 MB against the runtime's 20 MB, 18 MB resident, and
40% of one core at 128 Hz rather than the 100% a busy loop would take.

## What a project writes

Two things: what a player is, and four questions inside the behaviors that
move things.

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

A module without the entry is normal and silent in the runtime and the editor -
a single-player project needs none - and `vkm_server` refuses to serve it. A
project that has one still runs offline, because a session nobody
joined behaves exactly as no session.

Returning a null id refuses the connection, which is how a game says "the match
has started".

### The four questions

On `Behavior`, so a networked behavior reads like a single-player one:

| Ask | When | Offline |
|-----|------|---------|
| `isSimulated()` | before writing a transform, a velocity, anything the world can see | always true |
| `isMine()` | before touching the camera, the pointer, the HUD - anything about *this* player | always true |
| `command()` | instead of reading the device inside a fixed update | the local command |
| `isReplaying()` | before anything that *presents* - a clip, a sound, an effect | always false |

`isSimulated()` and `isMine()` are different questions and the difference matters:
a server simulates every player and owns none of them.

`command()` is the same call in every role. Offline and on the owning
client it is the local player's input; on a server it is what that entity's
player sent, run in the order they made it. An entity no player drives reads as
nothing held.

physics_lab's walker asks all four: three guards and one substitution. Offline
`isMine()` and `isSimulated()` answer yes for every character, and a scene authored with four
would move all four from one keyboard, so the lab adds a question of its own:
offline, only the first authored character - the seat a server hands the first
player to join - takes input (`isOfflineSeat`, `examples/physics_lab/src/lab_walker.h`).

### What replicates

`Transform`, `Rigidbody`, `CharacterController`, `Ragdoll` and `NetSpawn` are
registered by the engine (`registerEngineNetTypes`). `NetSpawn` is what a
spawned root is - see *Spawning*, below. `CharacterController` and `Ragdoll`
carry one field each and are worth saying why:

- **`CharacterController` sends `grounded` and nothing else.** It reads like an
  observation each end could make for itself, and it is not. The contact under a
  resting capsule exists because gravity presses the capsule into the floor
  every tick; an end that does not simulate the body applies no gravity to it and
  holds its position only to the millimetre the wire carries, so it sees the
  character in exactly the right place and finds nothing underneath. Measured on
  the same character at the same tick: `y = 0.005000, touched` on the server and
  `y = 0.005005, not touched` on a client. Movement input, the step-up state and
  the slope normal stay local - those are what a simulating end derives from
  this and from the command, and sending them would overwrite an owner's own
  prediction with an answer a round trip old.
- **`Ragdoll` sends `active` and nothing else.** It decides what the rest of the
  character costs; see *What does not replicate*.

A project adds its own from `vkmSetupNetwork`, and
only from there - the schema is rebuilt from nothing each time that entry runs,
so a type registered from `vkmRegisterBehaviors` is wiped before play:

```cpp
NetSchema::get().replicate<Health>("Health");
```

which requires `netEncode(const Health&, BitWriter&)` and `netDecode(Health&,
BitReader&)` to be visible - free functions beside the component, found by
ordinary lookup or by ADL. A component registered without a codec is a compile
error at the registration line.

### What does not replicate

Three rules, all the same rule: **replicate causes, not effects.** State that
every end recomputes from something already on the wire is not merely wasted
bandwidth - it arrives as a second writer for a value that already had one, and
which of the two a frame shows then depends on how many fixed steps that frame
happened to run.

- **A ragdoll bone whose ragdoll is inactive** (`isPosedByAnimation`). Inactive,
  the bodies are kinematic and placed from the animated pose every tick, and the
  clip is chosen from the body's velocity, which does replicate - so both ends
  derive the same skeleton from the same input, and sending the bones would
  spend most of every snapshot on them, leaving the props and the other players
  to compete for the rest. Active, it inverts:
  the solver drives the bones and the pose follows them, so they are the answer
  rather than a copy of it and every one travels. That is what `Ragdoll::active`
  is on the wire for - a client that missed the switch would pose twenty bones
  from a walk cycle while the server had a body falling.

  The clip phases need not match. Each end advances its own clip time, so two
  ends hold the same walk at different points in it; a kinematic bone decides
  nothing while the ragdoll is off, and the first snapshot after it comes on
  settles the difference.

- **A static body** (`isStaticBody`) - one whose motion is `RigidbodyMotion::Static`.
  It is the scene file, both ends read the same one, and what the wire sends
  back is a quantised copy replacing a value that was already exact. A
  kinematic body is immovable to the solver too, and still travels: a script
  moves it. A static body `NetSession::spawn` built is the one exception, and
  only by half: its `NetSpawn` travels, which is what it is and the exact pose
  it was built at, and nothing else about it ever does.

- **An entity inside a prefab instance** (`Prefab::isInsideInstance`). Both ends
  build it from the same file, into the same slot, and its root carries it: see
  *Identity is the scene slot*.

`netSilence` asks the three for one entity, and is what the editor's inspector
shows. A server classifies the whole world once per send round instead
(`NetSilenceMap`), however many peers it then writes for: the answer depends on
the world and not on who receives it, and a ragdoll bone found one entity at a
time is a walk of every ragdoll's bones. A client drawing its tracks marks the
posed bones once per call the same way. Both go through the same rule, so the
round's answer and the inspector's cannot differ.

## What it does not do

- **A spawned prefab's children do not replicate.** Each end builds them from
  the file into the same slots (the spawn names them), and they follow their
  root, so only the root's state is on the wire. An animator poses them
  correctly on both ends; a ragdoll does not, so a ragdoll belongs on a
  character the scene author placed. Lifting this is a matter of not silencing
  them - their slots already agree - at the cost of their bones' bandwidth.
- **Only players are lag-compensated.** A moving platform, a swinging door or a
  thrown crate is judged against the present. Players are what players shoot at,
  and remembering every body means a ring per crate.
- **Nothing a client says has to arrive.** Input is already repeated across a
  twelve-tick window (188 ms at the default 64 Hz), and nothing else goes upstream.

- **A connection lost mid-game leaves the world holding still.** The session
  goes to `NetRole::Disconnected` - deliberately *not* Offline, which is the
  opposite state on the one question that matters: Offline decides everything
  because there is nobody else, and a dropped client decides nothing because it
  spent the session being told and has just lost the teller. Made Offline it
  would inherit a world it had only ever been shown, every unowned body handed
  to its physics at once from interpolated positions. What to do about it is the
  game's - a message, a menu, a lobby - and `isDisconnected()` plus
  `lastError()` say what happened. A game that never asks shows a frozen world.

  A join that never got as far as playing ends the same way. Refused - full, a
  different build or world, declined by the game - or answered with a Welcome
  that does not decode, the client goes to `Disconnected`, not Offline: a
  `vkm_runtime --connect` that was turned away holds its world still and logs
  why, once a second, rather than quietly becoming a single-player game.

- **No fragmentation.** A datagram over 1200 bytes is refused, not split. One
  lost fragment loses all of it, and the budget exists so it never comes up.
- **No GL-free server binary.** `vkm_server` links no render backend, but
  `vkm_core` links glfw and GLEW, so the shared libraries must be present even
  though no display is. Freeing it of those means routing every glfw and GLEW
  call through an internal seam in a class every host uses; simplicity
  outranks features.

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### How it works

#### Identity is the scene slot

An entity is named on the wire by its slot in the `Scene`. Both ends loaded the
same scene file, which records the slot, so they agree with no handshake, no
mapping table, and nothing to keep in step. `SceneSerializer` writes
`entity["id"] = id.slot()` and restores through `createEntityAt`.

An entity built after the scene loads has no slot the other end knows, so
content spawning names one: `NetSession::spawn` builds a prefab and the root it
built carries which prefab it was, at the server's slot (see *Spawning*), and
the slot each entity below it took. The client builds each into the same slot,
so the two ends' slots go on agreeing and a root the server names later is
free on every client. What remains is narrower - **an entity inside a prefab
instance says nothing on the wire.** The root replicates and the hierarchy
carries the rest, which is correct for anything posed the same way on both
ends and wrong for a ragdoll, whose bones fall on the server and are never
heard about. The editor says so on the entity: see *In the editor*, below.

**A client does not create entities of its own while connected.** Its slots are
the server's names: an entity a client makes for itself - an effect, a marker -
takes the next free slot, and the server may give that same slot to something it
spawns. Every snapshot about that slot then lands on the client's own entity,
and the spawned prefab is built into it unless it is part of an instance this
end built, which is said once and left alone. Every entity takes a slot, so what
a client wants to show only itself belongs in a component on an entity it
already has.

#### Spawning

`NetSession::spawn` builds the prefab on the server and puts a `NetSpawn` on the
root it built: the prefab path, the pose it was built at, and the slot each
entity below the root took, by `PrefabEntity` uid (`Prefab::BuiltSlots`, for up
to `NET_SPAWN_MAX_SLOTS` of them; a larger instance is sent with none, and the
server says so). `NetSpawn` is a
replicated component like any other, so it goes out in the same snapshot entry
as the root's state, is said again until the client confirms it, and reaches a
player who joins an hour later exactly as it reached one who was there - a
joiner's baseline holds nothing, so its first snapshot says what every root is.
The client builds the same file into the root the snapshot created at the
server's slot, each entity into the slot the spawn names, so the subtree's
state is never on the wire; only the root is described. A slot the client
already holds something in is said once - the two ends' slots part there.

The pose goes whole and unquantised - position, rotation and scale as the floats
the server used. It is said once, and a static body is never described again,
so a rounded rotation or a dropped scale would be a difference between the two
ends that no later packet corrects. That is also why **silence does not cover
it**: the rules that keep a static body off the wire keep its *state* off, and
`writeSnapshot` still writes a silenced entity's `NetSpawn` and nothing else. A
client building a root that no snapshot placed puts it at the spawn's pose, bit
for bit.

A client builds after the snapshots of the frame are read, not while one is
decoded, and at most `NET_SPAWN_BUILDS_PER_FRAME` a frame: each build reads a
file, and how many arrive is the server's to decide. What waits keeps its
`NetSpawn` and whatever the snapshots have said of it since, and is built the
next frame. The build puts what the snapshots said back over what the file
authored, because the server holds those values confirmed and will not say them
again. The client then drops the `NetSpawn`, so what still carries one is
exactly what is waiting. A root that already holds an instance, or part of one,
is not built over.

What `NetSession::spawn` would put on the wire has to be sayable there, so on a
server it refuses, before building anything, what a client could not safely be
told (`isSayable`). The path is a file a client opens on the server's word, so
it must name a prefab inside the project and nothing else: relative, ending in
`.json`, at most `NET_PREFAB_PATH_MAX` characters, with no `..` component, no
backslash or colon and no control character. Otherwise a hostile server names
`\\attacker\share\x.json`, and a Windows client hands the attacker's share its
login hash in the course of opening it; on Linux a FIFO blocks the frame. A
component a Windows device answers to - `con`, `nul`, `com1` - is refused for
the same reason, and the rule is judged on the characters, so a Linux server and
a Windows client agree on it. The pose must be finite, with a rotation of some
length and a scale within `NET_MAX_SCALE`: refused at both ends rather than
clamped at one, which would leave the two holding different bodies for good.
The decoder refuses the same: a spawn it will not believe decodes with no path
and builds nothing, and the stream stays in step.

**There is no despawn.** Destroying the root with its subtree on the server -
`HierarchyOperations::destroyHierarchy`, or `Behavior::destroy` - is all of it.
The snapshot reports the root gone by occupant, and the client drops its
subtree. That report comes before any entry in the same snapshot, so a slot
destroyed and spawned into again between two snapshots meets the removal first
and the new root is built fresh, never kept as the old instance. Destroying the
root alone, with `Scene::destroyEntity`, hands its children up to the top level
on the server, where they are no longer inside an instance and start to
replicate as entities the client never built.

#### Snapshots: presence-delta against what was confirmed

A snapshot carries a component only when its encoded bits differ from what that
connection has **confirmed receiving**. The delta is on presence, never on
values.

An absolute entry held back for three snapshots is unconditionally correct the
moment it lands, and the receiver decodes it from the packet alone. A value
delta would not be less safe if it were taken against what was confirmed, as
presence is: a delta against the last packet *sent* chains, so one lost packet
poisons everything after it, but one against the last state *confirmed* does
not, and the sender already holds those bytes (`NetBaseline`). It is declined
for what it costs the receiver - a copy of every value it was sent, keyed by the
sequence that carried it, to know which one each delta is against - which at
four to eight players is not worth the bytes it saves. Presence is also why
joining and playing are one code path: a connection that has confirmed nothing
differs from the world in every component, so its first snapshot is the whole
world because of the rule rather than as an exception to it.

**Confirmed, not sent.** What a snapshot claimed is held aside until the
acknowledgement comes back. Folding it in at send time describes a client that
received it - and a client that did not is never told again, so a body that
moves through a burst of loss and then stops is frozen on that client until the
match ends, with nothing anywhere reporting a problem.

Comparison is on the quantised bytes, not on floats, so a settled world goes
quiet. A body at rest, once confirmed, costs nothing; when it is sent, its
`Rigidbody` is three bits.

**What has gone is reported by occupant, not by slot.** The baseline remembers
which entity each slot held, and one no longer alive is reported lost until the
report is confirmed. Asking whether the slot is alive instead misses an entity
destroyed and replaced between two sends - the slot is alive both times - and
the receiver decodes the newcomer onto the occupant it still holds, keeping every
component the server's entity never had. The client applies what is lost before
any entry, so the same snapshot can take the old occupant away and build the new
one at its slot.

The one exception: **the connection's own entity is written every snapshot**,
moved or not. The receiver does not hold what it was last told about that one -
it holds its own prediction. It is also written before anything is prioritised
and is never deferred, because it is what reconciliation compares against. Its
`NetSpawn`, when it has one, is the exception to the exception: what the entity
is was never predicted, so it is said once like anyone's.

#### What a value costs, and what it owes

A position is carried to the millimetre, a rotation as its three smallest
components at eleven bits each, a velocity to the centimetre a second. The
figures are in `net/wire/quantize.h`; two properties of them are load-bearing and
neither is obvious.

**Zero is exact, for every signed field** - a rotation component, a velocity,
and a command's axis, which share one quantiser (`Quantize::toSigned`). A
signed value uses one fewer than half its codes either side of a middle code
that is exactly zero. Codes spread evenly across the range would leave the
midpoint *between* two of them, so zero would be the one value that cannot be
said - and zero is not an ordinary value: almost everything in a scene is
unrotated and almost everything in it is still. A zero said inexactly reads as a
tilt on every unrotated body and as drift on every resting one, and the settled
world never goes quiet.

**A rotation error is an angle, so what it costs grows with the thing turned.**
A quarter of a degree is invisible on a crate and lifts one end of a sixty-metre
floor by ten centimetres - and a character standing on the low end finds nothing
beneath it and falls through the world. That is why rotations are eleven bits a
component, and it is the other half of why a static body is not
sent at all: the scene file is exact where the wire is not.

**A scale is carried whole, and both ends hold it to one bound.** It is rare
and it is never simulated, so it travels as three raw floats when it is not one,
and a raw float is bounded by nothing on the way. `netScale` is the bound: a
component that is not finite makes the scale one, and a finite one past
`NET_MAX_SCALE` is brought to it. The reader applies it to what it reads, and a
server applies it to every Transform a send round describes before writing
anything, so what it simulates is what every client is told. Held at the reader
alone, a body authored at two thousand would be two thousand on the server and
1024 on every client, a disagreement no later snapshot corrects. A body that is never
described - a static one - is the scene file's at both ends and is left alone.

#### The budget

`NetBudget::bytes` is a ceiling, not a target. What does not fit is deferred,
and a deferred entity's priority rises while it waits, so it goes first next
time rather than losing every round to whatever moved most recently.

Letting a busy frame produce whatever it produces is what turns a collapsing
tower into a datagram the socket refuses whole - the client then sees nothing at
exactly the moment there is most to see.

Measured: 117 bodies all awake and moving arrive in four snapshots, worst body
1177 bytes against the 1192 a datagram carries past the connection's header
(`NetConnection::MAX_PAYLOAD`, which the budget defaults to; a session takes
its own headers off it). A moving body is 24 bytes, so fifty fit one packet.

#### Commands: redundant, in order, once

A command packet carries past commands, not just this tick's: every one of the
**newest twelve** the server has not said it has, and before those any command
no packet has carried yet, however old (`firstCommandToSend`). An axis recovers
from loss on its own - the next packet says where the stick is now. An edge does
not: `pressed` is true for exactly one command, so one lost datagram loses that
jump permanently and nothing reports an error.

Twelve ticks is a 94 ms window at 128 Hz and 188 ms at the engine's default of
64. A command rides every packet sent inside it, so a burst of losses is
survivable as long as one of those packets arrives: eleven consecutive at the
default rates, where one command is made per packet sent and so rides twelve,
and five at 128 Hz, where two are and it rides six. The window is the invariant; raising the send rate buys more
packets inside the same window, not more protection.

The window slides with the newest command, not with what comes back: a window
that slid on the server's replies would trail the player by what the round trip
does not cover, where sliding with the newest keeps a command one trip plus the
server's queue old. Each snapshot says which command has arrived
(`NetCommandBuffer::newestSequence`), which only stops a command being resent
once it is there. The never-sent rule is what a stutter needs: a frame
can run more ticks than the window, and one passed over for being old is a tick
the client predicted and the server never ran. A packet holds up to sixteen, so
a quarter-second stutter goes out over two frames rather than one.

Commands go in unbroken runs - each one sequence and one tick after the last,
which is what every tick makes - so a packet carries the first command's two
numbers and nothing per command after it.

Retransmission is the other answer and it is the wrong one: by the time a
retransmit arrived, the tick it belonged to would be long past.

The server runs one command per tick, in the order the player made them, and
never twice. They are **not** addressed by tick number - the two ends count
ticks on separate clocks, so the client's tick 400 means nothing against the
server's. Running them in order gives the property that matters with no clock to
synchronise. The client's own tick number is carried and handed back in the
snapshot, so the client knows which of its predictions has been judged.

How deep that queue stands is the client's to keep shallow - see *Pacing*,
below. Its two edges are what happens when it is not.

A backlog is latency every command behind it waits through, so past
`NetCommandBuffer::MAX_DEPTH` - ten - the oldest are skipped down to it in one
tick. Skipped edges are folded into the command that runs - an axis can be
skipped, an edge cannot. With the client pacing the queue a command or two above
empty, this is for a burst, a stall's worth of commands arriving at once. It sits
above that cushion by as much again as the jitter the cushion is for, so a link
that jitters is never trimmed: a bound the cushion and the jitter together can
reach has the pacing refill what the trim empties, over and over.

An empty queue is the other direction. The tick runs anyway, on the last
command's axes with its edges cleared - a held key is still held, and a press
that already fired would fire again on every tick of the gap. Past
`NetCommandBuffer::REPEAT_TICKS` dry ticks the axes are zeroed too: a link
that has gone stops the body rather than walking it on.

That repeat **claims the client tick it stood in for.** A tick of movement
happens either way, and a snapshot naming the tick before it would describe a
pose the client never predicted - an error the client did not make, replayed
away on every snapshot for as long as it keeps walking. The real command for
that moment is discarded when it arrives, with its edges folded into the next
that runs, or one moment of input would move the character twice.

A repeat may only run as far dry as the buffer is allowed to run deep. Past that
the other end is not merely late, it is slower than this one, and a server
running ahead of everything that client will ever send would discard all of it.

#### Pacing

Two clocks free-running at one nominal rate drift, and the depth of the server's
queue of a client's commands is that drift added up. Left alone it walks to one
edge and stays there. Measured on a simulated link at 128 Hz with up to 15 ms of
jitter, a client whose clock ran 3% fast had 231 commands a minute skipped off
the top, and one 3% slow had 4217 of its 7680 ticks a minute run on a repeat -
each a misprediction the client then replays
(`testAClientPacesItsTicksToTheServersQueue`, which prints its own counts).

So the server says in every snapshot how low that queue ran since the last one:
the fewest commands waiting at any tick, counted before the tick took one
(`NetCommandBuffer::lowWater`), in four bits. The client steers that toward a
target by bending how fast its clock turns wall time into ticks, by at most 5%
either way (`NetPacing`). The target is two commands - one waiting behind the
one that runs - plus twice how far the readings stray from their mean, so a link
that jitters keeps a deeper cushion, and never more than half of `MAX_DEPTH`. A
proportional term answers a change at once; an integral term settles at the
steady bend a drifting clock needs. The same two clients, paced, lost nothing -
no repeat and no skip in the minute - with the low water held near three.

**The gains are set by the loop's own delay.** A bend shows in the readings a
round trip after it is made, and the smoothing adds about a sixth of a second,
so the proportional gain closes a command of error over two round trips plus a
quarter of a second, and divides by the tick rate, which is how many commands a
per cent of bend is worth: `1 / (tickRate * (2 * roundTrip + 0.25 s))`. The
integral takes a fixed share of it. Fixed gains are right for one link and no
other: tuned at 30 ms they would bend past the answer on every round trip of a
200 ms link and swing between the two bounds. Scaled,
the 200 ms link loses nothing either, and at the engine's default of 64 Hz the
same holds.

**A networked session runs at most `NET_MAX_TICK_RATE` - 128 Hz**, two commands
to a command packet, and `host` and `connect` refuse a project that ticks
faster. `MAX_DEPTH`, the resend window and the cushion are counted in commands,
and each packet lands as many commands as ticks ran since the last: at 256 Hz a
packet's worth takes most of the room above the cushion, the queue's top
reaches the trim and ticks run on a repeat every few seconds. Deriving the
constants from the rate instead would widen the depth on the wire and resize the
redundancy window past what a datagram holds at the widest command, to serve
rates no project uses.

**Only the pacing bends.** The step a tick advances by is
`Clock::getFixedStep()` on every end and is never touched, so a tick is the same
tick on both and a replay re-runs exactly what the server ran. What changes is
how many ticks a second of wall time buys: `Engine::run` hands
`NetSession::pacing()` to `Clock::setPacing` each frame, and that is exactly one
offline, on a server, while joining and once disconnected - nothing but a playing
client ever ticks off the wall clock's pace.

The reading is bounded by its width and the bend by `NetPacing::MAX_DILATION`,
so the most a server can do to a client's clock, whatever it sends, is five per
cent. The status line says where each end stands: a client's says how low the
queue is running against what it aims at and how fast it is ticking, a server's
how many ticks have run on a repeat and how many commands were skipped.

#### Prediction and correction

A client runs its own character at once, on its own input. The server's answer
arrives describing a moment already passed, so it is compared rather than
believed: the client holds what it predicted for each tick, and the snapshot
says which tick the server has now judged.

Below 3 mm nothing happens at all - that is quantisation plus float noise, and
re-running ten ticks of physics to close it would be paying for nothing. That
floor is what keeps the expensive path out of the common case.

Above it, the server's answer stands and **every tick since is run again from
it**. Each of those ticks was computed on top of the mistake, so recomputing
them is the only way to be right in one step rather than over the following
second. The replay runs three systems - behaviors, physics, the character
controller - and not the three that fire something (see *Replay*, below).

The simulation therefore takes a correction exactly and at once. The *picture*
does not: a 40 cm correction landing instantly is a character seen to teleport.
So the difference between where a body was about to be drawn and where it now
is becomes an offset that decays over about a sixth of a second, applied after
the ticks and taken back off inside the same frame. No tick ever simulates from
a position that was chosen to look nice.

The offset is in the component for one scope and no longer. `NetSession::Drawn`
is constructed around the systems that read a transform to draw it and takes the
offset back out in its destructor - a pair written as two calls is a rule the
next edit between them can break silently.

One offset per body this end predicts, not one for the character. A client
predicts whatever it is leaning on as well, and those bodies take the same
correction from the same replay - a crate that jumps under a character that
eases is the two of them visibly disagreeing about one push. A lapsing lease is
smoothed by the same machinery for the same reason: the body did not move, this
end simply stopped being the one deciding where it was, and handing it back to
an interpolation four ticks in the past would show as a jump the moment a
player stops pushing.

**What the owner keeps.** A snapshot writes every replicated component of the
owner's own entity but its `NetSpawn`, as of the tick the server confirmed.
Only `Transform` and `Rigidbody` are set aside first and put back when the prediction stands
(`holdPredicted` / `restoreHeld`), and only the position decides whether it
does. Every other replicated component on that entity - `CharacterController`'s
`grounded`, or a project's own - keeps the server's round-trip-old
value until this end's systems next write it, and a disagreement in one of
them never causes a replay. That is harmless for a value the owner's systems
recompute before anything reads it: the controller sets `grounded` at the top
of its tick, so only a behavior that reads it earlier in the same tick sees the
old one. It is wrong for a value the owner advances - a cooldown, a charge, a
count - which steps back a round trip at every snapshot. Such state stays out
of the schema, or lives on the server alone and is shown rather than predicted.

Measured on one rule and one stream of input: once the two ends are in step the
prediction is out by 0.0 mm and nothing is ever re-run. Against a wall the
client could not see, a 30 cm disagreement is re-run across 20 ticks and lands
on the server's answer exactly - 0.00000 - rather than near it.

#### Replay

A replayed tick must be a function of the world and the command: same state,
same input, same result, and doing it twice is the same as doing it once.
`System::isReplayed()` states which systems are, and only those run in the
replay pass.

| Replayed | Not replayed |
|---|---|
| `BehaviorSystem` - turns a command into movement | `AnimationSystem` - a clip advanced twice gains a tick |
| `PhysicsSystem` | `SkeletalAnimationSystem` - a marker crossed twice is a footstep played for one step |
| `CharacterControllerSystem` | `RagdollSystem` - writes bone state from the pose, which both ends do for themselves |

Three more things the replay pass deliberately does not do, each of which
would otherwise be a bug:

- **No `EventBus::flush()`** - flushing again delivers the live queue twice and
  drains it before the real tick's flush finds it.
- **No `InputMap::beginTick()`** - it stamps a new sequence, re-reads the axes
  as the device stands now, and drains the edge latch. A replayed tick runs the
  command it originally ran, which the session hands over.
- **No collision or trigger events** - a trigger entered once must not fire
  once per replayed tick.

Behaviors are ticked only for entities this end simulates while replaying,
which on a client is the one it drives and the bodies it has leased. And a body
this end does not decide is made unpairable for the pass, so the broadphase's
static-against-static skip drops every pair that involves neither the character
nor a body it leases - which is what stops a replay costing a full tick over every body in the world.

What a replay does **not** rewind is unreplicated per-tick state: the character
controller's step latch, animation, a behavior's own members. Those carry
forward from where they now are. A replay that begins mid-climb re-runs its
ticks with the latch as it stands, not as it stood.

#### Leasing

A client also simulates the bodies its own character is touching, and the
bodies those are touching, out to the edge of the pile. Without it a push is
predicted as walking into a wall - the crate cannot move, so the character
stops - and is corrected the moment the server says otherwise. That is the
softness felt on every contact.

The island is the owned body's, joined over the contacts and joints of the
tick that just ran, computed in `PhysicsSystem` because that is the only place
the graph exists, and reported to the session. Three rules shape it, each for a
reason:

- **Never through an immovable body.** A crate rests on the floor and the floor
  touches everything in the level, so a closure that stepped through statics
  would lease the whole world in two hops.
- **Never onto another player's character.** That one is theirs to predict, and
  predicting it here means guessing at input this end never saw.
- **Joints are edges too.** A jointed pair is skipped before the shape tests,
  so a shoved ragdoll would otherwise lease the one limb it was touching and
  leave the rest immovable, with the joint pulling against a frozen body.

A lease expires 32 ticks after the last time it was reported, not when contact
breaks: a bouncing crate loses contact for a tick at a time, and a lease that
lapsed on each bounce would hand the body back and forth. In ticks rather than
seconds, like every constant here, so it follows a project's tick rate.

Physics has no case of its own for a lease: `simulates()` gates both the gather
and the writeback, so a leased body gets its real mass into the solver and its
solved pose back out.

The lease is one tick late by construction: the gather asks `simulates()` at the
top of a tick and the island for that tick is only known after the narrowphase.
The first tick of a contact still pushes an immovable body.

Two clients leaning on one crate both predict it and are both corrected toward
the server's one answer every snapshot. What each player sees is their own shove
being immediate and firm, and the crate drifting by roughly the other player's
contribution until the snapshot arrives - a converging disagreement lasting under
a tenth of a second, not two worlds.

A leased body is corrected the same way the character is, and by the same
decision. Both are held aside while a snapshot is read, and then either both are
put back - the prediction was right - or neither is, and the replay re-runs the
ticks over both from the server's answer. Keeping one and discarding the other
would leave them disagreeing, and a crate the character is leaning on decides
where the character ends up: an uncorrected crate is a character wrong on every
tick, which is a replay on every snapshot.

The interpolation track is held rather than forgotten so a lapsing lease has the
server's last word to slide back to.

#### Interpolation

Everything the client does *not* own is drawn behind the newest news, between
two positions the server actually gave. A server sends sixty-four times a second
and a client draws a hundred and forty-four times a second, so most frames have
no news; drawing the newest thing heard makes everything move in steps, and no
amount of bandwidth fixes it.

How far behind is two snapshots - four ticks at 128 Hz - or, on a link that
jitters, one snapshot interval plus twice the measured jitter, up to four. Two
is enough on a steady link, even one losing a snapshot in ten; past about 30 ms
of jitter a late snapshot runs the clock into the newest sample, where it holds,
and the world pauses. The jitter is how far each snapshot's arrival strays from
when the ticks between it and the last say it was due, smoothed as the pacing
smooths its readings, so a lost snapshot is not mistaken for a late one and the
two clocks' drift cancels between neighbours. Measured over a minute at 144 fps
with 45 ms of jitter and one snapshot in ten lost, a fixed two snapshots stalled
34 frames and the measured delay 5; a link of 15 ms keeps the two and pays
nothing (`testTheDelayRidesOutTheJitterItMeasures`).

The clock that decides what is drawn is corrected by running slightly fast or
slow - up to ten per cent - and never runs past the newest sample. A stall shows
as the world pausing, which is what it is. Only a clock more than twenty ticks
from where it should be is moved there outright: that is a stall or a
reconnection, and easing across it would take longer than it took to open.

What the server said is put back before each snapshot is read. Presence is
measured against what the receiver was last told, so leaving the smoothed value
in the component would make "unchanged" mean "unchanged from the smoothed
value", and a still body would drift.

#### Lag compensation

A player aims at what they can see, which is a moment already past: their
screen shows the world as it was a round trip plus an interpolation delay ago.
Judged against the present, a player who aimed correctly misses - and the better
their aim and the worse their connection, the more often, which reads as the
game being broken rather than as latency.

So the server keeps the last 64 ticks of every player's pose and puts them back
for exactly the length of one query:

```cpp
{
    NetSession::Rewind rewound(net(), scene(), entity());
    RayHit hit;
    if (raycast(scene(), origin, Math::computeForward(command().view), range, hit, filter)) { ... }
}
```

Offline and on a client the scope does nothing, so that is a plain query in a
single-player game. A scope opened inside another - a helper opening its own
for the shot it was handed - changes nothing: the world is already at the
outer one's moment, and only the outermost scope's end puts the present back.

**The moment is not estimated from ping.** A client already knows which server
tick it drew - its interpolation clock runs in the server's own tick numbering -
so it says so in its command packets, five bytes a packet, whole and fraction.
What arrives already accounts for that client's latency, its interpolation delay
and its frame time, with no clock to synchronise and nothing to double-count.
Measured: a client reports it is looking 5.3 ticks behind the server's present.

What it drew belongs to the newest command in that packet, and that is not the
command a shot is judged under: the one running has waited in the server's
queue behind newer ones, so it was made - and aimed - as many ticks earlier as
it is older than the newest. The server keeps the pair and rewinds to the drawn
tick less that difference. Judged by the newest instead, every shot lands as
late as the queue is deep - 23 to 78 ms at 128 Hz, 14 to 47 cm on a runner at
6 m/s.

It is a claim from a client, so it is clamped to what is remembered. That bounds
how far a shot can reach back; it does not bound how often, which is a judgment
about a game rather than about a wire.

Three limits worth knowing. Only **players** are rewound - a moving platform, a
swinging door or a thrown crate is judged against the present, because players
are what players shoot at and remembering every body means a ring per crate.
Only a player's **root** is rewound: its bones follow it, but the pose they
hold, a crouch or a lean, is the present one. And the history is 64 ticks, which
is half a second at 128 Hz and a second at the engine default - so how far back
a shot may reach is a function of a project's tick rate.

#### Why UDP

Because nothing here wants what TCP guarantees, and TCP does not let you
decline it.

**A lost snapshot is not a hole to fill.** Presence is measured against what the
receiver *confirmed*, and it confirms nothing it never received - so the next
snapshot already describes everything the lost one would have. There is nothing
to retransmit, and a transport that retransmits anyway is paying for a repair
that was not needed.

**In-order delivery is backwards here.** TCP holds everything behind a lost
segment until it is resent, so a snapshot describing the present would wait on
one describing a moment already past. The newest state is the most valuable and
the previous one is worthless the instant it exists; ordering inverts that.

**Neither direction wants a retransmit.** State is superseded by the packet
after it. A command cannot be resent usefully at all - by the time the copy
arrived, the tick it belonged to would be long gone, which is why every packet
carries the newest twelve the server has not heard instead, so one command rides
twelve consecutive packets at the default rates and is already gone before a
retransmit could be asked for.

What genuinely cannot be dropped - an entity gone, a prefab spawned - is not a
message either. It is state: a lost report is said again by the next snapshot
because the receiver never confirmed it, the same rule every position rides, so
nothing on the wire has to be delivered rather than described.

The property this rests on is tested rather than assumed: with a third of
every snapshot lost, some delivered twice and others reordered, the two worlds
still agree to within a quantisation step, and the settled world still goes
quiet afterwards (`testAWorldConvergesThroughALossyLink`, which prints its own
counts).

Sequencing and the acknowledgement bitfield are machinery TCP would give free;
they are the price of not paying for ordering.

#### The connection

Every packet carries its sequence, the newest sequence seen coming back, and a
bitfield of the thirty-two before it. Acknowledgement rides traffic that was
going out anyway; nothing is sent for the purpose and nothing is retransmitted.

Newer-than is a distance question, not a comparison: sequences are sixteen bits
and wrap after 65536 packets, which at sixty-four commands a second is about
seventeen minutes. A
plain `a > b` is a session that dies at a fixed time after it began.

A packet older than one already accepted is refused - at this level a reordered
packet and a stale duplicate are indistinguishable, and applying the earlier of
two is a world assembled from two moments.

`MAX_DATAGRAM` is 1200 bytes and the socket refuses anything larger rather than
fragmenting.

On Windows alone, a datagram sent to a port nobody holds comes back as an ICMP
"port unreachable" that fails the socket's *next* read - about an unrelated
datagram. On a server's one socket that is a client that has gone ending the
read loop of every client still there. `UdpSocket::open` turns the report off
(`SIO_UDP_CONNRESET`), so a read only ever fails for its own reasons.

#### Rates

Snapshots and commands both go out 64 times a second, driven by wall clock
rather than by the render loop. A machine drawing at 144 would otherwise send
144 snapshots a second, and one drawing at 20 would starve every client it has.
Neither number has anything to do with how often the world needs describing.

The snapshot rate is what decides how late another player looks, because the
interpolation delay is counted in snapshots: two of them is 62 ms at 32 a second
and 31 ms at 64 - eight ticks against four at a 128 Hz tick - and the difference
is visible. What 64 costs is bandwidth and nothing else:
a snapshot does not get smaller when they are more frequent, because the
entities that move move every tick either way, so the measured per-peer cost is
about 17 KB/s against 9 KB/s at 32. That is the trade, and 32 is the value to
give `NET_SNAPSHOT_RATE` (`net/net_session.h`) in a build that would rather have
the bytes.

The wall clock sets the floor: never faster than the interval, whatever the
frame rate. The frame rate still sets the ceiling, because `send` is called once
a frame and deliberately sends at most one packet when it is - a machine drawing
at 20 sends 20 commands a second, not 64. That is the right failure: a client
that has stalled owes a burst nobody wants, and the alternative is a stutter
answered by a flood. Measured on a software rasteriser at 1 fps, a client sends
one packet a second and the round trip it reports is one frame, which is the
honest number and not a fault in the timing.

A frame that overran its interval carries the remainder forward rather than
losing it, so the rate does not drift slow; a frame that overran it by a lot -
a stall, or a window a compositor has stopped drawing - sends once rather than
firing off the whole burst it now owes.

#### The frame

`NetSession` is owned by `Engine` by value, like the clock and the event bus,
and reached through `FrameContext`. It is not a `System`: it brackets the frame
rather than taking part in it.

Behind it are two halves and what they share: `NetServer` holds the seats, the
join handshake, the command queues and the outgoing snapshots; `NetClient` the
connection, the prediction, its correction and the interpolation; `NetCore` the
socket, the buffers and what the two ends agreed. Both halves always exist; the
role decides which one the session asks.

```
advance(dt)              wall clock, so a paused editor still notices a peer leaving
receive(scene, res)      apply what arrived, reconcile, seat whoever joined,
                         build what the server spawned
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

#### In the editor

The editor never hosts and never joins - its session is Offline for the life of
the process - so there is no connection to show and nothing that varies at run
time to inspect. Two things about a *project* do vary, and the editor shows both:

**File > Project > Settings...** edits what `project.json` records, including
how many players the game is for and the port it is served on. Those are read on
every run, and this is where they are set rather than by hand in the JSON.
The Multiplayer card also lists what the game puts on the wire and its
fingerprint - exactly the pair a refused join prints - so a mismatch can be
compared against a server's log line rather than debugged as a world that
decodes into nonsense.

**The inspector's identity header** says what the other end will see of the
selected entity: its wire slot and which of its components replicate, or a
warning naming whichever of the three silence rules keeps it off the wire -
inside a prefab instance, posed by animation, or a static body - with the
reason as its tooltip. It
is drawn only for a project that replicates something and only for an entity
carrying some of it, because most entities are not on the wire.

The editor builds the schema for those two alone, through the same
`vkmSetupNetwork` entry a runtime uses, rebuilt on every module load and reload.
A project with no such entry gets an empty schema and neither surface says
anything.

There is deliberately no button that launches a server and some clients. The
editor plays the scene in memory and `vkm_server` boots `project.entryScene`
from disk, so the button would silently test the last save; nothing in the
editor builds the gameplay module, so it would run yesterday's code; and there
is no process-spawn seam anywhere in the engine to build it on.
`vkm run --players 2` does the job one layer down, where the thing being
launched is unambiguous: what is on disk, with the module built first.

#### The handshake

`Hello` carries a fingerprint of the schema - the ordered list of replicated
type names and their policies, folded with the quantiser's steps and widths,
which are what a field's bits mean and live in source rather than in the list -
the project's action count, a fingerprint of
which action holds which command slot (`InputMap::actionFingerprint`), and a
fingerprint of the entry scene file (`setWorld`). The last two exist for the
same reason as the first: a command carries actions by slot and slots follow the
order actions were defined, so two ends with the same actions in another order
agree on the count and read every command as other input; and a slot is an
entity's name, so two ends with different worlds agree on every name and mean
different things by all of them. A mismatch is refused with `NetRefusal::Mismatch` and both ends print what
they hold, so a developer diffs two lines rather than debugging a world that
decodes into plausible nonsense. The schema fingerprint is order-sensitive: the
index is the wire identity, so the same names in a different order agree on
every name and on nothing that matters. A world fingerprint of zero - a world
generated rather than loaded - is compared against nothing, and so is an action
fingerprint of zero, which a server whose behaviors have not yet defined any
replaces with its first player's.

It carries the tick rate too, the project's `tickRate`, and a different one is
refused with `NetRefusal::TickRate`, which says so to the player. A command is
one tick, so two ends at different rates give every command a different length
of time, and the pacing bends a clock by five per cent, not by a factor of two.
The rate lives in `project.json` rather than in anything the other fingerprints
cover, so without this a client and server built from the same source and
loading the same scene could disagree about it and nothing would say.

A full server and a game that declined are refused the same way, with a reason a
player can be shown. Silence would be indistinguishable from a firewall.

So is another build. A message's first twelve bytes - the tag, the protocol
version, the kind and the token - are the same in every version, as are the
kinds `Hello` and `Refuse` and a refusal's reason. A server answers a `Hello`
at another version with `Mismatch`, and a joining client takes a `Refuse` at
another version as one; anything else at another version is dropped unread.

The client says hello from the frame loop rather than from `connect()`, because
a project defines its actions from a behavior's `onStart`, which runs inside the
first tick. It repeats at the command rate until answered, which is what makes a join
survive a lost first packet with no retry timer of its own.

**Every message carries a 64-bit token, and the token is what a seat is.** A
source address is trivially forged, so on its own it would be the whole of a
peer's identity: one forged `Goodbye` ends a match, one forged `Command` steers
a character.

The server answers a `Hello` that passes those checks without the right token
with a `Challenge` holding one, and keeps nothing - no seat, no player number,
no `SpawnPlayer`, no record that the address asked. Only a `Hello` that echoes
it seats the address and gets the `Welcome`: the token was said only to the
address that asked, so echoing it proves that address can hear. A sequence
number cannot do that job, because a sender writes the acknowledgement field
itself. Without the proof, a forged Hello from every address a sender likes
would fill every seat and spawn a player for each.

**The token is a keyed hash, not a record** (`NetJoinCookie`): SipHash-2-4 of
the address and the current ten-second window, under a key the server draws
from the operating system when it starts. It is taken back in its own window
and the next, and an older one is answered with a fresh `Challenge`. Two things
follow. A token cannot be predicted - not from the clock, and not from tokens
already seen - so it cannot be echoed by a sender that never heard it. And
because a join that has proven nothing costs the server nothing to remember, a
flood of forged Hellos costs it a hash and a reply no larger than each, and
crowds out no real join: there is no table of pending joins for a flood to
fill.

A client still saying Hello as a window turns can be told two tokens, seated by
the first and then hold the second, and would drop every `Welcome` as a
forgery. It heard both, so either proves it: a seat whose player has not yet
sent a `Command` takes the token its address now echoes.

**The token is checked before the connection sees the datagram.** Accepting a
packet moves the connection's window and confirms whatever its acknowledgement
field claims, so one forged datagram numbered far ahead would have every real
packet after it refused as stale, and forged acknowledgements would confirm
snapshots the player never received. Both directions check: the server reads a
player's token, and a client drops what does not carry its own. What the client
cannot check is the `Challenge` itself - it is how the token arrives - or a
`Refuse` of a first `Hello`, sent before there is a token; a forged one of those
can end a join still in progress, and nothing more.

Snapshots wait for the first `Command`, which says the `Welcome` arrived: until
a client knows which entity is its own it would draw that one late.

#### Seats

Everything about a player - connection, baseline, command queue - is allocated
and destroyed with them. A seat outlives the player in it, and per-seat state
that is reused hands the next player deltas against a world they never held.

Player numbers count up from one and are taken only by a join the game
accepted. They are not handed out again while their player is seated; after
65535 joins the count wraps, skipping `NO_PLAYER` and any number still held.

### Key files

Four folders, by what the code is about rather than by what it is named. The
session sits above them because it is the one thing a project touches.

- `src/engine/net/net_session.{h,cpp}` - `NetSession`, the API, and `NetCore`, what its halves share
- `src/engine/net/net_server.{h,cpp}` - `NetServer`, the authority's half
- `src/engine/net/net_client.{h,cpp}` - `NetClient`, a guest's half

**`net/wire/`** - how a value becomes bytes.

- `bit_stream.h` - `BitWriter` / `BitReader`
- `quantize.{h,cpp}` - position, rotation and velocity on the wire
- `protocol.{h,cpp}` - `NetMessage`, `NetRole`, `NetRefusal`, `PlayerId`, and every payload's header
- `schema.{h,cpp}` - `NetSchema` / `NetType`, what replicates and in what order
- `codecs.{h,cpp}` - the engine's own codecs (`Transform`, `Rigidbody`,
  `CharacterController`, `Ragdoll`, `NetSpawn`) and `isSayable`

**`net/transport/`** - how a datagram gets there, and what happens when it does not.

- `connection.{h,cpp}` - sequencing, acknowledgement, round-trip
- `join_cookie.{h,cpp}` - `NetJoinCookie`, the token a join proves its address with, and `sipHash24`

**`net/replication/`** - what travels.

- `snapshot.{h,cpp}` - `writeSnapshot` / `readSnapshot` / `NetBaseline` / `NetBudget`
- `silence.{h,cpp}` - `netSilence` and `NetSilenceMap`: which entities are described at all

**`net/prediction/`** - the client's own copy of time.

- `command.{h,cpp}` - command packing and `NetCommandBuffer`
- `interpolation.{h,cpp}` - drawing the entities this end is only told about
- `pacing.{h,cpp}` - `NetPacing`, how fast a client ticks to keep the server's queue shallow
- `rewind.{h,cpp}` - where every player has been, for judging a shot
- `sample_track.h` - the ring both smoothing and rewind keep their samples in

**`src/engine/platform/net/`** - the OS, kept where every other platform seam is:
`udp_socket.{h,cpp}`, `net_address.{h,cpp}`, `winsock_init.{h,cpp}`.

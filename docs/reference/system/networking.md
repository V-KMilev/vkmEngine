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

## Key files

Four folders, by what the code is about rather than by what it is named. The
session sits above them because it is the one thing a project touches.

- `src/engine/net/net_session.{h,cpp}` - `NetSession`, the whole session in one object

**`net/wire/`** - how a value becomes bytes.

- `bit_stream.h` - `BitWriter` / `BitReader`
- `quantize.{h,cpp}` - position, rotation and velocity on the wire
- `protocol.h` - `NetMessage`, `NetRole`, `NetRefusal`, `PlayerId`
- `schema.{h,cpp}` - `NetSchema` / `NetType`, what replicates and in what order
- `codecs.{h,cpp}` - the engine's own codecs (`Transform`, `Rigidbody`,
  `CharacterController`, `Ragdoll`)

**`net/transport/`** - how a datagram gets there, and what happens when it does not.

- `connection.{h,cpp}` - sequencing, acknowledgement, round-trip
- `reliable.{h,cpp}` - the channel a spawn cannot be dropped from

**`net/replication/`** - what travels.

- `snapshot.{h,cpp}` - `writeSnapshot` / `readSnapshot` / `NetBaseline` / `NetBudget`
- `spawn.{h,cpp}` - the spawn and despawn messages
- `silence.{h,cpp}` - `netSilence`: which entities are described at all

**`net/prediction/`** - the client's own copy of time.

- `command.{h,cpp}` - command packing and `NetCommandBuffer`
- `interpolation.{h,cpp}` - drawing the entities this end is only told about
- `rewind.{h,cpp}` - where every player has been, for judging a shot
- `sample_track.h` - the ring both smoothing and rewind keep their samples in

**`src/engine/platform/net/`** - the OS, kept where every other platform seam is:
`udp_socket.{h,cpp}`, `net_address.{h,cpp}`, `winsock_init.{h,cpp}`.

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

**There is one way to host, and the runtime is not it.** That is partly the rule
that made `vkm_server` an executable - what a process is, is which executable was
run - but the sharper reason is what a listen server would do to the engine's own
testing. A host that also played would be driving a character that is the
authority: never predicted, never interpolated, never corrected. The
configuration a developer runs most would then exercise the client path for only
half its players, and the client path is the half most likely to be wrong.

Serving and joining costs a second process and buys a real client. `--host` is
refused by name rather than as an unknown argument, and says what to run
instead.

**A host referees and does not play.** It owns no entity, so `isMine()` answers
no everywhere with no special case for it, and every player in the game is a
client - predicted, interpolated and corrected like every other.

A dedicated server is `vkm_server`, a host of its own rather than a flag -
because what a process is, is which executable was run. It opens no window,
installs no render backend, and runs with `DISPLAY` and `WAYLAND_DISPLAY` unset.
It still needs the GL and X11 *shared libraries* present, because `vkm_core`
links glfw and GLEW; a binary free even of those is a refactor of a class every
host uses and is deliberately not done.

The system stack is identical to the runtime's, and that is the point: an
authority that simulated differently from the clients it corrects would not be
an authority. What is skipped is the window, the icon and the GL debug switch,
none of which a simulation consults - and the frame cap is *not* skipped with
them, because with no window there is no vsync and nothing to block on, so an
uncapped loop spins a core for frames nobody draws.

Measured on physics_lab: 9.5 MB against the runtime's 20 MB, 18 MB resident, and
40% of one core at 128 Hz rather than the 100% a busy loop would take.

## What a project writes

Two things: what a player is, and three questions inside the behaviors that
move things.

### What a player is

An optional module entry, beside `vkmRegisterBehaviors` and `vkmBuildScene`:

```cpp
extern "C" void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    session.onSpawn(
        [](Scene& scene, ResourceManager& resources, PlayerId id) -> EntityId {
            return /* the entity this player drives, or a null id to refuse */;
        },
        [](Scene& scene, ResourceManager&, PlayerId id, EntityId entity) {
            /* the player left */
        });
}
```

A module without the entry is normal and silent - a single-player project needs
none, and a project that has one still runs offline, because a session nobody
joined behaves exactly as no session.

Returning a null id refuses the connection, which is how a game says "the match
has started".

### The three questions

On `Behavior`, so a networked behavior reads like a single-player one:

| Ask | When | Offline |
|-----|------|---------|
| `isSimulated()` | before writing a transform, a velocity, anything the world can see | always true |
| `isMine()` | before touching the camera, the pointer, the HUD - anything about *this* player | always true |
| `command()` | instead of reading the device inside a fixed update | the local command |
| `isReplaying()` | before anything that *presents* - a clip, a sound, an effect | always false |

`isSimulated()` and `isMine()` are different questions and the difference matters:
a server simulates every player and owns none of them.

`command()` is the same call in all three roles. Offline and on the owning
client it is the local player's input; on a server it is what that entity's
player sent, run in the order they made it. An entity no player drives reads as
nothing held.

physics_lab's walker needed four lines: two guards and one substitution.

### What replicates

`Transform`, `Rigidbody`, `CharacterController` and `Ragdoll` are registered by
the engine (`registerEngineNetTypes`). The last two carry one field each and are
worth saying why:

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

`NetPolicy::OwnerOnly` sends a component to the connection that owns the entity
and to nobody else, for state that exists to make one player's prediction
converge and means nothing to anyone else.

### What does not replicate

Three rules, all the same rule: **replicate causes, not effects.** State that
every end recomputes from something already on the wire is not merely wasted
bandwidth - it arrives as a second writer for a value that already had one, and
which of the two a frame shows then depends on how many fixed steps that frame
happened to run.

- **A ragdoll bone whose ragdoll is inactive** (`isPosedByAnimation`). Inactive,
  the bodies are kinematic and placed from the animated pose every tick, and the
  clip is chosen from the body's velocity, which does replicate - so both ends
  derive the same skeleton from the same input. Measured before this rule
  existed: four characters of twenty bones each were **96% of every snapshot**,
  and the props and the other players competed for the rest. Active, it inverts:
  the solver drives the bones and the pose follows them, so they are the answer
  rather than a copy of it and every one travels. That is what `Ragdoll::active`
  is on the wire for - a client that missed the switch would pose twenty bones
  from a walk cycle while the server had a body falling.

  The clip phases need not match. Each end advances its own clip time, so two
  ends hold the same walk at different points in it; a kinematic bone decides
  nothing while the ragdoll is off, and the first snapshot after it comes on
  settles the difference.

- **A static body** (`isImmovable`). It is the scene file, both ends read the
  same one, and what the wire sends back is a quantised copy replacing a value
  that was already exact.

- **An entity inside a prefab instance** (`isInsidePrefabInstance`). Not because
  it is derived, but because it has no name: each end allocates its children's
  slots locally. See *Identity is the scene slot*.

## How it works

### Identity is the scene slot

An entity is named on the wire by its slot in the `Scene`. Both ends loaded the
same scene file, which records the slot, so they agree with no handshake, no
mapping table, and nothing to keep in step. `SceneSerializer` writes
`entity["id"] = id.slot()` and restores through `createEntityAt`.

An entity built after the scene loads has no slot the other end knows, so
content spawning names one: `NetSession::spawn` builds a prefab and tells every
client which prefab and which slot, reliably (see *Spawning*). The limit that
remains is narrower - **an entity inside a prefab instance has no name on the
wire at all.** Both ends build the subtree from the same file and each allocates
its children's slots locally, so the two disagree about what slot 243 is. The
root replicates and the hierarchy carries the rest, which is correct for
anything posed the same way on both ends and wrong for a ragdoll, whose bones
fall on the server and are never heard about. The editor says so on the entity:
see *In the editor*, below.

### Snapshots: presence-delta against what was confirmed

A snapshot carries a component only when its encoded bits differ from what that
connection has **confirmed receiving**. The delta is on presence, never on
values.

That distinction is the design. A value delta chains, so one lost packet
poisons everything after it until a full resend; an absolute entry held back
for three snapshots is unconditionally correct the moment it lands. It is also
why joining and playing are one code path: a connection that has confirmed
nothing differs from the world in every component, so its first snapshot is the
whole world because of the rule rather than as an exception to it.

**Confirmed, not sent.** What a snapshot claimed is held aside until the
acknowledgement comes back. Folding it in at send time describes a client that
received it - and a client that did not is never told again, so a body that
moves through a burst of loss and then stops is frozen on that client until the
match ends, with nothing anywhere reporting a problem.

Comparison is on the quantised bytes, not on floats, so a settled world goes
quiet. A body at rest costs three bits.

The one exception: **the connection's own entity is written every snapshot**,
moved or not. The receiver does not hold what it was last told about that one -
it holds its own prediction. It is also written before anything is prioritised
and is never deferred, because it is what reconciliation compares against.

### What a value costs, and what it owes

A position is carried to the millimetre, a rotation as its three smallest
components at eleven bits each, a velocity to the centimetre a second. The
figures are in `net/quantize.h`; two properties of them are load-bearing and
neither is obvious.

**Zero is exact, for every signed field.** A signed value uses one fewer than
half its codes either side of a middle code that is exactly zero. Spreading the
codes evenly across the range instead - the obvious way, and what this replaced -
leaves the midpoint *between* two of them, so zero is the one value that cannot
be said. That matters because zero is not an ordinary value: almost everything
in a scene is unrotated and almost everything in it is still. Unrotated came
back as a quarter of a degree, and a body at rest reported drift, which quietly
cost the settled world its silence.

**A rotation error is an angle, so what it costs grows with the thing turned.**
A quarter of a degree is invisible on a crate and lifts one end of a sixty-metre
floor by ten centimetres - and a character standing on the low end finds nothing
beneath it and falls through the world. That is why rotations are eleven bits a
component rather than nine, and it is the other half of why a static body is not
sent at all: the scene file is exact where the wire is not.

### The budget

`NetBudget::bytes` is a ceiling, not a target. What does not fit is deferred,
and a deferred entity's priority rises while it waits, so it goes first next
time rather than losing every round to whatever moved most recently.

Letting a busy frame produce whatever it produces is what turns a collapsing
tower into a datagram the socket refuses whole - the client then sees nothing at
exactly the moment there is most to see.

Measured: 117 bodies all awake and moving arrive in four snapshots, worst packet
1002 bytes. A moving body is 24 bytes, so fifty fit one packet.

### Commands: redundant, in order, once

A command packet carries the **oldest twelve** unacknowledged commands, not just
this tick's - the oldest, because after a stutter those are the ones the server
wants next, and sending the newest would strand them. An axis recovers from loss on its own - the next packet says where
the stick is now. An edge does not: `pressed` is true for exactly one command,
so one lost datagram loses that jump permanently and nothing reports an error.

Twelve ticks is a 94 ms window at 128 Hz, and every packet lost inside it is
survivable - six consecutive at the default rates, since two new commands are
made per packet sent. The window is the invariant; raising the send rate buys
more packets inside the same window, not more protection.

Retransmission is the other answer and it is the wrong one: by the time a
retransmit arrived, the tick it belonged to would be long past.

The server runs one command per tick, in the order the player made them, and
never twice. They are **not** addressed by tick number - the two ends count
ticks on separate clocks, so the client's tick 400 means nothing against the
server's. Running them in order gives the property that matters with no clock to
synchronise. The client's own tick number is carried and handed back in the
snapshot, so the client knows which of its predictions has been judged.

A backlog is drained faster than it fills, because every command behind it
waits. Skipped edges are folded into the command that runs - an axis can be
skipped, an edge cannot.

An empty queue is the other direction, and it is not the rare case: two clocks
free-running at the same nominal rate cross often. The tick runs anyway, on the
last command's axes with its edges cleared - a held key is still held, and a
press that already fired would fire again on every tick of the gap.

That repeat **claims the client tick it stood in for.** A tick of movement
happens either way, and a snapshot naming the tick before it would describe a
pose the client never predicted - an error the client did not make, replayed
away on every snapshot for as long as it keeps walking. The real command for
that moment is discarded when it arrives, with its edges folded into the next
that runs, or one moment of input would move the character twice.

A repeat may only run as far dry as the buffer is allowed to run deep. Past that
the other end is not merely late, it is slower than this one, and a server
running ahead of everything that client will ever send would discard all of it.

### Prediction and correction

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
next edit between them can break silently, and the two really did drift apart
once, across a frame boundary.

One offset per body this end predicts, not one for the character. A client
predicts whatever it is leaning on as well, and those bodies take the same
correction from the same replay - a crate that jumps under a character that
eases is the two of them visibly disagreeing about one push. A lapsing lease is
smoothed by the same machinery for the same reason: the body did not move, this
end simply stopped being the one deciding where it was, and handing it back to
an interpolation four ticks in the past would show as a jump the moment a
player stops pushing.

Measured on one rule and one stream of input: once the two ends are in step the
prediction is out by 0.0 mm and nothing is ever re-run. Against a wall the
client could not see, a 30 cm disagreement is re-run across 20 ticks and lands
on the server's answer exactly - 0.00000 - rather than near it.

### Replay

A replayed tick must be a function of the world and the command: same state,
same input, same result, and doing it twice is the same as doing it once.
`System::isReplayed()` states which systems are, the way `hasFixedUpdate()`
states which take part in the fixed step.

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
which on a client is the one it drives. And a body this end does not decide is
made unpairable for the pass, so the broadphase's existing static-against-static
skip drops every pair that does not involve the character being corrected -
which is what stops a replay costing a full tick over every body in the world.

What a replay does **not** rewind is unreplicated per-tick state: the character
controller's step latch, animation, a behavior's own members. Those carry
forward from where they now are. A replay that begins mid-climb re-runs its
ticks with the latch as it stands, not as it stood.

### Leasing

A client also simulates the bodies its own character is touching, and the
bodies those are touching, out to the edge of the pile. Without it a push is
predicted as walking into a wall - the crate cannot move, so the character
stops - and is corrected the moment the server says otherwise. That is the
softness felt on every contact.

The island is a breadth-first walk from the owned body over the contacts and
joints of the tick that just ran, computed in `PhysicsSystem` because that is
the only place the graph exists, and reported to the session. Three edges it
refuses, each for a reason:

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

Nothing in physics needed a new case for this. `simulates()` already gated both
the gather and the writeback, so a leased body simply gets its real mass into
the solver and its solved pose back out - which is what the one-predicate design
was for.

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

### Interpolation

Everything the client does *not* own is drawn about four ticks behind the
newest news, between two positions the server actually gave. A server sends
thirty-two times a second and a client draws a hundred and forty-four times a
second,
so most frames have no news; drawing the newest thing heard makes everything
move in steps, and no amount of bandwidth fixes it.

The clock that decides what is drawn is corrected by running slightly fast or
slow - up to ten per cent - never by being moved, and never runs past the newest
sample. A stall shows as the world pausing, which is what it is.

What the server said is put back before each snapshot is read. Presence is
measured against what the receiver was last told, so leaving the smoothed value
in the component would make "unchanged" mean "unchanged from the smoothed
value", and a still body would drift.

### Lag compensation

A player aims at what they can see, which is a moment already past: their
screen shows the world as it was a round trip plus an interpolation delay ago.
Judged against the present, a player who aimed correctly misses - and the better
their aim and the worse their connection, the more often, which reads as the
game being broken rather than as latency.

So the server keeps the last 64 ticks of every player's pose and puts them back
for exactly the length of one query:

```cpp
{
    NetRewindScope rewound(scene, net, entity());
    RayHit hit;
    if (raycast(scene, origin, Math::computeForward(command().view), range, hit, filter)) { ... }
}
```

Offline and on a client the scope does nothing, so that is a plain query in a
single-player game.

**The moment is not estimated from ping.** A client already knows which server
tick it drew - its interpolation clock runs in the server's own tick numbering -
so it says so in its command packets, five bytes a packet, whole and fraction.
What arrives already accounts for that client's latency, its interpolation delay
and its frame time, with no clock to synchronise and nothing to double-count.
Measured: a client reports it is looking 5.3 ticks behind the server's present.

It is a claim from a client, so it is clamped to what is remembered. That bounds
how far a shot can reach back; it does not bound how often, which is a judgment
about a game rather than about a wire.

Two limits worth knowing. Only **players** are rewound - a moving platform, a
swinging door or a thrown crate is judged against the present, because players
are what players shoot at and remembering every body means a ring per crate. And
the history is 64 ticks, which is half a second at 128 Hz and a second at the
engine default - so how far back a shot may reach is a function of a project's
tick rate.

### Why UDP

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
arrived, the tick it belonged to would be long gone, which is why the same
command is sent twenty-four times up front instead.

What genuinely cannot be dropped - a spawn, a despawn - gets a reliable channel
of its own riding the same datagrams, rather than making the whole wire reliable
to serve the few messages that need it.

The property this rests on is tested rather than assumed: with 156 of 400
snapshots lost, a dozen delivered twice and others reordered, the two worlds
still agree to within a quantisation step, and the settled world still goes
quiet afterwards.

The cost is honest and worth stating: sequencing, the acknowledgement bitfield
and the reliable channel are all machinery TCP would have given free. They are
the price of not paying for ordering.

### The connection

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

### Rates

Snapshots and commands both go out 64 times a second, driven by wall clock
rather than by the render loop. A machine drawing at 144 would otherwise send
144 snapshots a second, and one drawing at 20 would starve every client it has.
Neither number has anything to do with how often the world needs describing.

The snapshot rate is what decides how late another player looks, because the
interpolation delay is counted in snapshots: two of them is 62 ms at 32 a second
and 31 ms at 64. It was 32, and the difference was visible - measured on
physics_lab with two players, a client drew 7.6 ticks behind at 32 and 2.9 at
64, which at a 128 Hz tick is 59 ms against 23 ms. What it costs is bandwidth
and nothing else: a snapshot does not get smaller when they are more frequent,
because the entities that move move every tick either way, so the measured
per-peer cost went from about 9 KB/s to 17 KB/s. That is the trade, and 32 is
the number to go back to if a game would rather have the bytes.

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

### The frame

`NetSession` is owned by `Engine` by value, like the clock and the event bus,
and reached through `FrameContext`. It is not a `System`: it brackets the frame
rather than taking part in it.

```
advance(dt)              wall clock, so a paused editor still notices a peer leaving
receive(scene, res)      apply what arrived, reconcile, seat whoever joined
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

### In the editor

The editor never hosts and never joins - its session is Offline for the life of
the process - so there is no connection to show and nothing that varies at run
time to inspect. Two things about a *project* do vary, and both were invisible:

**File > Project > Settings...** edits what `project.json` records, including
how many players the game is for and the port it is served on. Those are read on
every run and had no surface at all before this: an author edited JSON by hand.
The Multiplayer card also lists what the game puts on the wire and its
fingerprint - exactly the pair a refused join prints - so a mismatch can be
compared against a server's log line rather than debugged as a world that
decodes into nonsense.

**The inspector's identity header** says what the other end will see of the
selected entity: its wire slot and which of its components replicate, or a
warning when it is inside a prefab instance and so has no name on the wire. It
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
is no process-spawn seam anywhere in the engine to build it on. `vkm play
<project> -n 2` does the job one layer down, where the thing being launched is
unambiguous.

### The handshake

`Hello` carries a fingerprint of the schema - the ordered list of replicated
type names and their policies - and the project's action count. A mismatch is
refused with `NetRefusal::Mismatch` and both ends print their list, so a
developer diffs two lines rather than debugging a world that decodes into
plausible nonsense. The fingerprint is order-sensitive: the index is the wire
identity, so the same names in a different order agree on every name and on
nothing that matters.

A full server and a game that declined are refused the same way, with a reason a
player can be shown. Silence would be indistinguishable from a firewall.

The client says hello from the frame loop rather than from `connect()`, because
a project defines its actions from a behavior's `onStart`, which runs inside the
first tick. It repeats every frame until answered, which is what makes a join
survive a lost first packet with no retry timer of its own.

The `Welcome` carries a 64-bit token, and that token is what a seat is. Every
`Command` and `Goodbye` echoes it, and a packet that does not is dropped -
without it a source address would be the whole of a peer's identity, so one
forged `Goodbye` ends a match and one forged `Command` steers a character.

Echoing it is also the proof the address can receive, which is what gates the
snapshot stream: nothing but the `Welcome` is sent to a seat until a packet
comes back carrying the token. A sequence number cannot do that job, because a
sender writes the acknowledgement field itself - it can claim to have received a
packet it never got, and a seat taken by a spoofed address is a kilobyte a tick
aimed at whoever it named. The token was said once, to the address that asked,
so only that address can repeat it.

### Seats

Everything about a player - connection, baseline, command queue - is allocated
and destroyed with them, and player numbers are never reused. A seat outlives
the player in it, and per-seat state that is reused hands the next player deltas
against a world they never held.

## What this deliberately does not do

Each of these is a decision with a reason, not an oversight.

- **A spawned prefab's children do not replicate.** Each end allocates their
  slots locally, so only the instance root is named on the wire. An animator
  poses them correctly on both ends; a ragdoll does not, so a ragdoll belongs on
  a character the scene author placed. `PrefabEntity::uid` is the identity that
  would lift this - it names an entity inside a prefab and survives the file
  being re-saved - and using it means the snapshot carrying a second addressing
  form, which nothing needs yet.
- **A leased body is not reconciled tick by tick** the way the owned one is. Its
  prediction is held while the lease lasts and slides back to the server's word
  when the lease lapses.
- **Only players are lag-compensated.** A moving platform, a swinging door or a
  thrown crate is judged against the present. Players are what players shoot at,
  and remembering every body means a ring per crate.
- **Nothing a client says has to arrive.** The reliable channel carries the
  acknowledgement in that direction and no messages, because nothing yet needs
  delivering upstream - input is already repeated across a 94 ms window.
- **A late joiner is told where the spawned things are, but not what they are.**
  Spawn rides the reliable channel, which carries a message until it is
  confirmed and then forgets it - so a player who joins after a spawn gets the
  entity's slot in the snapshot and builds a bare body with no mesh and no
  collider. Replaying every past spawn into a new peer's queue is the obvious
  answer and the wrong one: the queue is bounded, and a game with more live
  spawned entities than it holds would drop every joiner instead of some of
  their scenery. A game that spawns should author what a joiner must see, or
  wait for a join protocol that carries the world's content rather than its
  state.

- **A connection lost mid-game is a client that keeps playing.** The session
  goes Offline, which answers yes to `simulates` for everything, so the client
  becomes authoritative over a world nobody else can see. `isPlaying()` is what
  a project asks to tell that apart; a game that never asks will not notice.

- **No fragmentation.** A datagram over 1200 bytes is refused, not split. One
  lost fragment loses all of it, and the budget exists so it never comes up.
- **No GL-free server binary.** `vkm_server` links no render backend, but
  `vkm_core` links glfw and GLEW, so the shared libraries must be present even
  though no display is. Freeing it of those means routing 48 call sites through
  an internal seam in a class every host uses; simplicity outranks features.

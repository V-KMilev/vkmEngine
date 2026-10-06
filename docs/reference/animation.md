# Animation

Two systems, both in the Simulation stage, and both on the **tick** rather than
the frame: a pose is simulation - physics and the hitboxes read the Transforms
it writes - so advancing one per frame would make the same tick answer
differently depending on how long the last frame took. Pause, time-scale and
single-step still reach them, through the accumulator that decides whether a
tick happens at all rather than through a delta they scale themselves. They do
not overlap:

- **`AnimationSystem`** plays authored keyframe tracks onto an entity's own
  `Transform`. One entity, one animated object.
- **`SkeletalAnimationSystem`** plays a baked clip onto a *rig* and publishes the
  resulting pose on `FrameContext::poses`. It writes no `Transform` at all,
  because a rig's bones are indices in an array rather than entities - which is
  also why the two can never contend for the same component.

# Keyframe animation

## How an Animation advances

The system makes a single pass over **every** entity with an `Animation` component
(it does not filter by visibility). For each playing animation it:

1. Rewinds a one-shot that has nothing left to play in the direction it moves
   (at 0 with a negative `speed`, or at its end with a positive one) to the
   other end, which is how a clip played backwards starts: every way of
   starting one leaves the head at 0. This is `rewindSpentHead`, and an
   `Animator`'s playing head goes through it too.
2. Advances `time` by the tick's fixed step scaled by `speed`.
3. Handles the end of the timeline - wraps when `looping`, otherwise clamps and
   stops, at either end, since `speed` may be negative. This is `advanceHead`
   (`pose_evaluator.h`), the same rule every head of an `Animator` moves by.
4. Samples each track and writes `Transform.position` / `.rotation` / `.scale`.

`HierarchySystem` runs later in the same frame and rebuilds every
`WorldTransform`, so an animated entity inside a hierarchy needs nothing
recorded here.

Because it applies to all animated entities (not just visible ones), off-screen
animation stays in sync; the cost is bounded by the number of *animated* entities,
not the scene size.

## Animation component

```cpp
struct Animation {
    AnimationTrack<glm::vec3> positionTrack;
    AnimationTrack<glm::quat> rotationTrack;
    AnimationTrack<glm::vec3> scaleTrack;

    float length  = 0.0f;   // explicit minimum length (0 = auto from last keyframe)
    float speed   = 1.0f;   // playback multiplier
    bool  looping = true;

    bool  playOnStart = true;   // authored: start on the first simulated frame

    // Transient - runtime only, never serialized.
    float time        = 0.0f;   // the playback head
    bool  playing     = false;  // advancing right now
    bool  started     = false;  // playOnStart already honoured

    static float computeDuration(const Animation&);  // = max(each track's last keyframe, length)
};
```

**`playOnStart` is the authored flag and `playing` is the session's**, split the
way `AudioSource` splits the same pair. Only `playOnStart` is serialized: a
scene that came back from disk halfway through a clip would resume a motion
nobody saw begin, and the editor's Play / Pause buttons are a preview transport
rather than a decision about what a shipped scene does. The first update with a
non-zero simulation delta turns `playOnStart` into `playing` once and latches
`started`, so a one-shot that ends does not restart every frame - and so nothing
moves while a scene is merely open, since the editor is paused until Play.

The effective duration is derived on read, never stored: `computeDuration()` is
three O(1) reads and a `max`, so editing keyframes, tracks, or `length` cannot
leave anything stale. `length` lets you hold an animation open past its last
keyframe (e.g. a pause at the end of a loop); with `length == 0` the duration is
just the latest keyframe across the three tracks. Each track is independent - an
entity can animate position only, or any combination.

## AnimationTrack<T>

A time-sorted keyframe sequence with one easing function:

```cpp
AnimationTrack<glm::vec3> track;
track.setEasing(Easing::EaseInOutSine);
track.addKeyframe(0.0f, {0, 0, 0});
track.addKeyframe(2.0f, {0, 5, 0});

glm::vec3 value = track.getValue(1.0f);  // eased, interpolated
```

`getValue(time)`:

1. Clamp `time` to the `[first, last]` keyframe range.
2. Binary-search the enclosing interval.
3. Compute the normalized `t` within it and apply the easing function.
4. Interpolate: `glm::slerp` for `glm::quat`, `glm::mix` otherwise - selected with
   `if constexpr (is_same_v<T, glm::quat>)`.

`addKeyframe()` inserts and keeps the sequence sorted by time.

## Easing functions

**A curve's identity is its `Easing` enumerator**, never its function pointer.
`core/math/easing.h` lists every curve once - enumerator, stable name, glm
routine - and the enum, its `VKM_ENUM_NAMES` names and the function table are
generated from that list, so they cannot disagree. `AnimationTrack::setEasing`
takes the enumerator, `easingFunction` resolves it to a pointer at the point of
call, and the scene file carries the name, as it does for every enum.

The pointer cannot travel. The function table is an `inline` table of lambda
addresses in a header and `vkm_core` is a shared library whose data carries no
import annotation, so on Windows the editor's copy and the engine's hold
different addresses for the same row - a curve picked in the editor would be
unrecognisable to the serializer.

31 rows: `linear`, plus ten In/Out/InOut families.

| Family | In | Out | InOut |
|--------|----|-----|-------|
| Linear | `linear` | - | - |
| Quadratic | `easeInQuad` | `easeOutQuad` | `easeInOutQuad` |
| Cubic | `easeInCubic` | `easeOutCubic` | `easeInOutCubic` |
| Quartic | `easeInQuart` | `easeOutQuart` | `easeInOutQuart` |
| Quintic | `easeInQuint` | `easeOutQuint` | `easeInOutQuint` |
| Sine | `easeInSine` | `easeOutSine` | `easeInOutSine` |
| Exponential | `easeInExpo` | `easeOutExpo` | `easeInOutExpo` |
| Circular | `easeInCirc` | `easeOutCirc` | `easeInOutCirc` |
| Back | `easeInBack` | `easeOutBack` | `easeInOutBack` |
| Elastic | `easeInElastic` | `easeOutElastic` | `easeInOutElastic` |
| Bounce | `easeInBounce` | `easeOutBounce` | `easeInOutBounce` |

---

# Skeletal animation

A rig is a `SkeletonAsset` and a clip is an `AnimationClipAsset` (both described
in [Resources](resources.md)). What binds them to a character is one
`Animator`, and what comes out is a pose published for the frame.

## Animator

```cpp
struct Animator {
    SkeletonHandle      skeleton;   // the rig posed
    AnimationClipHandle clip;       // empty holds the bind pose

    float speed   = 1.0f;
    bool  looping = true;       // the clip in `clip`, not the animator
    bool  playOnStart = true;   // authored: start on the first simulated frame

    // Transient - runtime only, never serialized.
    float time    = 0.0f;       // the playback head
    bool  playing = false;      // advancing right now
    bool  started = false;      // playOnStart already honoured
    AnimationClipHandle fadeFrom;
    float               fadeTime      = 0.0f;
    float               fadeRemaining = 0.0f;
    float               fadeDuration  = 0.0f;
    bool                fadeLooping   = true;   // the outgoing clip's own answer
    std::vector<BoneAdjust> adjust;             // what a behavior adds per bone; see below

    static void crossFadeTo(Animator&, AnimationClipHandle, float seconds, bool looping);
};
```

**One Animator per character, not one per mesh.** Import spawns a sub-entity per
mesh in the file, so a rigged character arrives as body plus clothes plus hair; a pose
held on the mesh would mean three clocks drifting apart, or two of the three
silently frozen in bind pose.

The repo ships no multi-mesh rig, so what it carries for this is the recipe rather than
the artifact: `tools/make_multimesh_rig.py <out.gltf>` writes a file that is
nothing but the case - three skinned meshes over one skin, one skeleton and one
clip, each weighting a different subset of the joints. Import it and the
hierarchy shows one rig entity carrying one `Animator`, the three meshes
parented under it, and all three resolving the same `PoseSlice`. Every joint is
a pure translation, so the bind matrices can be checked by eye. `BrainStem.glb`, if a project has it, is
the same shape at scale: 59 skinned meshes over one 18-bone rig.

**There is no `SkinnedMesh` component.** A mesh is skinned exactly when its
`MeshAsset::skin` is non-empty - the asset already knows - and the rig driving it
is the nearest `Animator` at or above it in the `Hierarchy`, which is the
structure import produces anyway. That relationship needs no `EntityId` in any
serialized row, so prefabs, undo and scene load never have to remap it.

The authored fields are persisted, and the blend state deliberately is not. A
crossfade is a second clip and a countdown; freezing that shape into a scene row
would outlive the blend system that wrote it, in a project with no migration
path. A scene saved mid-blend reloads as the clip it was blending *to*, already
there - which is where it was going, one fade early.

**`playOnStart` is the authored flag and `playing` is the session's**, exactly
as `Animation` and `AudioSource` split the same pair, and `advancePlayback`
turns one into the other once on the first frame with simulation time to spend.
The Animator card's transport writes `playing` to preview a clip - correctly
pushing no undo step and raising no dirty marker, because previewing is not an
edit. Were `playing` persisted, a Pause pressed in that card and a Ctrl+S for an
unrelated reason would leave the scene file holding `playing: false` for a
character that is never going to animate again, with nothing having warned and
nothing to undo.

**`time` is the session's too**, and for the same reason: the timeline and the
Animator card both scrub it without an undo step, so persisting it would mean a
head position nobody chose riding into the next save. A start phase a game
wants - two characters half a lap apart - is set by the behavior that spawns
them, which is where "they should not be in step" is actually decided.

## Crossfading

```cpp
Animator::crossFadeTo(scene.get<Animator>(character), runClip, 0.2f, /*looping*/ true);
```

One fade, two slots. The clip being left moves into `fadeFrom` and **keeps
playing** at its own head, so a run fading into a walk does not freeze one foot
while the other keeps moving.

Whether a clip loops belongs to that clip, so the incoming one's answer is an
argument here and the outgoing one's is parked on `fadeLooping`. It is taken
rather than assigned by the caller around the call because only this can capture
the outgoing clip's answer before the incoming one overwrites it - a death
fading out of a walk has to clamp at its own last frame for the half second the
blend lasts, not start again. The countdown runs in unscaled simulation seconds,
because "blend over 0.2 seconds" is a duration the caller can predict, while
`speed` is about how fast the clips themselves run.

| Call | Result |
|------|--------|
| The clip already playing | Left alone. The caller means "keep going", not "restart from zero" |
| The same clip, stopped | Played again from its start, like any other clip. A one-shot that ran out is not playing, and a second hit wants a second flinch |
| Any clip while the current one is stopped | Blended, out of the frame the stopped clip is holding, which then advances as it fades like any outgoing clip. Stopped is not empty: the pose on screen is that frame |
| No clip at all, or `seconds <= 0` | A cut. There is nothing to blend out of, or no time to do it in |
| Again, mid-fade | The clip already on its way out is dropped; the blend runs from the one that was being faded *to*. The screen was showing the blend of the two, so the pose jumps by the dropped clip's remaining weight - the price of two slots |

The countdown is gated on simulation time, **not** on `playing`. A one-shot clip
shorter than the blend into it sets `playing` false partway through, and a fade
that stopped with it would hold the character at a weight no field names and
nothing clears - visibly mostly the clip it already left, with no way out but
another `crossFadeTo`. The blend is about reaching the clip, not about that clip
still advancing; it finishes, the outgoing slot clears, and the pose settles on
the target clip's last frame.

The blend happens on the **local TRS**, inside the same loop that composes, before
the parent multiply:

```cpp
local.position = glm::mix (leaving.position, local.position, weight);
local.rotation = glm::slerp(leaving.rotation, local.rotation, weight);
local.scale    = glm::mix (leaving.scale,    local.scale,    weight);
```

Blending the composed matrices instead is the mistake that looks nearly right: it
pulls a joint toward the midpoint of two *world* positions, which shortens the
limb hanging off it. A bone blended from 0 to 90 degrees sits one unit from its
root at every weight when the blend is local, and 0.707 units out halfway through
when it is not.

`weight` is `1 - fadeRemaining / fadeDuration`, and the second sample and the
three interpolations above are skipped entirely at weight 1 - which is every
frame that is not mid-fade.

## Adjusting a bone from gameplay

A clip cannot know where the player is aiming, how far a crouch goes or which
way a hit landed. `Animator::adjust` is where a behavior says so: a list of
`BoneAdjust`, one per bone it wants moved, applied inside `composePose` after
the clip is sampled and the crossfade blended, and before the bone is composed -
so everything hanging off the bone follows: the hand and the weapon socketed to
it, the ragdoll bodies placed from the pose, the skin.

```cpp
struct BoneAdjust {
    int32_t   bone = -1;                          // index into the rig's bones; out of range adjusts nothing
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   // rig model space, about the bone's own origin
    glm::vec3 offset{0.0f};                       // rig model space, added to the bone's origin
    float     scale    = 1.0f;                    // multiplies the bone's scale; children inherit
    bool      absolute = false;                   // true: rotation is the orientation, not a turn added
};
```

Both parts are stated in **rig model space** - the frame the pose is published
in - and act about the **bone's own origin**: the rotation turns the bone and
its subtree about that point, the offset moves the point. `composePose` brings
each into the parent's frame itself - the rotation conjugated by the parent's
composed rotation, the offset through the inverse of its upper 3x3 - so a caller
never learns which way a rig's authoring tool pointed a bone's local X. A
behavior thinking in world axes turns the axis into model space once, with the
inverse of the rig entity's world rotation, and states the angle about that.
Two entries naming one bone compose in list order; there is no weight and no
blend. An `absolute` entry is the
orientation the bone ends up with in model space, whatever the clip and the
parent did - what an arm solved to hold a weapon needs, since the clip is
swinging that arm and the solution is not relative to where the swing happens
to be this frame. `scale` multiplies the bone's own scale and its children
inherit it; near zero it collapses the bone's skin to a point, which is how a
first-person view hides the head its camera stands inside without a second
mesh - near, not at, zero, because a socket or a hit box reads an orientation
off the bone's matrix and a zero axis has none.

The bone is an index rather than a name. The list is session state: written
every tick by the behavior that resolved the name against the very skeleton on
the Animator, and never serialized, so no index outlives the skeleton it was
resolved against. The list is held on the component rather than handed in per frame because
the pose is composed on frames the behavior does not run - a paused editor.
Nothing clears it: a behavior that wants a bend gone writes an
identity or drops the entry.

An adjustment takes effect when the pose is next composed, which is on the
tick - not on every drawn frame. A running frame between ticks draws the last
tick's pose, adjustments and all, so a bone bent to follow something that
moves every frame, such as first-person arms aimed along a camera turned by the
mouse, steps at the tick rate against it, which shows whenever the tick rate is
below the frame rate. Such a project runs a tick rate at or above its frame
rate, or parents what must stay on the camera to the camera rather than to a
bone. Composing every rig on every frame would remove the step, at the cost of
a pose per character per frame instead of per tick.

It is the seam between a clip and what only the game knows, at the smallest
shape that serves what has asked for it: a spine bent to the aim, legs folded
into a crouch, a torso knocked by a hit, arms solved onto a weapon, a head
hidden from the eye inside it. `tests/animation/animation_tests.cpp` pins the
frame: the same bend on a spine whose hips have been yawed a quarter turn still
swings the head to model +Z, which an adjustment applied in the parent's frame
would not.

## Animation events

A clip carries **markers** - named instants it announces as the head passes
them. Crossing one enqueues an `AnimationEvent` on the `EventBus`:

```cpp
struct AnimationEvent {
    EntityId    entity;  // the entity carrying the Animator
    std::string marker;  // the crossed ClipMarker's name
};
```

Gameplay listens for it the way it listens for a contact or a UI click - there
is no `Behavior` hook, because the event names the rig rather than the listener:

```cpp
subscribe<AnimationEvent>([this](const AnimationEvent& e) {
    if (e.entity != m_player || e.marker != "footstep") return;
    if (!m_grounded) return;                       // gameplay decides, not the clip
    VoiceParams params;
    params.position = scene().get<Transform>(m_player).position;
    events().emit(PlaySoundEvent{m_footstep, params});
});
```

That is the point of the whole feature: the footstep fires from the animation
rather than from a timer beside it, so it stays in step when the stride speeds
up, slows down, or is retimed by whoever authored the walk. The clip says
*when*; gameplay says *whether* and *what it sounds like*. A request rather
than an `AudioSource` because a fast stride asks for the next footfall before
the last one has finished, and a source is a speaker rather than a queue - see
[Starting a sound with no entity](audio.md#starting-a-sound-with-no-entity).
Markers are authored in the clip's recipe - see
[AnimationClipAsset](resources.md#animationclipasset).

### Once per crossing, and only once

A marker is announced when the playback head **passes the instant it names**.
The sweep a frame makes is closed at the end it arrived at and open at the end
it left, in both directions, which is what makes that exact:

| The head... | Announces |
|-------------|-----------|
| lands exactly on the marker | yes - the arrival end is closed |
| moves off it again, either way | no - the departure end is open |
| wraps past the end of a looping clip | yes, on the lap it wraps into |
| runs backwards over it (negative `speed`) | yes, once |
| steps over several whole loops in one frame | once, not once per lap |

The last row is the hitch case. A frame that swallowed two seconds of a
half-second clip drew **one** pose, so it makes one sound; replaying four laps'
worth of footsteps into a single frame is a burst nobody authored.

`PlaybackStep` is what makes the once-ness hold across frames rather than only
within one. It records where the head was, where it now *is*, and how far it
signed-travelled - and the `to` it records is the exact float the `Animator`
now carries, so it is bit-for-bit the next frame's `from`. The arrival end of
one sweep and the departure end of the next are the same value, so no rounding
step and no wrap can put a marker in both or in neither.

Three things deliberately announce nothing:

- **A paused frame.** `advancePlayback` returns before it moves anything when
  the simulation delta is zero, so nothing travels and nothing is crossed. Nor
  is anything stored up: the next running frame advances by that frame's delta,
  not by the length of the pause, so there is no burst on resume.
- **Scrubbing an `Animator::time` by hand** - in the inspector, or from code.
  That is a teleport, not a sweep; the head is somewhere else next frame with no
  travel recorded, so no marker between the two fires.
- **A crossfade's outgoing clip.** Two heads advance during a blend, and only
  the one the `Animator` is *playing* speaks. Letting both would fire the run's
  footstep and the walk's at once, which is the double the whole shape is built
  to prevent. The incoming clip owns its events from the moment `crossFadeTo`
  names it - so a marker at time 0 is not announced when a clip starts (the head
  begins there, it never crossed it), only when a loop wraps round to it.

Publishing is **serial, after the parallel evaluate pass**: the `EventBus` is
main-thread only, and a `PlaybackStep` on each rig's work record is three floats
that make deferring it free. The events are `enqueue`d, not `emit`ted, so they
deliver at the top of the next Simulation stage the engine runs - the next
tick's, or this frame's own pass when no tick is left - as a contact's do.
`AudioSystem` runs in the Transform stage after that, so the sound starts on the
frame the marker was crossed.

### What a harness proves about it, and what it cannot

Every claim above is a statement about how many events landed on a bus over a
run of frames, so the marker harness counts them off a real
`SkeletalAnimationSystem` posing a real `Scene` (`tests/animation/animation_tests.cpp`).
It drives the `Clock` through `requestStep()` - the editor's own single-step
path - so each frame is exactly one fixed step and the expected counts are
arithmetic rather than a tolerance:

- a looping clip announces each of three markers exactly ten times over ten
  laps, with no marker twice in a row and no frame announcing more than the one
  marker it crossed
- a negative `speed` comes back over them the same number of times, in the
  reverse order
- a non-looping clip announces its end marker on the frame the head lands there
  and nothing after it stops - which frame that is depends on how sixty float
  additions accumulate, so the test runs until the head clamps rather than
  naming one
- a crossfade whose incoming and outgoing clips both carry markers announces
  **only** the incoming clip's, with the fade verifiably still in flight
- three hundred paused frames announce nothing and move nothing, and the frames
  that resume announce the one marker a lap reaches
- a head scrubbed past a marker announces nothing, and neither does a stopped
  `Animator` over three hundred running frames
- two characters on one clip, half a lap apart, each announce for themselves

The cook is covered elsewhere: the `cook` suite round-trips a clip's markers
through the cooked file, and the `hostile` suite reads damaged ones. Whether a
recipe's authored markers are sorted and range-checked on the way in
(`usableMarkers` in `import/model_loaders.cpp`) nothing tests.

What no test can say is whether a footstep lands where the eye says the foot
does. Nothing here watches a heel meet the ground, hears a step arrive early
against the pose, or judges whether a marker at the quarter point is where an
animator would have put it. That needs a person watching the runner with the
sound on.

## Bone sockets

`PoseBuffer::global()` is how something other than the renderer reads a pose,
and a socket is one such reader. A weapon in a hand, a hat on
a head, a muzzle flash at a gun tip: one component, one system, and no new
channel between them.

```cpp
struct BoneSocket {
    std::string bone;    // "hand.R"
    Transform   offset;  // placement on that joint, in bone space

    // Transient - runtime only, never serialized.
    SkeletonHandle resolvedRig;
    std::string    resolvedName;
    int32_t        boneIndex = -1;
};
```

**The socket is the attached entity**, not a marker something else hangs off. A
marker would be a second entity per attachment carrying a Transform nobody
authors, listed in the panel and written to the scene file - the cost an entity
per bone was refused for, charged per attachment instead - and the thing being
held would sit one level below it for no information gained. So the gun carries
the `BoneSocket`, and a muzzle flash parented under the gun is just a child.

**The bone is named, never indexed.** An index is what the pose arrays are
addressed by and it is a lookup cheaper, but it describes one export of one rig:
re-export a character with a joint inserted and every stored index silently
addresses its neighbour - a weapon on the elbow, and no error anywhere. The name
is the joint's durable identity, which is already what a clip binds by at cook
time. It is the same call `PrefabEntity` makes for the same reason: an authored
reference has to survive the thing it points into being rebuilt.

The lookup is linear, and `SkeletonAsset::indexOf` says it is not a per-frame
call, so the answer is memoised on the component against both halves of the
pairing - the rig handle and the name. Change either and it re-resolves on the
next frame, which is what makes retargeting a socket in the inspector immediate
rather than a reload away. Failure is memoised too: a name the rig does not
carry resolves to -1 once instead of rescanning a hundred bones every frame to
fail again.

### The parent is the rig, and has to be

A socket must be a **direct child of the entity carrying the `Animator`** - the
same arrangement import already produces for skinned meshes. That is not a
convenience, it is what the ordering below buys:

```
worldOfSocket == rigWorld * poses->global()[slice->first + bone] * offset
```

`BoneSocketSystem` writes the socket's **local** `Transform` as
`global[bone] * offset` and lets `HierarchySystem` supply the `rigWorld`
half by its usual `parentWorld * local`. That only lands on the bone when the
parent's world matrix is the frame the pose was composed in, which is the rig's.

What the hierarchy reads is a TRS, not a matrix, so the product goes back
through `Transform::fromModelMatrix` - the inverse of the `computeModelMatrix`
beside it. It is exact for a chain of translations, rotations and
uniform scales, which is what a rig composes, and it carries a mirrored basis on
one scale axis rather than handing `quat_cast` a reflection to read as a
rotation. Shear is not representable as a TRS at all and is dropped; it appears
only when a non-uniformly scaled joint carries a rotated child, the same case the
skinned vertex stage already approximates the lighting of.

An axis scaled to **nothing** is answered rather than refused. A clip that hides
a joint by keying its scale to zero produces exactly that, and so does an offset
scale dragged through zero in the inspector; dividing the basis by it would hand
`quat_cast` a NaN, and the socket would then write a NaN `Transform` that spreads
through every world matrix under it. The scale comes back as zero and the
rotation is read from the axes that survived - the lost one is the axis those two
imply, and with two of them gone there is no rotation left to recover and the
identity's column stands in.

### Which stage, and why the frame it runs in matters

`SystemStage::Transform`, registered **ahead of `HierarchySystem`**. A socket is a
derived transform and that is the stage derived transforms belong to; both
neighbours in the ordering are load-bearing:

| Boundary | What it buys |
|----------|--------------|
| After the Simulation stage | `ctx.poses` is a per-frame product. Reading it from Simulation would depend on registration order inside a stage; reading it from Transform cannot. |
| Before the world resolve | The socket is on its bone the frame the character moves, including the **first** frame, when there is no previous frame to have cached anything. |

Get the second one wrong and the failure is invisible in a screenshot. The
obvious alternative - read the rig's `WorldTransform`, write the socket's - reads
a matrix `HierarchySystem` last wrote *a frame ago*, so the attachment trails the
character by exactly one frame while it runs and is exactly right whenever it
stands still. It also strands anything parented **under** the socket, because
that walk has already happened by the time such a system would run: the muzzle
flash would resolve against last frame's gun.

Placement runs every frame rather than on the tick, for the reason a paused
frame recomposes the pose: scrubbing an `Animator` while paused has to move what
the character is holding, or a paused preview shows a pose its props disagree
with.

Because the `Transform` is an output, it is rewritten every frame - authoring one
on the socket entity does nothing. The offset is the authored half, and the
inspector says so on both cards.

### The six ways a socket has nothing to stand on

Every one of them is silent on screen - an unplaced socket simply stays where it
last was, which for a fresh one is the world origin and for a moved one is a
plausible-looking lie. So each is named in the log, edge-latched like the pose
system's own faults, so it is reported once per gap rather than once a frame -
each fault on a latch of its own, so naming one never silences another.

| Fault | What is said |
|-------|--------------|
| No pose published at all | Named once for the whole scene, not per socket: this is `BoneSocketSystem` running without `SkeletalAnimationSystem` ahead of it, which is a wiring mistake |
| No `Transform` on the socket | There is nothing to write the bone's place into |
| Not a direct child of a rig | The socket hangs off the entity carrying the `Animator`, not off a mesh under it |
| The rig above it posed nothing | Its `Animator` names no live skeleton |
| The rig's skeleton has no bones | There is no bone for any name to find |
| The rig has no bone of that name | Including the empty name a freshly added component starts with |

### What it costs a scene with no sockets

One null check on `storage<BoneSocket>()`. No pose lookup, no hierarchy walk, no
allocation - the same rule the vertex format and the palette follow, applied to
one more stage: a scene without characters pays nothing for the machinery that
attaches things to them.

## Seeing it

**View > Show Skeletons** draws every posed rig straight out of `ctx.poses`:
a segment from each bone to its parent, a dot at every joint, and an axis triad
per bone on the selected rig. Segments say where the joints are; only the axes
say which way they face, which is what a composition or bind-inverse mistake
actually corrupts.

The **Animator card** in the inspector authors the same thing: a rig picker, a
clip picker, and a transport whose scrub works while paused, for the same reason
the overlay does - a paused frame composes the pose again from wherever the head
now is. A clip cooked against another rig is named on the card, beside the
pickers that made the pairing. See [editor.md](editor.md).

### Hearing it

`examples/potion_runner` runs the whole chain, built in code with no asset file
anywhere. `runner_rig.h` makes a five-bone rig and one looping stride clip; the
runner's four limbs hang off its bones through `BoneSocket`s; the clip carries a
`footstep` marker at each instant a leg is vertical, which is when that foot is
on the ground. The behaviour subscribes to `AnimationEvent` and plays a
synthesized footstep (`proc_audio.h`) - but only while alive and grounded,
because what a marker *means* is gameplay's decision and not the clip's.

`./build/bin/vkm_runtime examples/potion_runner`. The footsteps should stay in
step as the run accelerates (one `Animator::speed` drives both the swing and the
markers, because they are the same timeline), stop in mid-air, and come back on
landing.

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### Keyframe storage

There is no `Keyframe<T>` struct. An `AnimationTrack<T>` stores its keyframes as
two parallel, time-sorted vectors:

```cpp
std::vector<float> m_times;   // keyframe times, ascending
std::vector<T>     m_values;  // value at each time (vec3 or quat)
```

`addKeyframe(time, value)` inserts into both at the position that keeps `m_times`
sorted, and `getValue` binary-searches `m_times` for the enclosing interval. The
parallel-array layout keeps the time lookup cache-friendly and avoids an
array-of-structs.

### The pose, and the palette derived from it

`SkeletalAnimationSystem` publishes a `PoseBuffer` on `FrameContext::poses`, the
same way `VisibilitySystem` publishes `ctx.visibility`. It holds **two** parallel
matrix arrays, and one never overwrites the other:

| Array | What it is |
|-------|------------|
| `global()` | Each bone's transform in rig model space. This is *the pose*. |
| `palette()` | `global[b] * inverseBind[b]` - the form a vertex stage wants. |

The palette is derived from the pose, never the reverse: recovering the pose from
the palette means inverting the bind matrices per bone, and the pose is what an
attachment, a socket or a physics body reads.

Rigs are packed end to end and addressed by slice:

```cpp
struct PoseSlice {
    uint32_t  first, count;       // into global() and palette()
    glm::vec3 originMin, originMax;  // box of the posed bone origins, rig space
    float     maxBoneScale;       // largest scale any bone carries in this pose
};

const PoseSlice* slice = ctx.poses->sliceOf(entity);  // null = not posed
```

`sliceOf` answers for the rig entity **and every descendant of it**, stopping
wherever a nested `Animator` takes over - so a mesh entity three levels down
finds its own character's pose with one lookup.

The two bounds fields are what the *pose* knows; they are not a bounding box on
their own, because skin hangs off a bone by a distance only the mesh knows. They
are published raw and inflated by `VisibilitySystem`, which has the mesh in hand
- see [visibility.md](visibility.md#posed-bounds). That inflation is mandatory,
not polish: the frustum and shadow culls keep exactly what the box says, so a
box that misses the posed skin deletes it.

### How a rig is posed

Five phases, only the third parallel:

1. **Allocate** (serial). Walk `storage<Animator>()`, resolve each handle, drop
   any rig whose skeleton is gone, and hand out a slice per rig. Serial because
   each slice's range is a running total - and because the parallel phase must
   never touch the `ResourceManager`. The rigs are then sorted by slot, so every
   phase below - the announce included, whose events gameplay reacts to - walks
   them in an order the world decides rather than their storage's history.
2. **Map** (serial). Stamp each rig's entity and its subtree with its slice.
3. **Evaluate** (`parallelFor`). Advance the playback head - and the outgoing
   one, and the fade countdown - then compose. Each rig records the
   `PlaybackStep` its own head made.
4. **Announce** (serial). Walk those steps and enqueue an `AnimationEvent` per
   marker crossed. Serial because the `EventBus` is main-thread only.
5. **Publish**. `ctx.poses` points at the system's own buffer.

A rig an active ragdoll has taken over is composed from its bodies in phase 3,
on the tick - and the tick reads them before `PhysicsSystem` moves them, since
`RagdollSystem` needs the pose ahead of the solver. So **a ragdoll's skin trails
its bodies by one tick**, and catches them up when they come to rest. Composing
it again after the solver would mean a second pose pass every frame a ragdoll
is live, against a world a behavior's `update` may have changed since the tick;
a tick of lag on a falling body is the cheaper answer.

Composition is **one forward loop with no recursion, no visited set and no
intermediate array of local transforms**:

```cpp
Transform local = skeleton.bindPose[i];       // a channel the clip lacks holds bind
if (clip) sampleBone(*clip, i, sample.time, local);
if (blending) {                               // the crossfade, before composition
    Transform leaving = skeleton.bindPose[i];
    sampleBone(*from, i, sample.fromTime, leaving);
    local.position = glm::mix (leaving.position, local.position, weight);
    local.rotation = glm::slerp(leaving.rotation, local.rotation, weight);
    local.scale    = glm::mix (leaving.scale,    local.scale,    weight);
}

for (uint32_t a = 0; a < sample.adjustCount; ++a)  // what a behavior added, after the blend
    if (sample.adjust[a].bone == int32_t(i))
        adjustBone(local, sample.adjust[a], parent < 0 ? nullptr : &global[parent]);

const glm::mat4 bone = Transform::computeModelMatrix(local);
global[i]  = (parent < 0) ? bone : global[parent] * bone;
palette[i] = global[i] * skeleton.inverseBind[i];
```

That is possible only because `parent < index` is a *validated invariant*
(emitted that way by import, stated once by `findSkeletonFault` and checked by
the cooked reader and writer and by the system itself): a bone's parent is
always already composed, so its local TRS never has to outlive one iteration. It
is also where blending attaches - on `local`, before composition - and where a
`BoneAdjust` lands after it.

What to sample is a per-frame `PoseSample`, not the `Animator`: one clip, or two
and a weight, and the bones a behavior adjusts. Nothing in it is written to a
file.

The pose is composed **once per tick**, with the tick that advances the heads,
and **once per paused frame**, which runs no tick: `update` composes a
zero-length step from where the heads stand, so scrubbing an `Animator` while
paused shows the pose it names. A running frame that happens to take no tick
draws the last tick's pose, which is the pose the simulation is actually in -
unless the world was replaced since that tick (`Scene::epoch()` moved, as a
`loadScene` does mid-frame). The buffer maps entity slots, which the new world
reuses, so that frame composes a zero-length step too rather than dress the new
world's entities in slices sized for the old one's rigs. Composition is
idempotent, so recomposing an unmoved head changes nothing.

Within one world the map keeps each slot's whole id, generation included, and
`sliceOf` answers only for that id. An entity destroyed between ticks and its
slot handed to a new one leaves the newcomer unposed until the next tick
composes it, rather than skinned from a slice sized for the rig the slot was
posed by. The epoch test above is still needed: a replaced world counts
generations afresh, so an id there can equal one from the world before.

A clip whose per-bone table is not parallel to the rig, or whose `skeleton` names
a different rig, is refused: the bind pose stands and the mismatch is logged
once. Playing it would pose the wrong joints out of matching indices, which is a
character that moves *nearly* right.

A skeleton or clip built in code - `potion_runner` builds its runner that way -
never passed the cooked reader, so the system judges both by the rules the reader
uses, `findSkeletonFault` and `findClipFault`, before indexing anything: a rig
that fails is left unposed and a clip that fails holds the bind pose, each
logged once per gap. The check is a pass of integer comparisons over the bones,
paid serially per rig per compose. Both clips of a fade are checked separately,
so a bad outgoing clip cannot take the incoming one down with it.

### The frame a pose lives in

Skinned vertices resolve into the rig's model space, so the matrix that puts a
posed character in the world is the **rig entity's** world matrix:

```
worldOfBone[b] == rigWorld * poses->global()[slice->first + b]
```

Import guarantees it twice over:

- the `Animator` goes on the entity whose frame the bones are composed in - the
  parent of the root bone's node, or the import root when the rig is rooted at
  the scene node itself. One node off and every bone is displaced by exactly that
  node's transform, which looks entirely plausible until it is compared against
  something;
- every **skinned mesh gets its own entity, parented to that rig entity at
  identity**. The inverse-bind matrices already carry whatever placed the mesh in
  rig space, so a mesh left under its own node would be transformed twice.

Bone nodes themselves spawn no entities. A bone is an index in the skeleton
asset, and an entity per bone would put a hundred of them per character into the
hierarchy panel, the Transform walk and the scene file for data nobody authors.
Pruning is by whole subtree - a node is dropped only when it and everything under
it is a bone with no mesh - so a prop parented to a hand keeps the chain of bones
that places it, and nothing is ever re-parented to an ancestor it did not sit
under.

Hand-authoring can still break either invariant, so the pose system names both,
once per gap:

| Fault | What it does if unnamed |
|-------|------------------------|
| A skinned mesh whose `MeshAsset::skeleton` is not the rig above it | Its bone indices address the wrong joints - a character that moves *nearly* right |
| A skinned mesh **below** the rig sitting off its origin | Its own transform is applied on top of a palette that already resolved into rig space |

The second fault is a question about descendants only, and the emphasis is the
whole of it. The **rig entity's** own transform is not a second matrix stacked on
the palette - it *is* the matrix the palette is multiplied by, and it is what
puts the character somewhere other than the world origin. A rig can carry a
skinned mesh itself (a one-mesh file whose rig is rooted at the scene node), so
it is walked like any other entity; testing its transform for identity there
would report every placed character in the scene. The rig-name half of the check
still applies to it, because that one is about the mesh, not about where it
stands.

### The GPU path

Skinning is a vertex-stage difference and nothing else, expressed as two extra
programs rather than one program with a branch.

**The rig binding is a second vertex stream**, at locations 8/9 with divisor 0:
a mesh whose `MeshAsset::skin` is non-empty goes into the `GLMeshPool`'s
skinned layout, whose vertex array reads that stream beside the vertices,
indexed alike. It is not four
more fields on `Vertex`, which stays 48 bytes: folding it in would cost every
vertex of every mesh in the engine 25% more bandwidth, paid hardest by the shadow
pass, which reads only `aPos` and replays the geometry per cascade tile and per
cube face. A rock does not pay for skinning.

Because the stream is exactly `vertices.size()` long at divisor 0, leaving 8/9
enabled costs nothing when a program that never declares them draws the same
vertex array.
`GLSceneCapture` and `GLPreview` do exactly that, so a character bakes into GI
and thumbnails in bind pose - the right answer for both.

**The palettes travel as one flat array.** `RenderView::skinMatrices` is the
frame's `PoseBuffer::palette()`, borrowed whole, and each object carries a range
into it - its rig's slice, so every mesh a rig drives shares one copy of its
bones. The range is stamped by the cull, which already looked the slice up to
size the posed bounds, into the object's own columns: `RenderObjects::skinFirst`
and `ObjectDraw::skinCount`. Every list reads the same stamp, which matters
because the lists are gathered from different sets - a character standing just
off-screen and casting into view is in the scene-wide list alone.

`skinFirst` is a column of its own rather than a field beside the handles
because it goes to the GPU as it stands: the backend uploads it next to the
model matrices, one entry per object, when the frame posed anything.

```
VisibilitySystem (the cull)   stamps each skinned mesh with its rig's range
RenderView::build             skinMatrices: the pose buffer's palette, borrowed
GLBackend::render
  |-- GLSkinPalette::update(view.skinMatrices)   one upload, SSBO binding 5
  |-- GLObjectBuffer::upload                      per-object skinFirst, SSBO binding 6
```

The per-object base is indexed by the **object index**, the value the
instance-index attribute carries, like the transforms. A camera batch and a
shadow tile hand the vertex stage lists of object indices into the same two
buffers, so a skinned caster draws instanced in a shadow tile exactly as it does
for the camera.

**A run is skinned or it is not**, and `InstanceDraw::skinned` leads the batch sort
key ahead of material and mesh, so a bucket switches program once. It takes two
things to be true - the GPU mesh carries a skin stream, and the frame posed this
instance. A skinned mesh with no rig above it fails the second, draws through the
static program, and renders the vertices it stored, which *is* its bind pose. No
per-instance branch in any vertex stage decides that.

**An empty palette switches the whole apparatus off.** A frame that posed nothing
leaves `skinMatrices` null, and every backend stage keys off that one fact rather than
discovering it per item:

| Stage | With no palette |
|-------|-----------------|
| `VisibilitySystem` | a skinned mesh's slice lookup finds none, so every object's bone count is 0; a mesh with no skin is never looked up |
| `GLObjectBuffer` | no per-object palette base uploaded or bound |
| `GLInstanceBatcher` | no mesh resolve per object to decide the program, whose bit in the sort key is then always 0 |
| `GLDepthPrepass` / `GLForwardPass` | the skinned program is not bound and not given the frame's uniforms |
| `GLShadowPass` | the skinned programs are never bound, and a tile's solid casters are one multi-draw per vertex layout, since its runs are keyed by layout first (`ShadowRun::key`) |

That is the same rule the vertex format follows, applied to the frame rather than
to the mesh: a scene with no characters pays nothing for the machinery that draws
them.

The skinned programs are plain path-constructed shaders, so hot reload tracks
them with no new code:

| Program | Pairs with |
|---------|-----------|
| `shaders/forward/pbr_skinned` | `forward/pbr` |
| `shaders/forward/prepass_skinned` | `forward/prepass` |
| `shaders/shadow/depth_skinned` | `shadow/depth` |
| `shaders/shadow/depth_masked_skinned` | `shadow/depth_masked` |

Both stages of each are includes of its pair rather than copies. The vertex
stage is `#define SKINNED` above an `#include` of the static vertex file, which
poses position, normal and tangent under `#ifdef SKINNED`; the fragment stage
includes the static fragment file. A lobe added to the ubershader, or a change
to how a vertex is placed, can never reach only half the scene.

`GLShadowPass` also tracks which program is current across the whole pass. A
program has to be bound to be given a uniform, and the pass hands a matrix per
atlas tile and per cube face - to each program the first time a run of that
tile binds it; binding every program per tile makes `glUseProgram` the largest
thing it does when only one program is ever used.

Both skinned shadow variants reach the position through the same
`skinnedWorldPosition` the camera path uses, so a character's shadow is cast by
the geometry the camera sees rather than by something standing near it.

Both skinned vertex stages take their position from one expression,
`skinnedWorldPosition(model, base)` in `shaders/skinning.glsl`. That is
what makes the depth agreement structural - the forward pass draws against the
depth the prepass primed under LEQUAL with writes off, so the two programs must
compute `gl_Position` identically, and there is only one expression for them to
compute it from.

Uniform state is per program in GL, so `GLForwardPass::bindFrameUniforms` gives
both programs the identical frame set from one place. A uniform added to only one
of them would go silently missing on characters and nowhere else.

#### Two limits the vertex stage carries on purpose

**Normals are exact under uniform bone scale, approximate under non-uniform.**
The skinned stages compute `normalMatrix(model) * (mat3(skinMatrix) * aNormal)`
- the normal matrix, the cofactor of the model's 3x3 derived in the vertex
stage, covers the *model* matrix, and the skin matrix reaches the normal
directly. A bone scaled evenly only changes the vector's length, which
the following `normalize` absorbs, so the common case is exact. A bone scaled
unevenly tilts the normal off the posed surface, and the lighting is wrong by
that much. Correcting it means the skin matrix's own normal matrix per vertex
as well, in the two skinned programs that carry a normal (`forward/pbr_skinned`,
`forward/prepass_skinned`), for a case rigs essentially never author. Clips keep
their scale tracks, and this is the price.

**A bone index is bounds-checked when the cooked mesh is read, not in the shader.** `readMesh`
refuses any index past `MAX_SKELETON_BONES`, but the shader reads
`b_skin[base + aBones.x]` with no clamp against the rig's own bone count. That is
sound for a mesh under the rig it was skinned to, which is the only arrangement
import produces. Move a skinned mesh under a *different* rig and its indices
address that rig's slice, or run off the end of the palette entirely - the same
misuse `SkeletalAnimationSystem` already names in the log ("its bone indices
address the wrong joints"). Within the palette it reads another rig's bones;
past its end it reads outside what `GLSkinPalette` uploaded, which GL leaves
undefined. The warning is the answer, not a per-vertex clamp.

### Key files

- `src/engine/system/animation/animation_system.h` - AnimationSystem
- `src/engine/ecs/component/animation/animation_track.h` - AnimationTrack<T> (keyframe storage lives beside the component that holds it)
- `src/engine/core/math/easing.h` - easing functions (interpolation curves)
- `src/engine/ecs/component/animation/animation.h` - Animation component
- `src/engine/system/animation/skeletal_animation_system.h` - SkeletalAnimationSystem
- `src/engine/system/animation/pose_evaluator.h` - `advancePlayback` + `crossesMarker` + `composePose`
- `src/engine/system/animation/animation_events.h` - AnimationEvent (the marker crossing)
- `src/engine/system/animation/pose_buffer.h` - PoseSlice, PoseWrite, PoseBuffer
- `src/engine/system/animation/ragdoll_pose.h` - RagdollBodyPose, one bone body's
  world pose read out of the scene before the parallel pass composes from it
- `src/engine/ecs/component/animation/animator.h` - Animator component
- `src/engine/ecs/component/animation/bone_socket.h` - BoneSocket component
- `src/engine/system/animation/bone_socket_system.h` - BoneSocketSystem
- `src/backend/opengl/frame/gl_skin_palette.h` - GLSkinPalette (the frame's palettes, SSBO 5)
- `shaders/skinning.glsl` - the vertex-stage skinning contract

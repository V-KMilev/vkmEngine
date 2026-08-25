# Audio System

Sound as ECS. A sound emitter is an entity with an `AudioSource` component
naming an `AudioClip` asset; the ear is an entity with an `AudioListener`. The
`AudioSystem` reconciles those components against a mixer every frame, and one
class - `AudioDevice` - is the engine's whole surface onto the audio backend.

> The one idea: **the engine owns everything with a name, the backend owns
> everything with a thread.** Which clips exist, which entities want to be
> heard, where the ear is and how long a voice lives are decided on the main
> thread by code that has never heard of miniaudio. Mixing, resampling,
> spatialization maths and the output device happen behind `AudioDevice` and
> nowhere else.

## Key files

- `src/engine/resource/asset/audio_clip_asset.h` - `AudioClipAsset` (decoded PCM + rate + channels)
- `src/engine/ecs/component/audio/audio_source.h` - `AudioSource` (clip, gain, pitch, loop, spatial, distances)
- `src/engine/ecs/component/audio/audio_listener.{h,cpp}` - `AudioListener` (the ear + master gain) and `findActiveListener`
- `src/engine/system/audio/audio_device.{h,cpp}` - `AudioDevice` (the backend seam; the only *engine* file that includes `miniaudio.h`)
- `src/engine/system/audio/audio_events.h` - `PlaySoundEvent` (the fire-and-forget request)
- `src/engine/system/audio/audio_system.{h,cpp}` - `AudioSystem` (component -> voice reconciliation)
- `src/tools/loader/audio_loaders.{h,cpp}` - `loadAudioClip` (wav / mp3 / flac import)
- `src/engine/io/asset/asset_cook.{h,cpp}` - the cooked `.vkmc` sound format
- `src/engine/io/asset/cooked_loader.{h,cpp}` - `loadCookedAudioClip`
- `modules/miniaudio` - the backend, absorbed privately into `vkm_core`

## The component model

| Component | Holds | Notes |
|-----------|-------|-------|
| `AudioSource` | clip handle, volume, pitch, loop, spatial, playOnStart, min/max distance | One entity, at most one voice. `playing` is the state the source *wants* to be in - gameplay writes it to start or stop, and reads it back to learn a one-shot finished. |
| `AudioListener` | active, volume | The pose every spatial source is heard relative to, plus the master gain. |

`playing` and `started` on `AudioSource` are runtime state: not reflected, not
serialized. They describe a play session rather than the authored scene, and a
scene row holding a half-finished sound would resume a noise whose beginning
nobody heard. `playOnStart` is the authored half of the same idea.

### Starting and stopping a sound from gameplay

There is no `play()` call. A behavior writes the field:

```cpp
void onTrigger(EntityId other) override {
    context().scene->get<AudioSource>(m_entity).playing = true;
}
```

and reads it back to find out when the sound ended:

```cpp
void onUpdate(float dt) override {
    if (!context().scene->get<AudioSource>(m_door).playing) openTheDoor();
}
```

One source is one speaker, not a queue: asking a source to play while it
already is does nothing, and a source whose voice retires on the very frame
gameplay asks again reports the sound finished rather than starting the next
one. That is the right shape for a thing that makes a sound - a door, a
generator, a radio - and the wrong one for a sound that has no thing.

Clearing `playing` does not cut the waveform where it stands: the voice is
ramped to silence over a few milliseconds first, for the reason in
[Stopping a sound is ramped](#stopping-a-sound-is-ramped). A deliberate fade
is still a per-frame `volume` ramp gameplay writes itself - the one below is
a declick, not a fade-out.

### Starting a sound with no entity

```cpp
VoiceParams params;
params.volume   = 0.55f;
params.position = context().scene->get<Transform>(m_player).position;
context().events->emit(PlaySoundEvent{m_footstep, params});
```

`AudioSource` is the answer whenever there is something making the sound, and
this is the answer when there is not. Two cases in the shipped example need it,
and neither can be expressed as a component:

- A **footstep** at the runner's top cadence. The stride announces footfalls
  138 ms apart against a 130 ms clip, so one speaker has 8 ms - under half a
  frame - to finish and retrigger in. Measured through the component: 0%
  dropped at walking cadence, **19% at cadence 1.875 and 48% at 2.0** - which a
  run reaches 33 and 40 seconds in, the second being the top speed its ramp
  clamps at. Through the request path: 0% at every cadence.
- A **coin**, which pays out in runs: coins are laid down in lanes of four
  3.6 m apart, so at top speed one run's chimes start 83 ms apart against a
  180 ms clip and a source on the player would swallow every ping after the
  first. Riding the coin is no better - collecting it switches the coin off
  where it stands, so there is no longer a thing there for the sound to be.

What makes a request safe to forget is that it hands back nothing. There is no
id, so nothing can stop it, move it or ask whether it finished - and therefore
nothing has to own it. Its voice is never entered in `AudioSystem`'s table, so
the sweep that stops voices whose source vanished never sees it; it plays to
its end and the next frame's `reapFinishedVoices()` releases it, which is the
lifetime `AudioDevice::play` already documents for a caller that never looks
back. `stopEverything` still reaches it, so a scene load silences requests
along with everything else.

That last sweep is deliberately blunt, and it is worth knowing how blunt. It
drops **every** request waiting to start, including one made against the graph
that just arrived, because nothing distinguishes the two: both were emitted
between the same pair of `AudioSystem` updates, and a handle from the old graph
is not merely dead - a swap hands the new graph its own generations, so an old
index can be alive there and name a different clip. Dropping the request is the
safe half of that trade. Only a load that lands *after* audio in the frame can
lose a good one, which in practice means the editor's UI stage: at runtime the
load happens in Simulation, the flip is consumed on that same frame, and the
next frame's requests go through untouched. Measured both ways.

`params.loop` is ignored, and that is the invariant that keeps the two paths
from overlapping: a sound with no end needs an id to stop it, so it needs a
component. `position` is a position rather than a follow - a request is fixed
where it was made, and a sound that must travel with a moving emitter wants a
source.

Requests are collected as they are emitted and started at the end of
`AudioSystem::update`, after the component walk and after the sweep, so a
request made during Simulation is heard on the same frame - the same guarantee
writing `playing` has.

## Per-frame flow

`AudioSystem` runs in the **Transform stage**, after `HierarchySystem` has
resolved the frame's world poses, so a parented source or listener is heard
where this frame put it rather than where the last one left it. It still hears
everything the frame decided, because Transform runs after Simulation: a sound
a script, an animation or a contact asked for starts on that same frame.

```
AudioSystem::update(FrameContext)
  |-- ResourceManager epoch moved? stop every voice - they belong to a world
  |     that no longer exists (scene load, editor Stop)
  |-- reap every voice that played to its end - one-shots, editor auditions,
  |     and stops that have finished ramping out, but never one the editor's
  |     transport is holding
  |-- findActiveListener: first entity with an enabled AudioListener and a
  |     Transform, storage order breaking ties
  |     |-- found: push its world pose + master gain
  |     `-- none:  disable the ear - spatial voices go silent, 2D ones play on
  |                at unity, the departed listener's gain going with it
  |-- for each AudioSource, posed by its Transform if it has one:
  |     playOnStart and simulation time has run? -> playing = true, once
  |     playing and no voice   -> start one from the clip (a closed device
  |                                yields no voice, so `playing` clears here)
  |     playing and voice ended-> playing = false, release the voice
  |     playing and voice live -> push volume / pitch / loop / position
  |     not playing and voice  -> stop it
  |-- any voice whose source was not visited (component or entity gone) is stopped
  `-- start every PlaySoundEvent collected since the last frame: one voice
        each, tracked by nobody, released by the next frame's reap once it ends
```

## Time, pause and the editor

`AudioSystem` steps no time of its own: the mixer is pumped by the device's own
thread, so there is no delta here for anything to advance. It runs every frame
and touches the Clock only through `getSimDelta()`, and only to tell a running
simulation from a paused one. That follows the engine-wide rule that **a system
reads the timeline its responsibility lives on**: simulation state (animation,
particles, physics, gameplay `onUpdate`) runs on simulation time; presentation
and services (input, camera, editor, async loading, audio, gameplay's
[`onRealtimeUpdate`](scripting.md#time-and-pause)) run every frame regardless of
it.

So pausing does **not** cut the music, silence a menu, or stop a UI click from
being heard - and `onRealtimeUpdate` is where the behavior that answers that
click still runs. The world stops moving, so 3D positions stop changing because
nothing moved. The single thing pause holds back is `playOnStart`, which waits
for simulation time to advance - which is what keeps an unplayed scene sitting
open in the editor quiet, since in the editor "paused" and "not playing" are
the same state.

### Two pauses wearing one word

The rule above is a **game's** pause, and it is the right rule: a pause menu
that cut its own music, swallowed the click that opened it and silenced the
menu behind it would be a bug. The **editor's transport** is a different pause
wearing the same word. There the world was frozen deliberately, to be looked
at, and the level's ambience playing on underneath it is noise nobody asked
for - which is exactly what pressing Pause used to do.

Nothing in `AudioSystem` knows about the second one, and that is the point. The
editor holds the voices itself:

- `PlaybackBar`'s Pause calls `AudioDevice::pauseAllVoices()`, and Resume
  `resumeAllVoices()`, through the `AudioSystem::device()` handle the editor
  already uses to audition clips. The system's contract is untouched, so a game
  that ships never inherits the editor's rule.
- A held voice is *held*, not stopped. The mixer keeps the sound and only its
  clock stops, so Resume continues from the sample the pause landed on rather
  than starting the clip again.
- The hold is ramped over the same five milliseconds a stop is, because a pause
  is also a cut at whatever sample the cursor is on. Swept across the phases of
  a 220 Hz tone at gain 0.8, cutting outright steps by **0.80** - thirty-five
  times the waveform's own steepest step - at the pause and again at the
  resume; ramped, both stay inside that own step. The cost is that the hold
  lands about ten milliseconds late, the sound playing through its own fade.
- Only what was sounding when the button was pressed is held, and only what
  that press held is let go. So a clip auditioned while the world is frozen is
  audible - being unable to hear a file because the world is paused would be
  the same mistake pointing the other way - and an audition somebody paused on
  purpose stays paused when the world resumes.
- `reapFinishedVoices` had to learn the difference. Pausing stops the node, and
  a stopped node is also what a voice that ran out looks like; the only thing
  separating them is the voice's `hold`, one field naming who is holding it -
  nobody, the caller that asked for this voice, or the transport. One field and
  not a held flag beside an owner flag, because the two can disagree and the
  disagreement is expensive: a voice that has been stopped but is still
  remembered as the transport's comes back on the next Resume, playing on with
  `find()` hiding it, which leaves nothing above the device able to stop it
  again. `stopVoice` hands the hold back, so a voice stopped while it was held
  is swept rather than resurrected, and asking to hold a voice the transport
  already holds takes it over rather than ramping it twice.

Pressing Stop restores the play snapshot, which replaces the asset graph; the
epoch check above stops every voice on the next frame.

## Spatialization

A spatial source is attenuated **linearly** between `minDistance` (full volume)
and `maxDistance` (silent). That model is chosen so both authored numbers mean
what they say. The inverse-square model most backends default to never reaches
zero, which leaves `maxDistance` meaning nothing but a clamp and every sound in
a level faintly audible from everywhere in it.

Every gain in the system - `AudioSource::volume`, `AudioListener::volume`, a
`PlaySoundEvent`'s - is a **linear gain**, multiplied into the mix as written.
0.5 is half the amplitude, which is roughly two thirds as loud rather than
half. That matters at exactly one place: a player-facing volume slider is
perceptual, so the settings menu owes the conversion (`position * position`, or
a decibel curve) before it writes `AudioListener::volume`. Storing the slider's
own position here instead would put a UI decision inside the mixer's contract
and leave gameplay unable to reason about what multiplying two gains means.

A gain that is negative or not finite is heard as **silence**, wherever it came
from - a source's field, a request's, or the listener's master. That floor is
not tidiness. Every voice sums into one master, so a single infinite gain takes
every sample of the mix non-finite and silences the whole game until that voice
is reaped: measured, two healthy voices beside one at `+inf` produced 4800 NaN
samples out of 4800, and recovered only when the bad voice went. A
`PlaySoundEvent` carries whatever arithmetic gameplay did, unchecked, so the
floor lives at the device, where every path already passes through. NaN was
always answered this way - `std::max` keeps its first argument when a comparison
against a NaN comes back false - and infinity now is too.

`spatial = false` bypasses all of it and mixes the clip flat - what music,
narration and UI clicks want.

### A positioned source wants a mono clip

The reason is sharper than "stereo already encodes a position", and it is worth
stating exactly because the loose version led to a warning that named the fix
without the failure. The mixer routes each of a voice's channels to the output
channel it was authored for and attenuates it there. Nothing crosses. Measured
against the offline mixer, with a listener at the origin facing `+Z` and an
emitter walked across it:

| emitter at | mono clip | 2ch clip, sound in channel 0 only |
|---|---|---|
| `x = -8` | L 0.1371  R 0.6857 | L 0.1371  R **0.0000** |
| `x =  0` | L 0.8000  R 0.8000 | L 0.8000  R **0.0000** |
| `x = +8` | L 0.6857  R 0.1371 | L 0.6857  R **0.0000** |

The mono clip swings across the pair as it passes. The stereo one never reaches
the second output channel from any position at all - half its field is
unreachable, and no placement in the world recovers it. Distance attenuation
still applies to both, which is what keeps the failure quiet: the sound gets
nearer and further correctly, it just cannot move sideways.

Quieter still: a stereo clip whose two channels are *identical* is
indistinguishable from the mono one - measured, L 0.1437 R 0.7183 for both at
`x = -6`. So testing the pairing with a centred recording passes and proves
nothing. It is wide clips that lose, and they lose silently.

Hence a warning rather than a conversion, in two places. The Inspector's card
says it where the mistake is being made, and `AudioSystem` says it once per clip
when a positioned voice starts, because a project that plays entirely through
`PlaySoundEvent` owns no `AudioSource` and so has no card for it to be said on -
the same pairing the missing-listener line already has. Per clip rather than per
world, which is where it parts from that line: two stereo clips are two
mistakes, and one message would hide the second. See [Not
implemented](#not-implemented) for why nothing downmixes the file for you.

A `Transform` is what a spatial source is positioned by, but it is not what
makes the source run. Every source is reconciled whether or not its entity has
a pose - a flat one needs none, and the entities most likely to carry one, a UI
button or anything a behavior spawned, have no `Transform` at all. A spatial
source without one is heard at the world origin, which the inspector names on
the card, rather than being silently skipped with `playing` stuck true.

Handedness works out with no correction. The engine's forward is `+Z` and
screen-right is `-X` (`core/math/axes.h`), and the backend derives its own right
vector as `cross(forward, up)`, which is `-X` for that basis - so a source at
world `-X` is heard from the right speaker. The harness measures this rather
than asserting it in prose.

Doppler is off: nothing in the engine tracks velocity, so there is no shift to
compute. Cones, per-source directivity and reverb are not implemented.

### Two cameras, one ear

The listener is a component of its own and is deliberately not tied to the
camera. A third-person game hears from its character while looking from an orbit
rig, and a cutscene camera should not move the ear. So "which camera do we
listen from" is not a question this system answers - `AudioListener` is placed
on whichever entity should hear. Two listeners is a question, and the answer is
the same one `findActiveCamera` gives for the eye: the first enabled one wins,
storage order breaking the tie.

That rule is stated once, in `findActiveListener` beside the component, because
everything that answers "which listener" has to agree on it: the system placing
the ear, the two Inspector cards that say which listener is heard from and warn
a positioned source that there is no ear at all, and the viewport, which dims
every listener icon except the one it picks. It takes no cached-entity hint -
`findActiveCamera` has one because two systems each keep a cached camera entity,
and nothing on the audio side caches one.

With **no** active listener there is nothing for a distance to be measured
from, so spatial sources go silent while non-spatial ones play on untouched.
The engine says so once per world - and only when a positioned sound actually
tries to start, a source's voice or a request's, because a project with no audio
in it should not be told it is missing an ear. The world that replaces this one
earns the line again: the flag is cleared with the voices, by the same
`stopEverything` a scene load and the editor's Stop go through.

The master gain goes back to **unity** at the same moment, because the gain
belonged to the ear rather than to the world - `AudioListener::volume` is what
set it. Turning the listener off only silences the *spatial* voices; the master
multiplies the 2D ones too, so a listener at half volume that is deleted, or
merely unticked, would otherwise leave the music and the UI at half volume with
nothing on screen still holding the slider that set it. A scene load does not
undo it either - the epoch flip stops voices, not gains - so without this the
quiet would outlive the world it was set in.

## The clip asset

A clip is **fully decoded at load**, to 16-bit interleaved PCM at the rate and
channel layout the source file carried. One answer for a footstep and a music
bed, and it is this one because a streamed clip would be the only asset in the
engine that keeps a file open past its load: a `Resource` is a value a scene
load builds in a staging `ResourceManager` and swaps in whole, and a live
decoder reading a file is not that. Playing then costs no decode, which is what
the footstep played fifty times a minute cares about.

The cost is visible rather than hidden - the cooked file *is* the samples, so a
clip occupies as much memory as it does disk. Streaming can arrive later as a
second data source behind the same handle without touching the component, the
scene format or the clip's identity, which makes it a thing to add when a
project needs it rather than a flag to carry until one does.

`AudioClipAsset::samples` is the one asset payload in the engine held through a
`shared_ptr`. The reason is the mixer: a playing voice reads those samples from
the audio thread while a scene load frees the asset from the main thread
without asking. Sharing ownership with the voice turns that from a
use-after-free into a sound that keeps playing for the one frame it takes
`AudioSystem` to notice the graph moved.

### Import and cook

Same shape as every other asset kind:

| Stage | What happens |
|-------|--------------|
| Import | `loadAudioClip("assets/audio/step.wav")` decodes wav / mp3 / flac to s16 at the file's own rate. The project-relative path is the clip's name. |
| Recipe | `{"kind": "file", "path": "assets/audio/step.wav"}`, written to `library/sounds/<uid>.json` - by the cook, not by the import. A clip that has been imported and not yet baked exists in memory only, and nothing that reads the library can find it |
| Cook | `AssetCook::writeAudioClip` -> `cooked/sounds/<uid>.vkmc` (header + rate + channels + sample count + the PCM) |
| Reference | The scene's `assets.sounds` block lists the clip by name; `AudioSource` resolves it through `findByName` on load |
| Runtime | `loadCookedAudioClip` reads the binary synchronously - the cooked file is already the PCM the mixer wants, so there is nothing to decode off-thread |

The clip's rate is preserved rather than resampled at import: the mixer converts
at play time if the device runs at a different rate, so a project moved between
a 44.1 kHz and a 48 kHz machine does not re-cook.

## The device seam

`AudioDevice` is the only file in the engine that includes `miniaudio.h`, and
the backend is absorbed privately into `vkm_core` - so nothing that plays a
sound has any way to reach it. Swapping the backend means re-implementing that
one file, plus `src/tools/loader/audio_loaders.cpp`, which is the only other
include site in the tree: the importer needs a decoder, and it lives in
`vkm_cook`, so nothing in a runtime ever calls one.

**Linking one is a different question, and the answer is not the one the
include sites imply.** miniaudio is compiled once, as a single translation
unit, with decoding left on, and `vkm_cook` and `vkm_core` share that one
static library - so `ma_dr_wav`, `ma_dr_mp3` and `ma_dr_flac` are inside
`libvkm_core.so` and therefore inside every shipped game: 292 symbols, about
139 kB, unreachable because the only code that would call them cooks. Dead
weight rather than a leak, and `MA_NO_DECODING` would cost a second build of
the backend with a different public macro set to remove it. Written down
because "the runtime links no decoder" is the sort of thing a later decision
leans on, and it is false.

### The voice budget, and why there is no cap

A voice is one backend sound reading the clip's samples where they lie, so
fifty footsteps cost fifty cursors and no duplicate PCM. There is **no voice
cap**: one source is one voice, and the number of sources is the project's to
manage.

That is a measurement now, not a deferral. Asked for 4096 voices at once the
device starts 4096 - nothing refuses at any count - and each costs about
3.2 kB. Mix cost is linear and small: on an AMD FX-8320, a 2012 eight-core
part, 64 voices take 5 to 6% of one core, 128 take 10 to 13%, and 1024 take 83
to 110% across six runs, the spread being how much of it is spatialized and
what else the machine is doing. The mixer's deadline is real time, so that last
figure is the deadline itself: a thousand voices is roughly where a device
callback starts to miss on that machine rather than comfortably short of it -
still an order of magnitude beyond the runaway below and further still past
anything either example plays. Measure it on a quiet machine: one stray process
holding five cores moved every figure here by half again.

**What breaks first is the sum, not the mixer.** Four phase-locked copies of
one clip at gain 0.5 already pass full scale. So the failure a cap could
prevent arrives an order of magnitude after the failure it cannot: a cap placed
where the CPU wants one would never fire in a game, and a cap placed where the
signal breaks would be four. Coincidence is what clips, and a cap does not stop
two sounds landing on the same instant. It is the wrong tool rather than a
missing one, and stealing a voice to make room would cost the guarantee the
system does make - that every source is heard.

Coincidence is also rarer than the worst case reads. The same clip started a
millisecond apart peaks at 0.23 across 128 copies, and 256 spatial sources
scattered 5 to 45 m around the listener peak at 0.37, because phase and
attenuation both work against the sum. Gain staging is the game's job; the
arithmetic it needs is this.

The fire-and-forget path is bounded by the clip rather than by a counter. Sixty
requests a second of a two-second clip - a behavior emitting one every frame,
which is the runaway a cap exists to catch - settle at exactly 120 voices, the
clip's length times the rate they are asked for at, and drain to zero within
two seconds of the requests stopping. There is nothing running away.

### Stopping a sound is ramped

Releasing a sound outright cuts its waveform at whatever sample the cursor
happens to be on, and a vertical edge is a click. Measured against a 220 Hz
tone at gain 0.8, the worst cut across one cycle is a step of **0.63** - the
signal's full amplitude - where the waveform's own steepest sample-to-sample
step is 0.018, thirty-five times smaller. Where the cut lands is luck: the same
stop a millisecond earlier can fall near a zero crossing and sound clean, which
is exactly why one measurement of it proves nothing.

So `stopVoice` schedules a five-millisecond ramp to silence instead of
releasing on the spot, and `reapFinishedVoices` takes the voice once the ramp
has run. Measured again over sixty phases: the worst step across a stop is
0.014 against the waveform's own 0.014, so the cut is no longer distinguishable
from the signal it cuts.

This is **not** a `fadeOut` field, deliberately. Nothing authors it, nothing
serializes it, and a fade someone *wants* is still a per-frame `volume` ramp
the caller writes - `volume` is pushed to the mixer every frame, so a ramp
already is a fade. What the five milliseconds cover is the stops with no caller
left to write one: a source's entity destroyed, its component removed, the
editor's audition stopped, and every `playing = false` that did not think of it.

Above `AudioDevice` nothing changes. A ramping id answers exactly as a released
one did - unknown to `updateVoice`, finished to `isVoiceActive` - so there is
no second state for `AudioSystem` or the editor to know about. `voiceCount()`
does count a ramping voice, because that is what the mixer is holding. A source
restarted on the next frame overlaps the tail of the one it replaced by under a
third of a frame, which is the crossfade it sounds like rather than a fault.

The five milliseconds are five milliseconds of *audio*. The object behind the
voice is freed later than that on real hardware, because `reapFinishedVoices`
asks the mixer's own clock and that clock advances one device period at a time,
running ahead of what is audible. Measured against PulseAudio here: the voice
is released 19 to 74 ms after the stop, and a source started and stopped on
every frame at 60 Hz holds at most five voices at once rather than one. None of
them is heard and none of them leaks - `stopAllVoices` and `close()` still free
on the spot - but it is why `voiceCount()` reads higher than the number of
audible sounds while things are stopping.

The editor's transport pause borrows the ramp for the same reason, in both
directions - see [Two pauses wearing one
word](#two-pauses-wearing-one-word) - and a held voice is the one thing
`reapFinishedVoices` must not mistake for a finished one.

`stopAllVoices` is the one exception and stays a hard cut. It is the teardown
path: `close()` uninitialises the mixer on the very next line, so a ramp
scheduled there would never be mixed at all, and the scene load that calls it
has already replaced the world those sounds belonged to.

Nothing in the ramp is a new threading hazard. miniaudio schedules the stop
through `ma_atomic_exchange_64` on the node's state time and `ma_atomic_*` on
the fade settings - it defers the fade to the audio thread by design - and a
ThreadSanitizer run that starts, updates and stops four thousand voices under a
live mixer reports only the listener-flag race named below.

### Which thread may call the device

All of `AudioDevice`, `render()` included, is **main-thread only**, and nothing
in it is guarded. It does not need to be: every engine call arrives from
`AudioSystem::update` or from an editor panel, both on the main thread, while
the mixer thread belongs to miniaudio and reaches back only through the log
bridge. A second thread calling `render()` while the main one calls `close()`
reads a graph being torn down - a segfault, not a wrong sample - so a harness
that lends the offline mixer a thread of its own must join it before closing
the device. A lock here would put a mutex in the frame's hot path to serve a
caller the engine does not have.

### The known races are miniaudio's

ThreadSanitizer reports the same two data races on every run that mixes while
the main thread pushes voice parameters, and both are inside the backend. A
third joins them only while something reads a playback cursor, which is the
Inspector's audition card and nothing else:

| Field | Written from | Read from |
|---|---|---|
| `ma_gainer::masterVolume` (plain `float`) | `AudioDevice::Backend::apply` -> `ma_sound_set_volume`, every frame per voice | `ma_gainer_process_pcm_frames_internal`, on the mixer thread |
| `ma_spatializer_listener::isEnabled` (plain `ma_bool32`) | `AudioDevice::setListenerActive`, every frame | `ma_spatializer_listener_is_enabled`, on the mixer thread |
| `ma_audio_buffer_ref::cursor` (plain `ma_uint64`) | the mixer thread, advancing as it reads | `AudioDevice::voiceCursor`, while an audition card is on screen |

Seeking is **not** one of them. `ma_sound_seek_to_pcm_frame` hands the target
to the mixer through an atomic and lets the audio thread perform the seek
before its next read - it is written for exactly this call from exactly this
thread - which is why `seekVoice` is safe and why a scrubbed position reads
back immediately rather than a buffer later.

Measured rather than reasoned: a run that starts, updates, holds, scrubs, stops
and sweeps voices against a live PulseAudio device reports those and nothing
else, with the cursor race named on both sides - `AudioDevice::voiceCursor`
reading it on the main thread and `ma_audio_buffer_ref_read_pcm_frames`
advancing it on the mixer's. Holding a
voice and scrubbing one add no race of their own, and no engine-owned state
races in any of it: the voice table, the clip's samples and every `AudioSource`
field are touched from the main thread alone.

AddressSanitizer over the same run is clean.

Every racing field is a single aligned scalar with no invariant spanning it,
and miniaudio uses `ma_atomic_float` for exactly this kind of field elsewhere -
`ma_engine_node::volume` and `ma_device::masterVolumeFactor` are both atomic -
so they read as upstream oversights rather than a design. They are left alone
on purpose: patching them means carrying a fork of the backend, and quieting
one of them from this side (the listener flag has a change signal the system
already computes; per-voice volume does not) would hide the rest. What the
cursor costs if it is ever read torn or stale is a slider a pixel behind for
one frame, which is why it is read by a scrubber and by nothing the engine
believes. This is the cost the version accepted when it chose a vendored
single-header backend, written down rather than discovered later.

### No device is a normal state

A headless cooker, a CI box, a machine whose driver is broken or whose session
has no audio permission: `open()` returns false, `isOpen()` stays false, and
every other call becomes a no-op costing one branch. The engine runs
**silently, not broken**, and says so once at `WARNING` rather than once a
frame.

`AudioSystem::update` still runs its whole reconcile on such a host, and that
is deliberate. Skipping it would leave `AudioSource::playing` stuck true
forever, because nothing would ever be there to report the sound finished - and
`playing` is the one field whose contract is "read it back to learn a one-shot
ended". Running anyway makes a silent host behave like a host where every sound
is zero-length: `play()` answers no voice, the request clears in the same
frame, `playOnStart` is honoured once, and gameplay that waits on a sound moves
on. The walk costs a `forEach` over however many audio components a scene
holds, which is the price of the contract meaning the same thing everywhere.

The backend's null device - which mixes into nowhere and would make every host
look like it had sound - is deliberately excluded from the backend list, so
that "this host has no audio" is a state the engine can see, report and be
tested against. On a machine that does have sound, that path is reachable by
taking it away:

```bash
PULSE_SERVER=/nonexistent ALSA_CONFIG_PATH=/nonexistent ./build/bin/vkm_runtime examples/stress_arena
```

which logs the backend's own reason for each refusal, then one line saying the
engine is running silently, and plays on with no error.

### Rendering without a device

`openOffline(rate, channels)` brings up the same graph, the same spatializer and
the same voices, with the output going to a caller's buffer instead of to
hardware. It exists because the audible half of audio is not machine-checkable
and the measurable half only becomes so if something can read the signal. The
audio harness opens the mixer this way to measure that attenuation falls off,
that panning lands on the correct side and that a source is heard where its
entity is; the adversarial harness beside it uses the same mixer to measure
what happens when the inputs are wrong.

### Auditioning a clip

The Asset Browser's Sounds rail row and the Inspector's Audio Source card both play
a clip through the device directly, never through `AudioSource::playing` -
setting that flag would be an edit to the scene when all that was asked for was
to hear the file. Both pass a **flat, non-spatial** `VoiceParams`. A default
one is spatial, and a spatial voice is measured against the scene's listener:
in a project that has no `AudioListener` yet - exactly the project someone is
importing sounds into - it would be silent, and in one whose ear stands
somewhere else it would play at whatever the world origin sounds like from
there. An audition is a request to hear the file, so it is heard flat.

An audition is heard flat but it is not heard *outside the mix*: the master gain
`AudioListener::volume` sets multiplies it like every other voice, so a scene
whose ear is at zero silences the editor's preview too. That is right - there is
one mixer - and it is invisible, because the transport still shows a Pause and a
cursor running against the clip's length. So both surfaces say it where they
already say the mix cannot be heard: the Sounds tab beside `Import Sound...`,
next to its no-audio-device line, and the Audio Source card above its transport,
outside the spatial warnings because a muted mix is not a positioning mistake.
`AudioDevice::masterVolume` is what they read - the gain the mixer was given,
already sanitised - rather than each of them re-deriving it from the listener.

Both carry the same transport - play, pause / resume on one button, stop and a
position slider - and they carry it because it is one widget, the editor's
`auditionTransport`, and not two implementations of the same idea. The Sounds
tab draws it per row, on the row whose clip the voice came from; every other
row offers Play alone, so a row can never hold or cut short a sound it is not
showing. Both follow the undo rule the two animation cards set: play, stop and
scrub preview a clip and never dirty the scene, since dirtying it every time
somebody listens to something would make the unsaved-changes prompt mean
nothing. Only edits that round-trip with the scene push a command.

### The cursor is the device's, not the component's

`Animator::time` is component state, and its card's scrubber writes it
directly. That works because nothing advances it but the system that reads it,
both on the main thread. A voice's cursor is not that: the mixer advances it
between our frames, at the device's own rate, so `AudioSource` deliberately has
no field mirroring it and the card asks `AudioDevice::voiceCursor` instead.

Mirroring it would have been a synchronisation problem with no good side.
Pushing device to component each frame throws a scrub away the moment it is
made; pushing component to device re-seeks the mixer to a frame-old position
sixty times a second, which is a stutter rather than a sound. Telling the two
apart needs a dirty flag, and that flag would outlive whoever added it.

It also answers what a scrub means with nothing playing: there is no cursor, so
the slider is disabled rather than inventing a start offset that lives in the
panel and is forgotten when the selection moves. Which it does: an audition is
the sound of the clip in front of you, so selecting anything else stops it, and
the card can never show a cursor running against another entity's clip.

Scrubbing a **held** voice works and is the point of holding one - the seek is
kept and playback resumes from it - and scrubbing to the very end finishes the
voice, which is what playing to the end does.

## What a harness can prove, and what it cannot

The audio harness measures, off a real mix:

- a clip decodes to exactly the frame count, rate and channel layout it was written with
- the cook round trip is sample-for-sample lossless
- a device opens, closes and reopens cleanly - and on a host with none, reports
  itself closed while every other check still passes
- attenuation falls off with distance and reaches silence at `maxDistance`
- a source at screen-right is louder in the right channel
- a source is heard where its **entity** is, after nothing but a `Transform` write
- a non-spatial source ignores distance and a missing listener
- 33 simultaneous voices return 200 full buffers with no short read
- the limit: 4096 voices start with nothing refused, mix cost stays linear in
  the count, and a request emitted every frame settles at the clip's length
  times the rate it is asked for at rather than growing
- what N copies of one clip actually sum to, phase-locked against staggered
  against scattered across a level
- a one-shot clears its own `playing` flag and releases its voice
- a stop is ramped rather than cut: swept across sixty phases of a tone, the
  worst step across a stop stays inside the waveform's own steepest step, and
  the voice is released by the following reap rather than leaked
- `playOnStart` waits for simulation time while an explicit play does not
- a scene load stops the voices belonging to the graph it replaced, requests
  nobody tracks included
- a component's voice dies with its entity, while a request survives the entity
  that asked for it and is still reaped when it ends
- the retrigger gap: what a source drops at each cadence, and that the request
  path drops none of it
- that a held voice is silent while it is held, survives every reap that runs
  under it, and resumes from the sample the hold landed on rather than from the
  start of the clip
- that the transport's hold takes only what was sounding when it was asked for,
  so a clip auditioned under a frozen world is audible, and gives back only what
  it took, so an audition held on purpose stays held
- that a scrub reads back at once, lands in the part of the clip it names,
  carries across a hold, and finishes the voice when it reaches the end

It cannot say whether any of it **sounds right**. Nothing there hears clipping,
a resampler artefact, a click at a loop point, panning that is technically
correct and perceptually wrong, or the latency between an action and the sound
it makes. Those need a human with headphones, and no amount of green output is
a substitute.

### Hearing it

The quickest way is to run one: `./build/bin/vkm_runtime examples/potion_runner`
plays a footstep on every marker its stride clip announces, and the whole chain
- clip, marker, event, request, listener - is built in code with no asset file
anywhere (see [Animation events](animation.md#animation-events)). Coins ring as
they are collected, from the same path. What to listen for there is that the
footsteps stay in step as the run accelerates, stop in mid-air, come back on
landing, and no longer sound like one sample on a loop: gain and pitch are
spread a little per playback at the emit site, which is where a variation
belongs when it is the playback that varies rather than the sound.

To hear a clip of your own, put a wav, mp3 or flac anywhere under a project's
`assets/`, then in the editor:

1. Bottom panel > Assets > Sounds > `Import Sound...`, pick it, press play.
   That is the decoder and the device, with no scene involved.
2. Create > Audio Listener, then Create > Audio Source; assign the clip to the
   source, tick Loop, press Play on the transport.
3. Drag the source around the listener with the gizmo. It should cross the
   stereo field, hold full volume until it leaves the inner sphere, and be
   inaudible once it passes the outer one. Use a **mono** clip for this: a
   stereo one holds still in the field however far you drag it, for the reason
   in [A positioned source wants a mono
   clip](#a-positioned-source-wants-a-mono-clip), and the card says so.

Both are drawn: selecting a spatial source puts a solid sphere at `Min
Distance` and a faint one at `Max Distance` around it, which is the whole
attenuation model, since the falloff between them is linear and nothing aims a
sound. The source's own icon carries the rest of what is worth seeing without
opening the Inspector. It keeps the speaker's radiating arcs while `spatial` is
set and drops them when it is not, because those arcs are the only thing the
two kinds differ by: a 2D source is drawn at its `Transform` like any other,
but that pose is not heard, so the marker would otherwise promise a placement
the mixer ignores. It is dimmed while the source names no clip (a broken source
and a quiet one look identical otherwise) and ringed while `playing` is set -
which, for a one-shot shorter than a blink, is a ring you will not catch, so it
is the loops and the beds it actually reports on. The listener icon is dimmed
on every listener except the one `findActiveListener` picked, and carries a
short arrow along its `+Z` facing, which is what decides the side a source is
heard from.

What to listen for, in the order it is likely to be wrong: a click where a loop
wraps, panning that is on the wrong side, a fade that steps rather than glides,
and latency between moving the source and hearing it move. A click when a sound
*stops* should not be among them - that one is measured, and the five
milliseconds that fix it are short enough to be worth checking they do not read
as a sound going soft instead of off. A clip whose own
partials do not complete whole cycles across its length clicks at the loop point
by itself, so use a deliberately loopable one before blaming the mixer.

## Not implemented

**Read this as a register of decisions taken, not as the boundary of what is
missing.** Each entry below is something that was considered and turned down,
with the reasoning kept so the next person can disagree with it on the merits.
It is not an inventory: something absent from the engine and absent from this
list has simply never come up, and nothing here should be cited as evidence
that the rest of the subsystem is complete.

One rejection in particular is worth not repeating. An entry that reads "no
caller" is a claim about the world, and it expires: pause was once turned down
that way while the editor's own transport already had a Pause button that froze
the world and left every sound in it running. **The tool's existing UI is a
caller.** So is a shipped example. Before leaning on any "nothing asks for it"
below, go and look - preferably by opening the editor and pressing the button.

- **Streaming.** See the clip asset above; additive later, no caller today. A
  three-minute stereo bed at 44.1 kHz is 30 MiB resident, which is the number
  that would decide it. It would not cost the runtime a decoder either: the
  cooked `.vkmc` *is* raw PCM, so streaming one is a read, and miniaudio's
  decoders are in every runtime already. What keeps it out is the `Resource`
  objection - a live file handle is not a value a scene load can swap in whole
  - and the absence of anyone asking.
- **Sound groups / buses.** A master gain on the listener is what a volume
  slider needs; a music-vs-effects split wants a category enum on the source and
  a bus per value, and there is no content asking for it yet. An ordinal enum
  written into a scene file also has to be append-only from the day it lands -
  a value's position IS its serialized identity, so reordering it silently
  re-labels every row already on disk - which is a rule worth stating once,
  beside the enum, rather than after the first reorder.
- **Reverb, filters, occlusion.** Each is a node in the backend's graph and a
  new authored surface; none has a caller.
- **A voice cap and priority.** See [the voice
  budget](#the-voice-budget-and-why-there-is-no-cap) above, which measures it
  rather than assuming it: nothing refuses at 4096 voices, a thousand cost most
  of one core on a 2012 part, and a request emitted every frame settles at the
  clip's length times its rate instead of growing.
  What clips is coincidence, not count - four phase-locked copies of one clip at
  gain 0.5 pass full scale - and a cap does not stop two sounds landing on the
  same instant. Gain staging is the game's job, and a cap would not be doing it.
- **Doppler, cones, per-source directivity.** No velocity is tracked, and no
  content aims a sound.
- **Editing a clip's samples.** `samples` is `const` by type: two threads read
  it, and nothing may change it out from under a voice.
- **Downmixing a stereo file to mono, on import or anywhere else.** A positioned
  source wants a mono clip and [the engine now says so
  twice](#a-positioned-source-wants-a-mono-clip), which raises the obvious
  question of why it does not just fix the file. Not
  because the bytes are precious - `loadAudioClip` opens the wav read-only and
  the cook writes a separate `.vkmc`, so nothing on disk is ever overwritten -
  but because **import is the moment of least information**. Nobody has said yet
  whether the clip is going on an emitter: `loadAudioClip` hands back the
  existing handle for a path already imported, so one `AudioClipAsset` serves
  every reference to that file, and the same recording is correct as stereo on a
  music bed and wrong on a footstep. The mistake being warned about is a
  *pairing*, and a pairing cannot be fixed from one half of it.

  It would also be one-way and invisible in the tool. The asset records only
  `channels`, so "this file is mono" and "we made it mono" read identically in
  the Asset Browser, and re-importing would downmix again - the author's width
  would be gone from everything downstream with nothing on screen admitting it.

  If it is ever wanted, the **cook** is the right home, not the importer: a cook
  is a build product, the wav stays the source of truth, and deleting `cooked/`
  rebuilds. The cost is what stops it today rather than the principle. Somebody
  still has to say *which* clips want it, which means an authored per-clip flag,
  which means a new field in the recipe - and a recipe is hashed by
  `hashRecipe` and written into `cooked/`, so it freezes on merge. And even
  there it cannot be decided from the clip alone, because one clip can sit on a
  spatial source and a flat one in the same scene. Warning costs nothing, is
  reversible, and points at the half that is actually wrong.

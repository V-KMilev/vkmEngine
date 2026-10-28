# Audio System

Sound as ECS. An emitter is an entity with an `AudioSource` naming an
`AudioClip` asset; the ear is an entity with an `AudioListener`. Each frame
`AudioSystem` reconciles those components against a mixer, and one class,
`AudioDevice`, is the engine's whole surface onto the backend (miniaudio).

The split to keep: **the engine owns everything with a name, the backend owns
everything with a thread.** Which clips exist, what wants to be heard and where
the ear is are decided on the main thread by code that never sees miniaudio;
mixing, resampling, spatialization and the output device live behind
`AudioDevice`.

## The component model

| Component | Notes |
|-----------|-------|
| `AudioSource` | One entity, at most one voice. |
| `AudioListener` | The pose spatial sources are heard from, plus the master gain. |

`playing` and `started` are runtime state, never serialized; `playOnStart` is
the authored half.

### Starting and stopping a sound from gameplay

There is no `play()` call. `playing` is the state the source *wants*: a behavior
writes it to start or stop, and reads it back to learn a one-shot finished.

```cpp
get<AudioSource>().playing = true;                        // start

const AudioSource* door = scene().tryGet<AudioSource>(m_door);
if (door && !door->playing) openTheDoor();                // it ended
```

A source is one speaker, not a queue: asking it to play while it plays does
nothing. Clearing `playing` ramps the voice out briefly so it does not click;
a deliberate fade is a `volume` ramp gameplay writes.

### Starting a sound with no entity

```cpp
playSoundAt(footstep, scene().get<Transform>(m_player).position, 0.55f);   // in a behavior
events().emit(PlaySoundEvent::at(handle, position, 0.55f));                // anywhere else
```

Use a request when nothing is making the sound, or one speaker is not enough - a
footstep that retriggers faster than its clip, the sound of a thing being
destroyed. A behavior calls `playSoundAt`, or `playSound` from its own entity: it
resolves the clip's `AssetRef` and plays nothing in a replayed tick. Code that is
not a behavior, or needs the rest of `VoiceParams`, emits the `PlaySoundEvent`
itself with a clip handle and sets `params` on it. A request returns no id, so
nothing can stop or move it: it plays to its end where it was made, and
`params.loop` is ignored (a sound with no end needs a component). Requests start
at the end of `AudioSystem::update`, the same frame.
`stopEverything` silences them too, pending ones included.

## Time, pause and the editor

Audio is a service: `AudioSystem` runs every frame and the device's thread
pumps the mixer, so a game's pause does **not** cut music or a menu click (as
with [`onRealtimeUpdate`](scripting.md#time-and-pause)). The one thing pause
holds back is `playOnStart`, which waits for simulation time to advance - so a
scene open in the editor before Play is quiet.

### Two pauses wearing one word

The **editor's transport** pause freezes the world to look at it, and is a
different thing. `AudioSystem` knows nothing of it: `PlaybackBar` calls
`AudioDevice::pauseAllVoices()` and `resumeAllVoices()`, so a shipped game never
inherits the editor's rule. A held voice resumes where it stopped. Only what was
sounding at the press is held and released, so a clip auditioned while frozen is
audible, and an audition paused on purpose stays paused.

Stop restores the play snapshot, a new world; every voice stops on the next
frame (see [Per-frame flow](#per-frame-flow)).

## Spatialization

A spatial source fades from full volume at `minDistance` to silence at
`maxDistance` by its `rolloff`: **`Linear`** (default, miniaudio's curve) or
**`Inverse`** (1/distance bent to reach zero at `maxDistance`,
`inverseRolloffGain`, applied by the device itself). `spatial = false` mixes the
clip flat - music, narration, UI.

Every gain is **linear**: 0.5 is half the amplitude. A player-facing volume
slider is perceptual: the game converts its position (squared, or a decibel
curve) before writing `AudioListener::volume`. A negative or non-finite gain is
silence.

### A positioned source wants a mono clip

Each of a voice's channels goes to the output channel it was authored for;
nothing crosses. A mono clip pans as the emitter passes the listener; a stereo
clip only leans, each channel turned down, so what is in its right channel is
never heard from the left however the emitter moves, which is easy to miss. The
engine warns - on the Inspector's card, and once per clip per world when a
positioned voice starts - and does not downmix
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

A spatial source with no `Transform` is heard at the world origin; the card
names it.

### Two cameras, one ear

The listener is its own component, not the camera: a third-person game hears
from the character while looking from an orbit rig. With several listeners, the
`active` one with a `Transform` in the lowest entity slot wins
(`findActiveListener`, as `findActiveCamera` picks the eye). With **none**,
spatial sources go silent, flat ones play on, and the master gain returns to
unity - it belongs to the ear, and a deleted listener must not leave the music
at half volume.

## Acoustics

The world's colliders shape what the ear hears: a sound behind a wall is quieter
and duller, one through a doorway is heard straight, and a voice in a hall rings
in it. The colliders are the acoustic world - there is no acoustic geometry to
author - and only static and kinematic bodies count: a crate rolling past is not
a wall. A scene has it on by default (`Environment::acoustics`); a positioned
source or request can opt out (`AudioSource::acoustics`,
`PlaySoundEvent::acoustics`), for a sound that must cut through; a flat source
is never shaped.

**Occlusion.** Rays run from the ear to each shaped voice, one at its middle and
four around it across the line of sight - pulled back off a floor or wall the
sound sits by, which would otherwise muffle a sound in plain view - each passing
through the body the sound sits in (the nearest the rays strike at or above its
entity, a request's `from`) and stopping a little short, so neither that body
nor a surface the sound is pressed against blocks it
(`Acoustics::castOcclusionRays`, `blockedShare`). The share blocked sets the
voice's gain and a low-pass, eased so a door opening is heard opening: a sound
past a corner is partly muffled, one through an open door not at all. A voice
starts as occluded as it is, and a `PlaySoundEvent` is shaped once, where it is
made.

**The room.** Rays leave the ear spread evenly over the sphere, a few a frame,
each followed off what it strikes for a couple of bounces
(`Acoustics::traceRoomRay`). The share of their legs that struck says how shut
in the ear is; their mean length, how big the space is. Eyring's reverberation
time turns the size into a decay, and the enclosure scales it and the level
(`Acoustics::reverbFor`): open ground has no reverb, a small room a short one, a
hall a long one. The rays are a rolling window, so the reverb follows the ear
from room to room. `AcousticsSettings::reverb` scales its level; 0 keeps the
occlusion alone.

**Seeing it.** `DebugCategory::Acoustics` (the editor's **View > Show >
Acoustics**) draws the rays (`AudioSystem::drawAcoustics`): the room's window as
it was cast, out from the ear, pulsing along each leg, dimmer at each bounce and
with a dot where it struck; and each shaped source's rays from the ear, cast
again for the drawing, whole where nothing stands between and red up to what
stops one ([rendering.md](rendering.md#debug-drawing)).

**One reverb, the ear's room.** Every shaped voice feeds one tank
(`ReverbTank`), tuned to the room the ear is in: a sound in the next room is
heard muffled through the wall but rings as if it were here. A tank per room
would need rooms, which no scene has.

Not modelled: materials (every surface absorbs alike), sound bending round a
corner beyond what the rays around a source see, and a lone wall's distinct
echo.

## The clip asset

A clip is **fully decoded at load** to 16-bit interleaved PCM at the file's own
rate and channels; there is no streaming, so a scene load can build every clip
in a staging `ResourceManager` and swap it in whole.

`AudioClipAsset::samples` (`ClipSamples`) is the one asset payload with shared
ownership: a voice reads it on the audio thread while a scene load may free the
asset, so the voice keeps it alive. Its filling constructor is defined only in
vkm_core, because a shared buffer's control block carries the code that frees
it: a clip a gameplay module makes is never freed by code a hot reload unmaps
([Scripting](scripting.md#unloading-a-module)).

`loadAudioClip` imports wav, mp3 and flac; the cook writes the clip
(`AssetCook::writeAudioClip`, [io.md](io.md#assets-recipes-and-the-cooked-cache)),
read back by `loadCookedAudioClip`; a scene lists clips by name in
`assets.sounds`. A looping voice returns to the clip's **`loopStart`**, so what
precedes it plays once as an intro; import reads it from a wav's `smpl` loop or
its earliest cue point.

## Not built

No streaming, no buses or groups, no Doppler or cones.

---

## How it works inside

### Per-frame flow

`AudioSystem` runs in the **Transform stage**, after `HierarchySystem` resolves
world poses (so a parented source is heard where this frame put it) and after
Simulation (so a sound a script, animation or contact asked for starts that
frame). A world replaced stops every voice. A scene load - the `ResourceManager`
epoch moved - also drops every request not yet started, as an old graph's handle
can name another clip in the new one. The `Scene` epoch moving alone (the
editor's Stop and restart) drops only the requests that reached `AudioSystem` in
the old world: one emitted on the way out, as `playSound` emits, goes with it,
while one queued arrives in the next world and plays there. The rest re-measures
the room (`AudioSystem::updateAcoustics`), then reconciles each `AudioSource`
with its voice (`AudioSystem::update`).

### What the acoustics cost

The rays are cast on the main thread, in `AudioSystem::update` - none on a host
with no sound device, such as a server - against the colliders gathered once a
frame around the ear, its playing sounds and those asked for (`QueryParts`). A
frame casts a fixed number of room rays, the occlusion rays of the voices whose
turn it is - from `OCCLUSION_VOICES_PER_FRAME` on, each is re-asked every few
frames, by its entity slot - and those of each sound that starts. On the mixer
thread a voice costs its low-pass, retuned only when its cutoff moves a step,
and the tank runs once for all of them.

### Each voice's chain

A voice runs through its low-pass to the output or, when it rings in the room
(`VoiceParams::reverb`), through the reverb node, which passes it on with the
room's tail added - through it rather than beside it, for the reason
`ReverbNode` gives. The reverb node runs whether or not anything feeds it, so a
tail rings out after its sound ends - until `stopAllVoices`, which fades it out
with the voices. Its decay and level cross to the mixer thread as atomics
(`AudioDevice::setRoomReverb`).

### The device seam

Only `AudioDevice` (`audio_device.cpp`) and the import decoder
(`src/tools/import/audio_loaders.cpp`) include `miniaudio.h`.

#### Which thread may call the device

All of `AudioDevice`, `render()` included, is **main-thread only** and
unguarded: engine calls come from `AudioSystem::update` or an editor panel, and
the mixer thread is miniaudio's. A harness lending the offline mixer its own
thread must join it before closing the device.

#### The known races are miniaudio's

ThreadSanitizer reports races inside the backend on single aligned scalars (a
voice's volume written as the mixer reads it, a low-pass retuned as it runs).
They are left alone: fixing them means forking the backend, and no engine-owned
state races.

#### The voice budget

`MAX_ACTIVE_VOICES` caps live voices. At it, `play()` stops a voice the new
sound outranks: one of a lower `priority`, or of its own and quieter than it as
heard - the lowest priority first, the quietest of those, then the oldest - and
plays the new one; with no such voice it refuses. A voice a pause holds is kept
for its resume. So music raised above the rest is never cut, a gunshot takes a
distant footstep's place, and a sound no louder than the voices of its own
priority cuts none of them. It logs once per saturation, and it does not prevent
clipping: gain staging is the game's. A spatial `PlaySoundEvent` beyond
its own `maxDistance` is dropped rather than hold a voice it could never make
audible; an `AudioSource` that starts out of range still starts, because it can
move.

#### Stopping is ramped

`stopVoice` schedules a short ramp to silence and `reapFinishedVoices` takes the
voice after, which covers stops with no caller to fade - an entity destroyed, a
component removed. Above `AudioDevice` a ramping voice already reads as gone;
only `voiceCount()` counts it. `stopAllVoices` (a scene load, Stop) ramps every
voice the same way; only `close()` cuts.

#### No device is a normal state

With no audio, `open()` returns false, every other call is a no-op, and the
engine runs **silent, not broken**, saying so once. Every sound reads as
zero-length, so `playing` clears and gameplay waiting on a sound moves on. To
reach that path on a machine with sound:

```bash
PULSE_SERVER=/nonexistent ALSA_CONFIG_PATH=/nonexistent ./build/bin/vkm_runtime examples/stress_arena
```

A device lost mid-game (a headset unplugged) is reopened by
`reapFinishedVoices`, retried every `REOPEN_INTERVAL`, and playback carries on.
A one-shot asked for while it is lost is not played: its moment has passed.

`openOffline(rate, channels)` renders the same graph into a caller's buffer;
`tests/audio/audio_tests.cpp` measures attenuation, panning, gain, occlusion and
reverb through it.

#### Auditioning a clip

The Asset Browser's Sounds tiles and the Audio Source card play clips through
the device directly, never through `AudioSource::playing` (that would edit the
scene), with **flat** `VoiceParams` so a project with no listener still hears
them. The master gain still applies, so both say when it is zero.

#### The cursor is the device's, not the component's

The mixer advances a voice's cursor between frames, so `AudioSource` has no
mirror of it; the card asks `AudioDevice::voiceCursor`. A mirror would either
drop a scrub every frame or re-seek the mixer to a stale position.

### Key files

- `src/engine/resource/asset/audio_clip_asset.h` - `AudioClipAsset`
- `src/engine/ecs/component/audio/audio_source.h` - `AudioSource`
- `src/engine/ecs/component/audio/audio_listener.{h,cpp}` - `AudioListener`, `findActiveListener`
- `src/engine/system/audio/audio_device.{h,cpp}` - `AudioDevice`, the backend seam
- `src/engine/system/audio/audio_events.h` - `PlaySoundEvent`
- `src/engine/system/audio/audio_system.{h,cpp}` - `AudioSystem`
- `src/engine/system/audio/acoustics.{h,cpp}` - occlusion and the room's estimate, over any ray cast
- `src/engine/system/audio/reverb.{h,cpp}` - `ReverbTank`
- `src/tools/import/audio_loaders.{h,cpp}` - `loadAudioClip`
- `src/engine/io/asset/asset_cook.{h,cpp}` - the cooked `.vkmc` sound format
- `src/engine/io/asset/cooked_loader.{h,cpp}` - `loadCookedAudioClip`
- `modules/miniaudio` - the backend, absorbed privately into `vkm_core`

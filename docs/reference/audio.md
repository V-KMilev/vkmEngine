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

## The component model

| Component | Holds | Notes |
|-----------|-------|-------|
| `AudioSource` | clip handle, volume, pitch, loop, spatial, playOnStart, min/max distance | One entity, at most one voice. `playing` is the state the source *wants* to be in - gameplay writes it to start or stop, and reads it back to learn a one-shot finished. |
| `AudioListener` | active, volume | The pose every spatial source is heard relative to, plus the master gain. |

`playing` and `started` on `AudioSource` are runtime state: not reflected, not
serialized. A scene row holding a half-finished sound would resume a noise whose
beginning nobody heard. `playOnStart` is the authored half of the same idea.

### Starting and stopping a sound from gameplay

There is no `play()` call. A behavior writes the field:

```cpp
void onTriggerEnter(EntityId other) override {
    get<AudioSource>().playing = true;
}
```

and reads it back to find out when the sound ended:

```cpp
void onUpdate(float dt) override {
    const AudioSource* door = scene().tryGet<AudioSource>(m_door);
    if (door && !door->playing) openTheDoor();
}
```

One source is one speaker, not a queue: asking a source to play while it
already is does nothing.

Clearing `playing` ramps the voice to silence over a few milliseconds rather
than cutting it ([Stopping a sound is ramped](#stopping-a-sound-is-ramped)). A
deliberate fade is a per-frame `volume` ramp gameplay writes itself.

### Starting a sound with no entity

```cpp
events().emit(PlaySoundEvent::at(m_footstep, scene().get<Transform>(m_player).position, 0.55f));
```

`PlaySoundEvent::at` fills a position and a volume and leaves every other
`VoiceParams` field at its default; set `params` on the event for the rest. Use
it when nothing is making the sound, or when one speaker is not enough: a
footstep at a run's top cadence retriggers faster than its clip ends, a run of
coins chimes over itself, and the sound of a thing being destroyed has no
entity left to play it.

A request hands back nothing - no id, so nothing can stop it, move it or ask
whether it finished, and nothing has to own it. It plays to its end and the
next frame's `reapFinishedVoices()` releases it. Its position is fixed where it
was made, and `params.loop` is ignored: a sound with no end needs an id to stop
it, so it needs a component. Requests start at the end of
`AudioSystem::update`, so one made during Simulation is heard on the same frame.

`stopEverything` silences requests too, including any still waiting to start:
a scene load swaps the asset graph, and a handle from the old graph may name a
different clip in the new one. At runtime the load happens in Simulation and
the next frame's requests go through; only a load in the Editor stage can drop
a good one.

## Time, pause and the editor

`AudioSystem` steps no time of its own - the device's thread pumps the mixer -
and reads the Clock only to tell a running simulation from a paused one. Audio
is a service, so it runs every frame regardless, as gameplay's
[`onRealtimeUpdate`](scripting.md#time-and-pause) does: a game's pause does
**not** cut the music or silence a menu click. The one thing pause holds back
is `playOnStart`, which waits for simulation time to advance - which keeps an
unplayed scene open in the editor quiet.

### Two pauses wearing one word

The **editor's transport** pause is a different pause: the world is frozen to
be looked at, and ambience playing on under it would be noise. `AudioSystem`
knows nothing about it; the editor holds the voices itself:

- `PlaybackBar`'s Pause calls `AudioDevice::pauseAllVoices()` and Resume
  `resumeAllVoices()`, so a shipped game never inherits the editor's rule.
- A held voice keeps its sound and stops its clock, so Resume continues from
  the sample the pause landed on. The hold is ramped like a stop.
- Only what was sounding at the press is held, and only what that press held is
  let go: a clip auditioned while frozen is audible, and an audition paused on
  purpose stays paused when the world resumes.
- A voice's `hold` names who holds it - nobody, its caller, or the transport -
  so `reapFinishedVoices` can tell a held voice from a finished one and
  `stopVoice` on a held voice sweeps it rather than resurrecting it.

Pressing Stop restores the play snapshot, which replaces the asset graph; the
epoch check in [Per-frame flow](#per-frame-flow) stops every voice on the next
frame.

## Spatialization

A spatial source is attenuated **linearly** between `minDistance` (full volume)
and `maxDistance` (silent), so both authored numbers mean what they say; the
inverse-square default of most backends never reaches zero.

Every gain - `AudioSource::volume`, `AudioListener::volume`, a
`PlaySoundEvent`'s - is a **linear gain**: 0.5 is half the amplitude, roughly
two thirds as loud. A player-facing slider is perceptual, so the settings menu
converts (`position * position`, or a decibel curve) before writing
`AudioListener::volume`. A gain that is negative or not finite is heard as
**silence**: the device floors it, because one infinite gain would take the
whole mix non-finite.

`spatial = false` bypasses all of it and mixes the clip flat - what music,
narration and UI clicks want.

### A positioned source wants a mono clip

The mixer routes each of a voice's channels to the output channel it was
authored for and attenuates it there; nothing crosses. A mono clip swings
across the stereo pair as its emitter passes the listener; a stereo clip never
moves sideways - a sound only in its left channel is never heard on the right -
while still getting nearer and further, which keeps the failure quiet. A stereo
clip whose two channels are identical behaves as mono, so testing with a
centred recording proves nothing.

The engine warns rather than converts: the Inspector's card says it where the
mistake is made, and `AudioSystem` says it once per clip per world when a
positioned voice starts, for a project that plays through `PlaySoundEvent`
alone. Nothing downmixes the file
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

A spatial source with no `Transform` is heard at the world origin, which the
inspector names on the card.

Handedness needs no correction. The engine's forward is `-Z` and up is `+Y`
(`core/math/axes.h`), and the backend's right vector is `cross(forward, up)`,
`+X` - so a source at world `+X` is heard from the right speaker, which the
`audio` suite measures off the mix.

### Two cameras, one ear

The listener is its own component, not the camera: a third-person game hears
from its character while looking from an orbit rig. With two listeners, the
enabled one in the lowest entity slot wins - `findActiveListener`, the same rule
`findActiveCamera` gives the eye.

With **no** active listener, spatial sources go silent and flat ones play on.
The engine says so once per world, and only when a positioned sound tries to
start; `stopEverything` clears the flag with the voices. The master gain also
returns to **unity**, because it belongs to the ear: a deleted or unticked
listener at half volume would otherwise leave the music at half volume with
nothing on screen holding the slider that set it.

## The clip asset

A clip is **fully decoded at load**, to 16-bit interleaved PCM at the rate and
channel layout the source file carried, so playing costs no decode. A streamed
clip would hold a live file handle, which is not a value a scene load can build
in a staging `ResourceManager` and swap in whole. The cooked file *is* the
samples, so a clip occupies as much memory as it does disk.

`AudioClipAsset::samples` is the one asset payload whose ownership is shared: a
playing voice reads it from the audio thread while a scene load may free the
asset on the main thread, so the voice keeps it alive until `AudioSystem` notices
the graph moved. The field is a `ClipSamples`, built by a constructor defined in
vkm_core alone: a clip made in a gameplay module must not be freed by the
module's code ([Scripting](scripting.md), on what may not outlive a reload).

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
at play time, so a project moved between a 44.1 kHz and a 48 kHz machine does
not re-cook.

## Not implemented

- **Streaming.** Every clip is resident: a three-minute stereo bed at 44.1 kHz
  is 30 MiB.
- **Sound groups / buses.** A master gain on the listener is what a volume
  slider needs; no content asks for a music-vs-effects split.
- **Reverb, filters, occlusion.** None has a caller.
- **Voice priority or stealing.** Past the cap the new sound is refused
  ([the voice budget](#the-voice-budget)).
- **Doppler, cones, per-source directivity.** No velocity is tracked, and no
  content aims a sound.
- **Editing a clip's samples.** `samples` is `const` by type: two threads read
  it.
- **Downmixing a stereo file to mono.** See
  [A positioned source wants a mono clip](#a-positioned-source-wants-a-mono-clip).

---

## How it works inside

### Per-frame flow

`AudioSystem` runs in the **Transform stage**, after `HierarchySystem` has
resolved the frame's world poses, so a parented source or listener is heard
where this frame put it. Transform runs after Simulation, so a sound a script,
an animation or a contact asked for starts on that same frame.

```
AudioSystem::update(FrameContext)
  |-- ResourceManager epoch moved? stop every voice - they belong to a world
  |     that no longer exists (scene load, editor Stop)
  |-- reap every voice that played to its end - one-shots, editor auditions,
  |     and stops that have finished ramping out, but never one the editor's
  |     transport is holding
  |-- findActiveListener: lowest-slot entity with an enabled AudioListener
  |     and a Transform
  |     |-- found: push its world pose + master gain
  |     `-- none:  disable the ear - spatial voices go silent, 2D ones play on
  |                at unity, the departed listener's gain going with it
  |-- for each AudioSource, posed by its Transform if it has one:
  |     playOnStart and simulation time has run? -> playing = true, once
  |     voice of another clip  -> stop it; the clip was changed while playing,
  |                                so the row below starts the new one
  |     playing and no voice   -> start one from the clip (a closed device
  |                                yields no voice, so `playing` clears here)
  |     playing and voice ended-> playing = false, release the voice
  |     playing and voice live -> push volume / pitch / loop / position
  |     not playing and voice  -> stop it
  |-- any voice whose source was not visited (component or entity gone) is stopped
  `-- start every PlaySoundEvent collected since the last frame: one voice
        each, tracked by nobody, released by the next frame's reap once it ends
```

### The device seam

`AudioDevice` is the only engine file that includes `miniaudio.h`, and the
backend is absorbed privately into `vkm_core`. The only other include site is
`src/tools/import/audio_loaders.cpp`, the cook's decoder. miniaudio is compiled
once with decoding on, so its wav / mp3 / flac decoders (about 139 kB) are
linked into every shipped game though only the cook calls them;
`MA_NO_DECODING` would remove them at the cost of a second build of the backend.

#### The voice budget

A voice reads its clip's samples where they lie, so fifty footsteps cost fifty
cursors and no duplicate PCM. `MAX_ACTIVE_VOICES` is **128**, and `play()`
refuses past it rather than stealing - the oldest voice is as likely to be the
music as a footstep - and says so once per saturation.

The cap is a runaway guard, typically against a loop firing one
`PlaySoundEvent` a frame, not where the mixer breaks. Nor does it prevent
clipping: four phase-locked copies of one clip at gain 0.5 pass full scale, and
gain staging is the game's job.

A request is bounded by its clip before the counter: sixty requests a second of
a two-second clip settle at 120 voices. A spatial `PlaySoundEvent` further from
the ear than its own `maxDistance` is dropped before `AudioDevice::play`, since
it could never become audible and would hold a voice for its whole length; a
range at or below `minDistance` does not attenuate, so such a request plays. An
`AudioSource` that starts inaudible still starts, because it can move.

#### Stopping a sound is ramped

Releasing a voice outright cuts the waveform wherever the cursor is, and a
vertical edge is a click. So `stopVoice` schedules a five-millisecond ramp to
silence and `reapFinishedVoices` takes the voice once it has run.

It is not a `fadeOut` field: it covers the stops with no caller left to write a
fade - an entity destroyed, a component removed, an audition stopped. Above
`AudioDevice` a ramping voice answers exactly as a released one does (unknown to
`updateVoice`, finished to `isVoiceActive`); only `voiceCount()` counts it. The
mixer's clock advances a device period at a time, so the voice is freed some
tens of milliseconds after the stop, not five. `stopAllVoices` is a hard cut: it
is the teardown path, and `close()` follows it.

#### Which thread may call the device

All of `AudioDevice`, `render()` included, is **main-thread only** and
unguarded: every engine call comes from `AudioSystem::update` or an editor
panel, and the mixer thread belongs to miniaudio. A harness that lends the
offline mixer a thread of its own must join it before closing the device.

#### The known races are miniaudio's

ThreadSanitizer reports the same races on every run that mixes while the main
thread pushes voice parameters, all inside the backend:

| Field | Written from | Read from |
|---|---|---|
| `ma_gainer::masterVolume` (plain `float`) | `AudioDevice::Backend::apply` -> `ma_sound_set_volume`, every frame per voice | `ma_gainer_process_pcm_frames_internal`, on the mixer thread |
| `ma_spatializer_listener::isEnabled` (plain `ma_bool32`) | `AudioDevice::setListenerActive`, every frame | `ma_spatializer_listener_is_enabled`, on the mixer thread |
| `ma_audio_buffer_ref::cursor` (plain `ma_uint64`) | the mixer thread, advancing as it reads | `AudioDevice::voiceCursor`, while an audition card is on screen |

Each is a single aligned scalar with no invariant spanning it. They are left
alone: patching them means carrying a fork of the backend. Seeking is not among
them - `ma_sound_seek_to_pcm_frame` hands the target over atomically - and no
engine-owned state races. AddressSanitizer over the same run is clean.

#### No device is a normal state

On a host with no audio, `open()` returns false and every other call is a
no-op; the engine runs **silently, not broken**, and says so once. `AudioSystem`
still runs its whole reconcile, so every sound reads as zero-length: `playing`
clears and gameplay waiting on a sound moves on. The backend's null device is
left out of the backend list so that "no audio" is a state the engine can see.
To reach that path on a machine with sound:

```bash
PULSE_SERVER=/nonexistent ALSA_CONFIG_PATH=/nonexistent ./build/bin/vkm_runtime examples/stress_arena
```

#### A device that goes away

A device can stop under a running game - a headset unplugged, a driver reset.
`reapFinishedVoices` sees a stopped device under an open mixer and opens the
default device again at the mixer's rate and layout, and what was playing
carries on from where it stopped; while none will open, another is tried every
`REOPEN_INTERVAL`. miniaudio's own WASAPI rerouting is off
(`noAutoStreamRouting`), because it restarts the device from a thread of its
own; an output that becomes the default while the current one still plays is
not followed. The `audio` suite cannot prove this path - it needs a device to
take away.

#### Rendering without a device

`openOffline(rate, channels)` brings up the same graph and voices with the
output going to a caller's buffer. The `audio` suite renders through it to
measure attenuation, panning, and what a gain that is not a number does to the
mix.

#### Auditioning a clip

The Asset Browser's Sounds tiles and the Inspector's Audio Source card play a
clip through the device directly, never through `AudioSource::playing`, which
would be an edit to the scene. Both pass a **flat** `VoiceParams`: a spatial one
would be silent in a project with no `AudioListener` yet. The master gain still
applies, so both surfaces say when `AudioDevice::masterVolume` is zero. Both
share `auditionTransport` and `auditionScrubber`; play, stop and scrub never
dirty the scene.

#### The cursor is the device's, not the component's

A voice's cursor is advanced by the mixer between frames, so `AudioSource` has
no field mirroring it and the card asks `AudioDevice::voiceCursor`. A mirror
would either throw a scrub away each frame or re-seek the mixer to a frame-old
position sixty times a second. With nothing playing the slider is disabled, and
selecting anything else stops the audition. Scrubbing a held voice keeps the
seek; scrubbing to the end finishes the voice.

### What the tests can prove

`tests/audio/audio_tests.cpp` measures, off a mix rendered with no device,
panning, attenuation, the listener and gain rules, holding, the voice budget,
the stop ramp and the far-request drop; and, over a real `Scene` with the
device closed, what `AudioSystem` does with components. The `cook` suite holds
the cooked sound format. None of it can say whether it **sounds right** -
clipping, a click at a loop point, perceptual panning, latency need a human with
headphones.

#### Hearing it

`./build/bin/vkm_runtime examples/potion_runner` plays a footstep on every
marker its stride clip announces and rings coins as they are collected, all
built in code (see [Animation events](animation.md#animation-events)). The
footsteps should stay in step as the run accelerates, stop in mid-air and come
back on landing.

To hear a clip of your own, put a wav, mp3 or flac under a project's `assets/`,
then in the editor:

1. the Assets panel > Sounds > `Import...`, pick it, press play.
2. Create > Audio Listener, then Create > Audio Source; assign the clip to the
   source, tick Loop, press Play on the transport.
3. Drag the source around the listener with the gizmo, using a **mono** clip.
   Selecting a spatial source draws a solid sphere at `Min Distance` and a faint
   one at `Max Distance`. The source icon shows arcs while spatial, dims with no
   clip and is ringed while `playing`; the listener icon is dimmed on every
   listener but the active one, with an arrow along its forward (`-Z`).

Listen for a click where a loop wraps, panning on the wrong side, a fade that
steps, and latency. A clip whose partials do not complete whole cycles across
its length clicks at the loop point by itself.

### Key files

- `src/engine/resource/asset/audio_clip_asset.h` - `AudioClipAsset` (decoded PCM + rate + channels)
- `src/engine/ecs/component/audio/audio_source.h` - `AudioSource` (clip, gain, pitch, loop, spatial, distances)
- `src/engine/ecs/component/audio/audio_listener.{h,cpp}` - `AudioListener` (the ear + master gain) and `findActiveListener`
- `src/engine/system/audio/audio_device.{h,cpp}` - `AudioDevice` (the backend seam; the only *engine* file that includes `miniaudio.h`)
- `src/engine/system/audio/audio_events.h` - `PlaySoundEvent` (the fire-and-forget request)
- `src/engine/system/audio/audio_system.{h,cpp}` - `AudioSystem` (component -> voice reconciliation)
- `src/tools/import/audio_loaders.{h,cpp}` - `loadAudioClip` (wav / mp3 / flac import)
- `src/engine/io/asset/asset_cook.{h,cpp}` - the cooked `.vkmc` sound format
- `src/engine/io/asset/cooked_loader.{h,cpp}` - `loadCookedAudioClip`
- `modules/miniaudio` - the backend, absorbed privately into `vkm_core`

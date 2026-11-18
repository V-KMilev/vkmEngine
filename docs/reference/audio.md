# Audio System

Sound as ECS. An emitter is an entity with an `AudioSource` naming an
`AudioClipAsset`; the ear is an entity with an `AudioListener`. Each frame
`AudioSystem` reconciles those components against a mixer, vkmAudio's
`Audio::Device` (`modules/vkmAudio`), the engine's whole surface onto the
backend (miniaudio).

The split to keep: **the engine owns everything with a name, the library owns
everything with a thread.** Which clips exist, what wants to be heard and where
the ear is are decided on the main thread by code that never sees miniaudio;
mixing, resampling, spatialization and the output device live in vkmAudio.

## The component model

An `AudioSource` is one speaker: one entity, at most one voice. An
`AudioListener` is the pose spatial sources are heard from, plus the master gain.
`playOnStart` is a source's authored half; `playing` and `started` are session
state, never serialized.

### Starting and stopping a sound from gameplay

There is no `play()` call. `playing` is the state the source *wants*: a behavior
writes it to start or stop, and reads it back to learn a one-shot finished.

```cpp
get<AudioSource>().playing = true;                        // start

const AudioSource* door = scene().tryGet<AudioSource>(m_door);
if (door && !door->playing) openTheDoor();                // it ended
```

A source is one speaker, not a queue: asking it to play while it plays does
nothing. Every stop ramps the voice out briefly so it does not click, whoever
caused it - `playing` cleared, the entity destroyed, the component removed, a
scene load; a deliberate fade is a `volume` ramp gameplay writes.

### Starting a sound with no entity

```cpp
playSoundAt(footstep, scene().get<Transform>(m_player).position, 0.55f);   // in a behavior
events().emit(PlaySoundEvent::at(handle, position, 0.55f));                // the request itself
```

Use a request when nothing is making the sound, or one speaker is not enough - a
footstep that retriggers faster than its clip, the sound of a thing being
destroyed. A behavior calls `playSoundAt`, or `playSound` from its own entity:
either resolves the clip's `AssetRef` and plays nothing in a replayed tick. Code
that is not a behavior, or needs the rest of `VoiceParams`, emits the
`PlaySoundEvent` itself. A request has no id, so nothing can stop or move it, and
a sound with no end needs a component.

## Time, pause and the editor

Audio is a service: `AudioSystem` runs every frame and the device's thread
pumps the mixer, so a game's pause does **not** cut music or a menu click (as
with [`onRealtimeUpdate`](scripting.md#time-and-pause)). The one thing pause
holds back is `playOnStart`, which waits for simulation time to advance - so a
scene open in the editor before Play is quiet.

### Two pauses wearing one word

The **editor's transport** pause freezes the world to look at it, and is a
different thing. `AudioSystem` knows nothing of it: `PlaybackBar` calls
`Audio::Device::pauseAllVoices()` and `resumeAllVoices()`, so a shipped game never
inherits the editor's rule. A held voice resumes where it stopped. Only what was
sounding at the press is held and released, so a clip auditioned while frozen is
audible, and an audition paused on purpose stays paused.

Stop restores the play snapshot, a new world; every voice stops on the next
frame (see [Per-frame flow](#per-frame-flow)).

## Spatialization

A spatial source fades from full volume at `minDistance` to silence at
`maxDistance` by its `rolloff`; `spatial = false` mixes the clip flat - music,
narration, UI.

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
([decisions.md](decisions.md#what-has-been-decided)).

A spatial source with no `Transform` is heard at the world origin.

### Two cameras, one ear

The listener is its own component, not the camera: a third-person game hears
from the character while looking from an orbit rig. With several listeners, the
`active` one with a `Transform` in the lowest entity slot wins
(`findActiveListener`). With **none**, the active camera hears, so a new game's
sounds play before it places an ear, and the master gain returns to unity - it
belongs to the listener, and a deleted one must not leave the music at half
volume. With no camera either, spatial sources go silent and flat ones play on.

## Acoustics

The world's colliders shape what the ear hears: a sound behind a wall is quieter
and duller, one through a doorway is heard straight, and a voice in a hall rings
in it. The colliders are the acoustic world - there is no acoustic geometry to
author - and only static and kinematic bodies count: a crate rolling past is not
a wall. A scene has it on by default (`Environment::acoustics`); a positioned
source or request can opt out (`AudioSource::acoustics`,
`PlaySoundEvent::acoustics`), for a sound that must cut through; a flat source
is never shaped.

**Occlusion.** The share of the rays from the ear to a shaped voice that the
colliders stop (`Audio::Acoustics::castOcclusionRays`) sets its gain and a
low-pass, eased so a door opening is heard opening. A voice starts as occluded as
it is, and a `PlaySoundEvent` is shaped once, where it is made.

**The room.** Rays leave the ear a few at a time and bounce; how shut in and how
big the space they find is sets a reverb (`Audio::Acoustics::reverbFor`) - none
on open ground, a short one in a small room, a long one in a hall. The rays are a
rolling window, so the reverb follows the ear from room to room;
`AcousticsSettings::reverb` scales its level.

**One reverb, the ear's room.** Every shaped voice feeds one tank
(`Audio::ReverbTank`), tuned to the room the ear is in: a sound in the next room
is heard muffled through the wall but rings as if it were here. A tank per room
would need rooms, which no scene has.

The ear listens a fixed number of times a second at any faster frame rate
(`Audio::Acoustics::LISTENS_A_SECOND`), since a room changes no faster for a
faster frame, and once a frame at a slower one, never twice; the levels ease every
frame toward what it last heard. The rays are
cast on the main thread, only while a shaped sound plays or starts - none in a
scene with no sound, and none on a host with no sound device, such as a server;
the room last heard holds meanwhile. `DebugCategory::Acoustics` draws them
([rendering.md](rendering.md#debug-drawing)).

Not modeled: materials (every surface absorbs alike), sound bending round a
corner beyond what the rays around a source see, and a lone wall's distinct
echo.

## The clip asset

A clip is **fully decoded at load** to 16-bit interleaved PCM at the file's own
rate and channels (`AudioClipAsset` says why it is not streamed).
`AudioClipAsset` is vkmAudio's `Audio::Clip` as an asset, and its samples
(`Audio::ClipSamples`) are the one asset payload with shared ownership, kept
alive by the voices playing them; that class says why, and why its filling
constructor is compiled into the library
([scripting.md](scripting.md#unloading-a-module)).

`loadAudioClip` imports what `Audio::decodeFile` reads, wav, mp3 and flac
(`Audio::DECODABLE_EXTENSIONS`); the cook writes the clip
([io.md](io.md#assets-recipes-and-the-cooked-cache)), and a scene lists clips by
name in `assets.sounds`. A looping voice loops the whole clip.

## Not built

No streaming, no buses or groups, no Doppler or cones.

---

## How it works inside

### Per-frame flow

`AudioSystem` runs in the **Transform stage**, after `HierarchySystem` resolves
world poses (so a parented source is heard where this frame put it) and after
Simulation (so a sound a script, animation or contact asked for starts that
frame). It re-measures the room (`AudioSystem::updateAcoustics`), reconciles each
`AudioSource` with its voice, then starts the frame's requests.

A world replaced stops every voice. A scene load (the `ResourceManager` epoch
moved) also drops every request not yet started, as an old graph's handle can
name another clip in the new one; the editor's Stop, which moves only the `Scene`
epoch, drops only the requests that reached `AudioSystem` in the old world.

### The device seam

The mixer is vkmAudio's: `AudioSystem` owns the one `Audio::Device`, maps each
`AudioSource` to its `Audio::VoiceParams` (the scene's `AudioRolloff` to the
library's `Audio::Rolloff` in one switch) and casts the acoustics' rays over the
physics queries. Only vkmAudio includes `miniaudio.h`. Which thread may call the
device, the races miniaudio has and how a voice is routed are on the library's
own page, `modules/vkmAudio/docs/how-it-works.md`. Every engine call comes from
`AudioSystem::update` or an editor panel, on the main thread.

#### The voice budget

`Audio::MAX_ACTIVE_VOICES` caps live voices. At it, a new sound takes the place
of a voice it outranks, or is refused (`Audio::Device::play` says which voice
gives way); a voice a pause holds is kept. So music raised above the rest is
never cut, and a gunshot takes a distant footstep's place. It logs once per
saturation, and it does not prevent clipping: gain staging is the game's. What
starts out of range is `AudioSource::maxDistance`'s.

#### No device is a normal state

With no audio, the device does not open and the engine runs **silent, not
broken**, saying so once. Every sound reads as zero-length, so `playing` clears
and gameplay waiting on a sound moves on. To reach that path on a machine with
sound:

```bash
PULSE_SERVER=/nonexistent ALSA_CONFIG_PATH=/nonexistent ./build/bin/vkm_runtime examples/stress_arena
```

A device lost mid-game (a headset unplugged) is reopened, and playback carries
on; a one-shot asked for while it is lost is not played, as its moment has
passed. `Audio::Device::openOffline` renders the same graph into a caller's
buffer; `tests/audio/mixing.cpp` measures attenuation, panning, gain and
occlusion through it.

#### The cursor is the device's, not the component's

`AudioSource` keeps no copy of a voice's cursor; the card asks
`Audio::Device::voiceCursor`, which says why.

An audition - a clip played from the Assets window or the Audio Source card -
goes through the device directly, never through `AudioSource::playing`, which
would edit the scene, and with flat `Audio::VoiceParams`, so a project with no listener
still hears it. The master gain still applies.

### Key files

- `src/engine/resource/asset/audio_clip_asset.h` - `AudioClipAsset`, vkmAudio's `Audio::Clip` as an asset
- `src/engine/ecs/component/audio/audio_source.h` - `AudioSource`
- `src/engine/ecs/component/audio/audio_listener.{h,cpp}` - `AudioListener`, `findActiveListener`
- `src/engine/system/audio/audio_events.h` - `PlaySoundEvent`
- `src/engine/system/audio/audio_system.{h,cpp}` - `AudioSystem`
- `modules/vkmAudio/src/vkm_audio/` - vkmAudio (its `README.md` maps it): `acoustics.{h,cpp}`, occlusion
  and the room's estimate over any ray cast; `reverb_tank.{h,cpp}`, `Audio::ReverbTank`; `clip.{h,cpp}`,
  `Audio::Clip`; `decode.{h,cpp}`, `Audio::decodeFile`; `device.{h,cpp}`, `Audio::Device`, the backend seam.
  Its `docs/how-it-works.md` says how the device works inside
- `src/pipeline/import/audio_import.{h,cpp}` - `loadAudioClip`
- `src/engine/io/asset/cooked/asset_cook.{h,cpp}` - the cooked `.vkmc` sound format
- `src/engine/io/asset/cooked/cooked_loader.{h,cpp}` - `loadCooked`
- `modules/vkmAudio/modules/miniaudio` - the backend, vkmAudio's own submodule

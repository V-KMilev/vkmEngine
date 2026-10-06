#include "support.h"

#include <cmath>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "system/audio/audio_device.h"
#include "system/audio/audio_events.h"
#include "system/audio/audio_system.h"

namespace {

// What a source sounds like is decided inside the mixer, by a spatializer and an
// attenuation curve the engine never sees the result of. AudioDevice::openOffline
// runs the same graph and voices into a buffer instead of hardware, so every claim
// below is measured off the mixed signal - with no sound card needed.

// A clip's buffer is freed by the code that built its control block; a gameplay
// module that built one would free it after a reload unmapped it. So only
// ClipSamples' own constructor, in vkm_core, makes one - checked at compile time.
using SharedSamples = std::shared_ptr<const std::vector<int16_t>>;
static_assert(
    !std::is_constructible_v<decltype(AudioClipAsset::samples), SharedSamples>,
    "a clip's samples are built in vkm_core, never handed in shared"
);
static_assert(
    !std::is_assignable_v<decltype(AudioClipAsset::samples)&, SharedSamples>,
    "a clip's samples are built in vkm_core, never assigned a shared buffer"
);

constexpr uint32_t RATE     = 48000;
constexpr uint32_t CHANNELS = 2;

// A quarter-second tone, loud and simple: what is measured is a channel's energy.
AudioClipAsset makeTone(uint32_t channels = 1, float seconds = 0.25f) {
    AudioClipAsset clip;
    clip.sampleRate = RATE;
    clip.channels   = channels;

    const size_t frames = static_cast<size_t>(static_cast<float>(RATE) * seconds);
    std::vector<int16_t> samples(frames * channels);
    for (size_t frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * glm::pi<double>() * 220.0
            * static_cast<double>(frame) / static_cast<double>(RATE);
        const auto value = static_cast<int16_t>(std::sin(phase) * 26000.0);
        for (uint32_t c = 0; c < channels; ++c) samples[frame * channels + c] = value;
    }
    clip.samples = ClipSamples(std::move(samples));
    return clip;
}

// RMS of one output channel over a mixed block; assertions only compare two of these.
struct Energy {
    double left  = 0.0;
    double right = 0.0;
    bool   finite = true;
};

Energy mix(AudioDevice& device, uint64_t frames) {
    std::vector<float> out(static_cast<size_t>(frames) * CHANNELS, 0.0f);
    const uint64_t got = device.render(out.data(), frames);

    Energy energy;
    for (uint64_t i = 0; i < got; ++i) {
        const float l = out[static_cast<size_t>(i) * CHANNELS];
        const float r = out[static_cast<size_t>(i) * CHANNELS + 1];
        if (!std::isfinite(l) || !std::isfinite(r)) energy.finite = false;
        energy.left  += static_cast<double>(l) * l;
        energy.right += static_cast<double>(r) * r;
    }
    if (got > 0) {
        energy.left  = std::sqrt(energy.left  / static_cast<double>(got));
        energy.right = std::sqrt(energy.right / static_cast<double>(got));
    }
    return energy;
}

double loudness(const Energy& energy) { return energy.left + energy.right; }

// An ear at the origin looking down -Z, the engine's forward.
void placeEar(AudioDevice& device) {
    device.setListenerActive(true);
    device.setListener({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Math::WORLD_UP);
}

void testTheOfflineMixerMakesSound() {
    std::printf("The mixer with no device behind it:\n");

    AudioDevice device;
    check("a mixer opens with no hardware at all", device.openOffline(RATE, CHANNELS));
    check("  and says it is open", device.isOpen());

    const AudioClipAsset tone = makeTone();
    placeEar(device);

    VoiceParams flat;
    flat.spatial = false;
    const VoiceId voice = device.play(tone, flat);
    check("a clip plays", voice != 0);
    check("  and the mixer is holding it", device.voiceCount() == 1);

    const Energy heard = mix(device, RATE / 100);   // 10 ms
    check("and the samples that come out are a sound", loudness(heard) > 0.01);
    check("  in both channels, since it is not positioned", heard.left > 0.01 && heard.right > 0.01);

    // Played to its end, the voice finishes and the next reap frees it, so play() is
    // safe for a caller that keeps no id.
    mix(device, RATE / 2);
    check("a one-shot ends by itself", !device.isVoiceActive(voice));
    device.reapFinishedVoices();
    check("  and the reap lets it go", device.voiceCount() == 0);

    device.close();
    check("closing it leaves it closed", !device.isOpen());
}

void testAPositionedSourceIsHeardFromWhereItIs() {
    std::printf("Where a positioned source is heard from:\n");

    AudioDevice device;
    check("the mixer is up", device.openOffline(RATE, CHANNELS));
    placeEar(device);

    // Screen-right is +X (see core_tests, "the axis convention"): +X belongs in the
    // right channel, -X in the left.
    const AudioClipAsset tone = makeTone();

    VoiceParams right;
    right.spatial     = true;
    right.position    = {5.0f, 0.0f, 0.0f};
    right.minDistance = 1.0f;
    right.maxDistance = 100.0f;
    const VoiceId voiceRight = device.play(tone, right);
    const Energy fromRight = mix(device, RATE / 50);
    device.stopVoice(voiceRight);
    mix(device, RATE / 50);
    device.reapFinishedVoices();

    VoiceParams left = right;
    left.position = {-5.0f, 0.0f, 0.0f};
    const VoiceId voiceLeft = device.play(tone, left);
    const Energy fromLeft = mix(device, RATE / 50);
    device.stopVoice(voiceLeft);

    std::printf(
        "      +X: L %.4f R %.4f    -X: L %.4f R %.4f\n",
        fromRight.left,
        fromRight.right,
        fromLeft.left,
        fromLeft.right
    );
    check("a source to +X is louder on the right", fromRight.right > fromRight.left);
    check("and one to -X is louder on the left",   fromLeft.left  > fromLeft.right);
    check(
        "  by the same asymmetry, mirrored",
        std::abs((fromRight.right - fromRight.left) - (fromLeft.left - fromLeft.right)) < 0.05
    );

    device.close();
}

void testDistanceTakesTheSoundAway() {
    std::printf("What distance does to a positioned source:\n");

    const AudioClipAsset tone = makeTone();

    // Three separate runs, not three voices in one mix, so each number is one source.
    const auto atDistance = [&](float z) {
        AudioDevice device;
        device.openOffline(RATE, CHANNELS);
        placeEar(device);

        VoiceParams params;
        params.spatial     = true;
        params.position    = {0.0f, 0.0f, z};
        params.minDistance = 1.0f;
        params.maxDistance = 50.0f;
        device.play(tone, params);
        const double heard = loudness(mix(device, RATE / 50));
        device.close();
        return heard;
    };

    const double near_  = atDistance(-1.0f);    // inside minDistance: full volume
    const double middle = atDistance(-20.0f);
    const double far_   = atDistance(-49.0f);   // just inside maxDistance
    const double beyond = atDistance(-80.0f);   // past it

    std::printf("      1 m %.4f, 20 m %.4f, 49 m %.4f, 80 m %.4f\n", near_, middle, far_, beyond);

    check("a source at minDistance is heard at full volume", near_ > 0.01);
    check("  further away it is quieter", middle < near_);
    check("  further still, quieter again", far_ < middle);
    check("and past maxDistance it is inaudible", beyond < far_ * 0.5);
}

void testWithNoEarASpatialSourceIsSilentAndAFlatOneIsNot() {
    std::printf("A world with no listener in it:\n");

    const AudioClipAsset tone = makeTone();

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    device.setListenerActive(false);

    VoiceParams positioned;
    positioned.spatial  = true;
    positioned.position = {0.0f, 0.0f, -2.0f};
    device.play(tone, positioned);
    const Energy spatial = mix(device, RATE / 50);
    device.stopAllVoices();

    VoiceParams flat;
    flat.spatial = false;
    device.play(tone, flat);
    const Energy heard = mix(device, RATE / 50);

    std::printf("      positioned %.5f, flat %.5f\n", loudness(spatial), loudness(heard));
    check("a positioned source with nothing to be heard from is silent", loudness(spatial) < 1e-4);
    check("while music and UI play on regardless", loudness(heard) > 0.01);

    device.close();
}

void testOneBadGainCannotTakeTheWholeMixWithIt() {
    std::printf("The gain a game computed and nobody checked:\n");

    const AudioClipAsset tone = makeTone();

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);

    // The voice that matters: an ordinary sound beside the bad one.
    VoiceParams good;
    good.spatial = false;
    good.volume  = 0.5f;
    device.play(tone, good);

    // Values unchecked arithmetic can reach. Each must be silence: one non-finite
    // sample sums into the master and takes every other sound with it.
    const float badVolumes[] = {
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        -4.0f
    };
    for (const float bad : badVolumes) {
        VoiceParams poison;
        poison.spatial = false;
        poison.volume  = bad;
        device.play(tone, poison);
    }

    const Energy heard = mix(device, RATE / 50);
    check("the mix is still made of numbers", heard.finite);
    check("  and the sound beside them is still audible", loudness(heard) > 0.01);

    // A distance is as likely bad arithmetic as a gain (a falloff scaled by a zero
    // radius), and the curve's gain is applied per sample: a NaN distance is a NaN mix.
    const float badDistances[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -10.0f
    };
    for (const float bad : badDistances) {
        VoiceParams poison;
        poison.spatial     = true;
        poison.position    = {0.0f, 0.0f, -2.0f};
        poison.minDistance = bad;
        poison.maxDistance = bad;
        device.play(tone, poison);
    }

    const Energy withBadDistances = mix(device, RATE / 50);
    check("a distance that is not a number does not take the mix with it", withBadDistances.finite);
    check("  and the sound beside it is still there", loudness(withBadDistances) > 0.01);

    // The master multiplies every voice, so a bad one there is the whole game.
    device.setMasterVolume(std::numeric_limits<float>::quiet_NaN());
    check("a master gain that is not a number is held at silence", nearly(device.masterVolume(), 0.0f));
    device.setMasterVolume(-1.0f);
    check("  and so is a negative one", nearly(device.masterVolume(), 0.0f));

    const Energy muted = mix(device, RATE / 50);
    check("  which silences the mix rather than breaking it", muted.finite && loudness(muted) < 1e-4);

    device.setMasterVolume(1.0f);
    const Energy back = mix(device, RATE / 50);
    check("and putting it back brings the sound with it", loudness(back) > 0.01);

    device.close();
}

void testAPitchOfZeroDoesNotStopTheClock() {
    std::printf("A pitch a game can reach by dividing:\n");

    const AudioClipAsset tone = makeTone();

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);

    VoiceParams stalled;
    stalled.spatial = false;
    stalled.pitch   = 0.0f;
    const VoiceId voice = device.play(tone, stalled);
    check("a voice at zero pitch still starts", voice != 0);

    // Mixed in blocks over a long window on purpose. The floor is a hundredth of
    // normal speed, and the cursor reports the DATA SOURCE position, which the
    // resampler fills a chunk at a time, so a short read can show it standing still.
    mix(device, RATE / 50);
    const float first = device.voiceCursor(voice);
    for (int block = 0; block < 40; ++block) mix(device, RATE / 20);
    const float second = device.voiceCursor(voice);
    std::printf("      cursor %.4f s, then %.4f s two seconds of output later\n", first, second);

    // Zero would hold one sample forever: a voice never finishing or reaped, holding
    // one of the MAX_ACTIVE_VOICES.
    check("and its cursor advances rather than standing still", second > first);
    check("  slowly, which is what a floored pitch means", second < 1.0f);

    device.close();
}

void testAVoiceIsHeldAtItsCursorRatherThanRestarted() {
    std::printf("Pausing one voice, and pausing everything:\n");

    const AudioClipAsset tone = makeTone(1, 2.0f);

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);

    VoiceParams flat;
    flat.spatial = false;
    const VoiceId voice = device.play(tone, flat);

    mix(device, RATE / 10);
    const float before = device.voiceCursor(voice);
    check("the voice has played some of its clip", before > 0.0f);

    device.pauseVoice(voice);
    check("it reads as held", device.isVoicePaused(voice));
    check("  and as still there, because it has not finished", device.isVoiceActive(voice));

    // Ramped, so it plays through its fade before stopping; hence a block first.
    mix(device, RATE / 50);
    const float atHold = device.voiceCursor(voice);
    const Energy held = mix(device, RATE / 10);
    check("a held voice makes no sound", loudness(held) < 1e-4);
    check(
        "  and its cursor stops where the hold caught it",
        std::abs(device.voiceCursor(voice) - atHold) < 0.01f
    );

    device.resumeVoice(voice);
    check("resuming lets go of the hold", !device.isVoicePaused(voice));
    const Energy resumed = mix(device, RATE / 10);
    check("  and the sound comes back", loudness(resumed) > 0.01);
    check("  from where it was held, not from the start", device.voiceCursor(voice) > atHold);

    // The transport's pause is the bulk one, and gives back only what it took.
    device.pauseVoice(voice);
    device.pauseAllVoices();
    device.resumeAllVoices();
    check("a voice held on its own survives the transport resuming", device.isVoicePaused(voice));

    device.close();
}

void testTheVoiceBudgetRefusesRatherThanSteals() {
    std::printf("A game firing a sound every frame:\n");

    const AudioClipAsset tone = makeTone(1, 4.0f);

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);

    VoiceParams flat;
    flat.spatial = false;

    // The first voice is the one a game would care about - a music bed, say.
    const VoiceId music = device.play(tone, flat);
    check("the first sound plays", music != 0);

    size_t started = 1;
    VoiceId last = music;
    for (size_t i = 1; i < MAX_ACTIVE_VOICES + 8; ++i) {
        const VoiceId id = device.play(tone, flat);
        if (id != 0) {
            ++started;
            last = id;
        }
    }

    check("the mixer takes exactly its budget", started == MAX_ACTIVE_VOICES);
    check("  and holds that many", device.voiceCount() == MAX_ACTIVE_VOICES);
    check("a sound past it is refused rather than made room for", device.play(tone, flat) == 0);

    // The point of refusing: the oldest voice is as likely the music as the newest a
    // footstep.
    check("and the first sound is still playing", device.isVoiceActive(music));
    check("  as is the last one that fit", device.isVoiceActive(last));

    device.close();
}

void testStoppingASoundDoesNotClickAndSeekingStaysInTheClip() {
    std::printf("How a sound ends, and where it can be moved to:\n");

    const AudioClipAsset tone = makeTone(1, 2.0f);

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);

    VoiceParams flat;
    flat.spatial = false;
    const VoiceId voice = device.play(tone, flat);
    mix(device, RATE / 20);

    // The ramp is why stopVoice exists: cut outright, a waveform ends on whatever
    // sample it was at. Measured as a tail quieter than the sound but not silent at once.
    device.stopVoice(voice);
    const Energy tail = mix(device, 256);
    check(
        "a stopped sound fades rather than ending on the sample it was on",
        tail.finite && loudness(tail) > 0.0
    );
    check("  and the id reads as gone the moment it is asked to stop", !device.isVoiceActive(voice));

    const Energy after = mix(device, RATE / 20);
    check("  with the tail over well inside a frame", loudness(after) < 1e-4);
    device.reapFinishedVoices();
    check("  and the reap frees it", device.voiceCount() == 0);

    // A seek past the end is clamped: miniaudio refuses an out-of-range seek but moves
    // the sound's clock to it anyway.
    const VoiceId scrubbed = device.play(tone, flat);
    device.seekVoice(scrubbed, 99.0f);
    check("a seek past the end lands in the clip, not past it", device.voiceCursor(scrubbed) <= 2.0f + 1e-3f);
    device.seekVoice(scrubbed, -5.0f);
    check("  and one before the start lands at zero", nearly(device.voiceCursor(scrubbed), 0.0f));
    device.seekVoice(scrubbed, std::numeric_limits<float>::quiet_NaN());
    check("  as does one that is not a number", nearly(device.voiceCursor(scrubbed), 0.0f));

    device.close();
}

constexpr bool PLAY_ON_START   = true;
constexpr bool PLAY_ON_REQUEST = false;
constexpr bool NON_SPATIAL     = false;
constexpr bool NO_POSE         = false;
constexpr bool SIM_PAUSED      = false;
constexpr bool SIM_RUNNING     = true;

// What AudioSystem does with components, over a real Scene with the device closed:
// the claims are about the reconcile, not the mix.
struct SilentAudioWorld {
    Scene       scene;
    TestFrame   frame{scene};
    AudioSystem audio;

    /**
     * @brief A clip for the sources to name.
     *
     * A dead handle has its request cleared on the spot - a different test.
     */
    AudioClipHandle clip;

    SilentAudioWorld() {
        audio.setSilent();
        audio.init(frame.ctx);
        clip = frame.resources.add(makeTone(), "test:tone");
        // Starts the clock, so the first measured frame is the first tick().
        frame.clock.beginFrame();
    }
    ~SilentAudioWorld() { audio.shutdown(); }

    /// An entity with a source naming the clip above.
    EntityId addSource(bool playOnStart, bool spatial = true, bool posed = true) {
        const EntityId entity = scene.createEntity();
        if (posed) scene.add(entity, Transform{});
        AudioSource source;
        source.clip        = clip;
        source.playOnStart = playOnStart;
        source.spatial     = spatial;
        scene.add(entity, std::move(source));
        return entity;
    }

    // The clock measures a real span, and two back-to-back beginFrame() calls on a fast
    // machine round to zero - a paused frame to playOnStart, which the engine never has.
    void tick(bool simRunning = true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        frame.clock.setPaused(!simRunning);
        frame.clock.beginFrame();
        audio.update(frame.ctx);
        frame.events.flush();
    }
};

// The voice cap is the one resource audio.md says a game can exhaust by accident; a
// fire-and-forget request taking a voice at any distance would let a coin ping across
// the level hold one at zero gain for the clip's length.
//
// Safe to drop for a request: its position does not follow, and loop is forced false
// at start, so one inaudible at start never becomes audible. A source moves, so it
// must still start.
void testARequestTooFarToHearDoesNotSpendAVoice() {
    std::printf("A fire-and-forget sound from the far side of the level:\n");

    SilentAudioWorld world;
    // Silent leaves the device closed, holding no voices to count; offline is the same
    // mixer without a sound card.
    world.audio.device().openOffline(RATE, CHANNELS);

    // An ear at the origin; without one there is no distance, and nothing may be culled.
    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});
    world.tick();

    // The one-line form a behavior writes, from the reference it authored.
    const AudioClipHandle named = world.frame.resources.find(AssetRef<AudioClipAsset>{"test:tone"});
    check("an authored reference finds the clip it names", named == world.clip);
    check("  and an empty one finds nothing", !world.frame.resources.find(AssetRef<AudioClipAsset>{}));
    const PlaySoundEvent oneLine = PlaySoundEvent::at(named, {2.0f, 0.0f, 0.0f}, 0.5f);
    check(
        "PlaySoundEvent::at is a spatial sound where it says, as loud as it says",
        oneLine.clip == world.clip
            && oneLine.params.spatial
            && oneLine.params.position == glm::vec3(2.0f, 0.0f, 0.0f)
            && oneLine.params.volume == 0.5f
    );
    world.frame.events.enqueue(oneLine);
    world.tick();
    world.tick();
    check("  and is played", world.audio.device().voiceCount() == 1);
    world.audio.stopEverything();

    VoiceParams withinEarshot;
    withinEarshot.spatial     = true;
    withinEarshot.maxDistance = 50.0f;
    withinEarshot.position    = {1.0f, 0.0f, 0.0f};
    // Two ticks: the bus delivers at the end of one frame, and startPendingRequests
    // drains it on the next.
    world.frame.events.enqueue(PlaySoundEvent{world.clip, withinEarshot});
    world.tick();
    world.tick();
    check("a request within earshot is played", world.audio.device().voiceCount() == 1);

    VoiceParams tooFar = withinEarshot;
    tooFar.position = {5000.0f, 0.0f, 0.0f};
    world.frame.events.enqueue(PlaySoundEvent{world.clip, tooFar});
    world.tick();
    world.tick();
    check("  and one far past maxDistance takes no voice of its own", world.audio.device().voiceCount() == 1);

    // A flat sound has no position, so distance must not touch it.
    VoiceParams flat = tooFar;
    flat.spatial = false;
    world.frame.events.enqueue(PlaySoundEvent{world.clip, flat});
    world.tick();
    world.tick();
    check(
        "  while a non-spatial request is played wherever it says it is",
        world.audio.device().voiceCount() == 2
    );

    // A range not opening past minDistance turns attenuation off: full gain at any distance.
    VoiceParams unattenuated = tooFar;
    unattenuated.minDistance = 10.0f;
    unattenuated.maxDistance = 5.0f;
    world.frame.events.enqueue(PlaySoundEvent{world.clip, unattenuated});
    world.tick();
    world.tick();
    check("  and so is a positioned one whose attenuation is off", world.audio.device().voiceCount() == 3);
}

void testASourceIsOnlyStartedByTimeMoving() {
    std::printf("What playOnStart waits for:\n");

    SilentAudioWorld world;
    const EntityId entity = world.addSource(PLAY_ON_START);

    // Paused is a scene open in the editor; ambience starting on open is the noise this
    // gate stops.
    world.tick(SIM_PAUSED);
    check("a scene merely open does not start its sounds", !world.scene.get<AudioSource>(entity).playing);
    check("  and has not spent its one start", !world.scene.get<AudioSource>(entity).started);

    world.tick(SIM_RUNNING);
    check("the first frame of simulation starts it", world.scene.get<AudioSource>(entity).started);

    // The start is spent once, or the source would restart its sound every frame.
    world.scene.get<AudioSource>(entity).playing = false;
    world.tick(SIM_RUNNING);
    check("  and does not start it a second time", !world.scene.get<AudioSource>(entity).playing);
}

// A host with no sound card - like a dedicated server - runs AudioSystem whole against
// a device that never opened, as if every sound were zero-length. Pinned because no
// one listens on this path: a regression is silent in both senses.
void testASilentHostRunsTheWholeSystemAnyway() {
    std::printf("A host with no audio device at all:\n");

    SilentAudioWorld world;
    check("the device never opened", !world.audio.device().isOpen());

    const EntityId entity = world.addSource(PLAY_ON_START);
    world.tick();

    const AudioSource& source = world.scene.get<AudioSource>(entity);
    check("a source still takes its one start", source.started);
    // Cleared in the same pass: play() returns no voice, as for a zero-length clip, so
    // gameplay reading `playing` learns the sound is over rather than waiting forever.
    check("  and the sound is over as soon as it began", !source.playing);
    check("  with no voice behind it", world.audio.voiceOf(entity) == 0);

    // It asks once: retrying would burn a lookup per source per frame on a server.
    world.tick();
    world.tick();
    check("and it does not ask again", !source.playing && source.started);
}

void testASourceWithNothingToPlayStopsAsking() {
    std::printf("A source asked to play with no clip:\n");

    SilentAudioWorld world;
    const EntityId entity = world.addSource(PLAY_ON_REQUEST);
    world.scene.get<AudioSource>(entity).clip = AudioClipHandle{};
    world.scene.get<AudioSource>(entity).playing = true;   // asked for by hand

    world.tick();
    // Cleared, not retried, or a clipless source asks the mixer every frame for good.
    check("the request is dropped rather than repeated", !world.scene.get<AudioSource>(entity).playing);
}

void testASourceWithNoPoseIsStillHeard() {
    std::printf("A UI click on an entity with no Transform:\n");

    SilentAudioWorld world;
    const EntityId entity = world.addSource(PLAY_ON_START, NON_SPATIAL, NO_POSE);

    // Joining the walk on Transform would leave this source silent with `playing` stuck
    // true - the shape a UI button has.
    world.tick();
    check("a source with no pose is still visited", world.scene.get<AudioSource>(entity).started);
}

void testAVoiceDoesNotOutliveTheEntityThatOwnedIt() {
    std::printf("What happens to a voice when its source goes:\n");

    SilentAudioWorld world;
    // Offline, since a closed device hands out no voices and the table would be empty.
    world.audio.device().openOffline(RATE, CHANNELS);
    const EntityId entity = world.addSource(PLAY_ON_START);

    world.tick();
    check("the source was started", world.scene.get<AudioSource>(entity).started);
    check("  with a voice behind it", world.audio.voiceOf(entity) != 0);

    // Nothing may stay keyed to a slot whose entity is gone.
    world.scene.destroyEntity(entity);
    world.tick();
    check("and nothing is left tracking the entity that is gone", world.audio.voiceOf(entity) == 0);

    // The recycled slot must not inherit the old entity's sound.
    const EntityId reused = world.scene.createEntity();
    check("the slot was recycled", reused.slot() == entity.slot());
    check("  and the new entity inherits no voice", world.audio.voiceOf(reused) == 0);
}

// A source's voice is its clip's samples, so re-pointing a playing source - a
// footstep per surface, the next level's music - must be heard. A reconcile pushing
// only gain and position would play the old clip on.
void testAPlayingSourceRePointedAtAnotherClipPlaysIt() {
    std::printf("A playing source given a different clip:\n");

    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);

    const EntityId entity = world.addSource(PLAY_ON_REQUEST, NON_SPATIAL);
    world.scene.get<AudioSource>(entity).playing = true;
    world.tick();
    const VoiceId first = world.audio.voiceOf(entity);
    check("the source is playing its clip", first != 0);

    world.scene.get<AudioSource>(entity).clip =
        world.frame.resources.add(makeTone(1, 0.5f), "test:other");
    world.tick();
    const VoiceId second = world.audio.voiceOf(entity);
    check("re-pointed, it plays the new clip on a voice of its own", second != 0 && second != first);
    check("  and is still playing", world.scene.get<AudioSource>(entity).playing);

    world.tick();
    check("  which it keeps rather than restarting every frame", world.audio.voiceOf(entity) == second);
}

void testTheEarIsTheLowestSlotListener() {
    std::printf("Which of two listeners is the ear:\n");

    Scene scene;
    const EntityId first = scene.createEntity();
    scene.add(first, Transform{});
    scene.add(first, AudioListener{});

    const EntityId second = scene.createEntity();
    scene.add(second, Transform{});
    scene.add(second, AudioListener{});

    check("two active listeners resolve to the lower slot", findActiveListener(scene) == first);

    // Unticking the first hands the ear to the second, not to nothing.
    scene.get<AudioListener>(first).active = false;
    check("  and unticking it hands the ear on", findActiveListener(scene) == second);

    // A listener with no pose has nowhere to measure from, so it is no ear - the rule
    // findActiveListener states and nothing else could enforce.
    const EntityId poseless = scene.createEntity();
    scene.add(poseless, AudioListener{});
    scene.get<AudioListener>(second).active = false;
    check("a listener with no Transform is not the ear", !findActiveListener(scene));
}

} // namespace

void runAudioTests() {
    testTheOfflineMixerMakesSound();
    testAPositionedSourceIsHeardFromWhereItIs();
    testDistanceTakesTheSoundAway();
    testWithNoEarASpatialSourceIsSilentAndAFlatOneIsNot();
    testOneBadGainCannotTakeTheWholeMixWithIt();
    testAPitchOfZeroDoesNotStopTheClock();
    testAVoiceIsHeldAtItsCursorRatherThanRestarted();
    testTheVoiceBudgetRefusesRatherThanSteals();
    testStoppingASoundDoesNotClickAndSeekingStaysInTheClip();

    testASourceIsOnlyStartedByTimeMoving();
    testARequestTooFarToHearDoesNotSpendAVoice();
    testASilentHostRunsTheWholeSystemAnyway();
    testASourceWithNothingToPlayStopsAsking();
    testASourceWithNoPoseIsStillHeard();
    testAVoiceDoesNotOutliveTheEntityThatOwnedIt();
    testAPlayingSourceRePointedAtAnotherClipPlaysIt();
    testTheEarIsTheLowestSlotListener();
}

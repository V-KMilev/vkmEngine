#include "support.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include "physics/physics_support.h"

#include "debug/debug_draw.h"

#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/environment.h"
#include "ecs/hierarchy_operations.h"
#include "system/audio/acoustics.h"
#include "system/audio/audio_device.h"
#include "system/audio/audio_events.h"
#include "system/audio/audio_system.h"
#include "system/audio/reverb.h"
#include "system/physics/query/query.h"

namespace {

// What a source sounds like is decided inside the mixer, by a spatializer and an
// attenuation curve the engine never sees the result of. AudioDevice::openOffline
// runs the same graph and voices into a buffer instead of hardware, so what is heard
// is measured off the mixed signal - with no sound card needed.

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
AudioClipAsset makeTone(uint32_t channels = 1, float seconds = 0.25f, double hertz = 220.0) {
    AudioClipAsset clip;
    clip.sampleRate = RATE;
    clip.channels   = channels;

    const size_t frames = static_cast<size_t>(static_cast<float>(RATE) * seconds);
    std::vector<int16_t> samples(frames * channels);
    for (size_t frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * glm::pi<double>() * hertz
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

// A stereo clip placed to one side leans that way, each output channel turned down by how far
// it faces from the source; but nothing crosses, so what the clip holds in its right channel is
// never heard on the left, wherever the emitter goes.
void testAStereoClipLeansButNeverCrosses() {
    std::printf("A positioned stereo clip:\n");

    AudioDevice device;
    check("the mixer is up", device.openOffline(RATE, CHANNELS));
    placeEar(device);

    AudioClipAsset both = makeTone(2);
    std::vector<int16_t> samples(both.samples->begin(), both.samples->end());
    for (size_t i = 0; i < samples.size(); i += 2) samples[i] = 0;
    AudioClipAsset rightOnly = both;
    rightOnly.samples = ClipSamples(std::move(samples));

    const auto heardAt = [&](const AudioClipAsset& clip, float x) {
        VoiceParams params;
        params.position    = {x, 0.0f, 0.0f};
        params.maxDistance = 100.0f;
        const VoiceId voice = device.play(clip, params);
        const Energy heard = mix(device, RATE / 50);
        device.stopVoice(voice);
        mix(device, RATE / 50);
        device.reapFinishedVoices();
        return heard;
    };

    const Energy toTheRight = heardAt(both, 5.0f);
    check("to +X it leans right", toTheRight.right > 2.0 * toTheRight.left);
    const Energy rightToTheLeft = heardAt(rightOnly, -5.0f);
    check(
        "its right channel taken to -X is quieter, never on the left",
        rightToTheLeft.left < 1e-4 && rightToTheLeft.right > 0.01 && rightToTheLeft.right < toTheRight.right
    );
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

// AudioRolloff::Inverse falls as 1/distance near the source and still reaches silence at
// maxDistance, and its gain is redone as the ear moves, for a one-shot nothing updates too.
void testAnInverseRolloffFallsAsOneOverDistanceToSilence() {
    std::printf("An inverse-distance rolloff:\n");

    VoiceParams range;
    range.minDistance = 1.0f;
    range.maxDistance = 8.0f;
    check(
        "full volume to minDistance",
        inverseRolloffGain(range, 0.5f) == 1.0f && inverseRolloffGain(range, 1.0f) == 1.0f
    );
    check(
        "  then 1/distance, bent to reach zero at maxDistance",
        std::abs(inverseRolloffGain(range, 2.0f) - (0.5f - 0.125f) / 0.875f) < 1e-6f
            && inverseRolloffGain(range, 8.0f) == 0.0f && inverseRolloffGain(range, 20.0f) == 0.0f
    );

    const AudioClipAsset tone = makeTone(1, 1.0f);
    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);
    VoiceParams params = range;
    params.spatial  = true;
    params.rolloff  = AudioRolloff::Inverse;
    params.position = {0.0f, 0.0f, -1.0f};
    device.play(tone, params);
    const double atOne = loudness(mix(device, RATE / 50));
    device.setListener({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, Math::WORLD_UP);
    const double atTwo = loudness(mix(device, RATE / 50));
    device.setListener({0.0f, 0.0f, 9.0f}, {0.0f, 0.0f, -1.0f}, Math::WORLD_UP);
    const double atTen = loudness(mix(device, RATE / 50));
    std::printf("      1 m %.4f, 2 m %.4f, 10 m %.4f\n", atOne, atTwo, atTen);
    // 0.43 by the curve; miniaudio's linear fade stacked on it would make it 0.37.
    check(
        "a one-shot fades as the ear walks off, as the curve says",
        std::abs(atTwo / atOne - 0.4286) < 0.025
    );
    check("  and is silent past maxDistance", atTen < atOne * 1e-3);
    device.close();
}

// A clip's loopStart plays what comes before it once: the loop goes back to it, not to the top.
void testALoopGoesBackToTheClipsLoopStart() {
    std::printf("A clip with an intro before its loop:\n");

    // A silent tenth of a second, then a tone: looping whole would come back to silence.
    AudioClipAsset clip = makeTone(1, 0.2f);
    std::vector<int16_t> samples = *clip.samples;
    const size_t intro = RATE / 10;
    std::fill(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(intro), int16_t{0});
    clip.samples   = ClipSamples(std::move(samples));
    clip.loopStart = intro;

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    VoiceParams flat;
    flat.spatial = false;
    flat.loop    = true;
    device.play(clip, flat);
    const double introHeard = loudness(mix(device, RATE / 20));
    mix(device, RATE / 20 + RATE / 10);
    const double secondTime = loudness(mix(device, RATE / 20));
    check("the intro plays", introHeard < 1e-4);
    check("  and once: the second time round is the loop", secondTime > 0.1);
    device.close();
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

// At the cap a new sound takes the place of a voice it outranks - of a lower priority, or of its
// own and quieter - so the music keeps playing and a gunshot is heard, and a sound a pause holds
// is kept for its resume.
void testTheVoiceBudgetMakesRoomByPriority() {
    std::printf("A mix past its voice budget:\n");

    const AudioClipAsset tone = makeTone(1, 4.0f);

    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);

    VoiceParams loud;
    loud.spatial = false;
    VoiceParams music = loud;
    music.priority    = 10;
    VoiceParams quiet = loud;
    quiet.volume      = 0.1f;
    VoiceParams held  = loud;
    held.volume       = 0.05f;
    VoiceParams minor = loud;
    minor.priority    = -5;

    const VoiceId bed     = device.play(tone, music);
    const VoiceId paused  = device.play(tone, held);
    const VoiceId softest = device.play(tone, quiet);
    const VoiceId lesser  = device.play(tone, minor);
    device.pauseVoice(paused);
    for (size_t i = 4; i < MAX_ACTIVE_VOICES; ++i) device.play(tone, loud);
    check("the mix fills to its budget", device.voiceCount() == MAX_ACTIVE_VOICES);

    const VoiceId shot = device.play(tone, loud);
    check("a new sound past it is heard", shot != 0 && device.isVoiceActive(shot));
    check("  in place of a lower priority, however loud", !device.isVoiceActive(lesser));
    const VoiceId next = device.play(tone, loud);
    check("  then of the quietest of its own", next != 0 && !device.isVoiceActive(softest));
    check("  never of the one a pause holds, quieter still", device.isVoicePaused(paused));
    check("  nor of the music, which matters more", device.isVoiceActive(bed));
    check("a sound no louder than its equals cuts none of them", device.play(tone, loud) == 0);
    check("  nor does one that matters less than every voice", device.play(tone, minor) == 0);
    device.close();

    // Positioned: what is far off is quieter, so it gives way first.
    AudioDevice spatial;
    spatial.openOffline(RATE, CHANNELS);
    placeEar(spatial);
    VoiceParams nearby;
    nearby.position = {0.0f, 0.0f, -2.0f};
    VoiceParams distant = nearby;
    distant.position    = {0.0f, 0.0f, -40.0f};
    const VoiceId away = spatial.play(tone, distant);
    for (size_t i = 1; i < MAX_ACTIVE_VOICES; ++i) spatial.play(tone, nearby);
    const VoiceId closer = spatial.play(tone, loud);
    check("a far voice gives way before a near one", closer != 0 && !spatial.isVoiceActive(away));
    spatial.close();

    // Nothing reaps a voice ramping out while the mixer does not run, as when the device is lost.
    AudioDevice stalled;
    stalled.openOffline(RATE, CHANNELS);
    placeEar(stalled);
    VoiceParams rising = loud;
    for (int i = 0; i < static_cast<int>(MAX_ACTIVE_VOICES) * 3; ++i) {
        rising.priority = i;
        stalled.play(tone, rising);
    }
    check("voices ramping out never pile past a ceiling", stalled.voiceCount() <= MAX_HELD_VOICES);
    stalled.close();
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

    // A scene load or Stop ends everything at once: as a fade too, not a cut.
    device.play(tone, flat);
    mix(device, RATE / 20);
    device.stopAllVoices();
    const Energy allTail = mix(device, 256);
    check("everything stopped at once fades too", allTail.finite && loudness(allTail) > 0.0);
    check("  over as quickly", loudness(mix(device, RATE / 20)) < 1e-4);
    device.reapFinishedVoices();
    check("  and the reap frees it all", device.voiceCount() == 0);

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
    // Stopped with a ramp, which offline only a mix plays out; the next tick reaps it.
    mix(world.audio.device(), RATE / 50);

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

// Acoustics. The estimators are tried first against spaces worked out on paper - the inside of a
// box, open ground, a wall with a doorway - then against the same shapes as colliders, then heard.

// The inside of an axis-aligned box: the nearest wall along a ray, its normal facing in.
struct BoxRoom {
    glm::vec3 lo;
    glm::vec3 hi;
    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) const {
        float best = reach;
        int   axis = -1;
        float in   = 0.0f;
        for (int a = 0; a < 3; ++a) {
            if (direction[a] > 1e-6f) {
                const float t = (hi[a] - origin[a]) / direction[a];
                if (t < best) {
                    best = t;
                    axis = a;
                    in   = -1.0f;
                }
            } else if (direction[a] < -1e-6f) {
                const float t = (lo[a] - origin[a]) / direction[a];
                if (t < best) {
                    best = t;
                    axis = a;
                    in   = 1.0f;
                }
            }
        }
        if (axis < 0) return false;
        strike.distance     = best;
        strike.point        = origin + direction * best;
        strike.normal       = glm::vec3(0.0f);
        strike.normal[axis] = in;
        return true;
    }
};

// Open ground: a floor at y = 0 and nothing else.
struct Ground {
    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) const {
        if (direction.y >= -1e-6f) return false;
        const float t = -origin.y / direction.y;
        if (t > reach) return false;
        strike = {t, origin + direction * t, {0.0f, 1.0f, 0.0f}};
        return true;
    }
};

// The plane x = 2, open where |z| < 0.6 below 2.2 m: a wall with a doorway.
struct WallWithDoor {
    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) const {
        if (std::abs(direction.x) < 1e-6f) return false;
        const float t = (2.0f - origin.x) / direction.x;
        if (t <= 0.0f || t > reach) return false;
        const glm::vec3 p = origin + direction * t;
        if (std::abs(p.z) < 0.6f && p.y < 2.2f) return false;
        strike = {t, p, {direction.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f}};
        return true;
    }
};

// A scene's colliders, through the physics query the engine casts with.
struct Colliders {
    QueryParts parts;

    explicit Colliders(Scene& scene) { parts.gather(scene, {}, {glm::vec3(-50.0f), glm::vec3(50.0f)}); }

    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) const {
        RayHit hit;
        if (!parts.raycast(origin, direction, reach, hit)) return false;
        strike = {hit.distance, hit.point, hit.normal};
        return true;
    }
};

// The room as the first @p slots of the window measure it.
template <typename Cast>
Acoustics::Room measure(const Cast& cast, glm::vec3 ear, size_t slots = Acoustics::ROOM_WINDOW) {
    std::vector<Acoustics::RoomRay> rays;
    for (size_t slot = 0; slot < slots; ++slot) {
        rays.push_back(Acoustics::traceRoomRay(cast, ear, Acoustics::roomDirection(slot)));
    }
    return Acoustics::estimateRoom(rays.data(), rays.size());
}

void testTheRaysTellAShutRoomFromOpenGround() {
    std::printf("What the rays say about the space the ear is in:\n");

    const glm::vec3 ear(0.0f, 1.6f, 0.0f);
    const Acoustics::Room hall = measure(BoxRoom{{-5.0f, 0.0f, -3.0f}, {5.0f, 4.0f, 3.0f}}, ear);
    // A room's mean free path is 4V/S.
    const float volume = 10.0f * 4.0f * 6.0f;
    const float area   = 2.0f * (10.0f * 4.0f + 10.0f * 6.0f + 4.0f * 6.0f);
    const float walls  = 4.0f * volume / area;
    std::printf(
        "      hall: shut %.2f, free path %.2f m against 4V/S %.2f m\n",
        static_cast<double>(hall.enclosure),
        static_cast<double>(hall.meanFreePath),
        static_cast<double>(walls)
    );
    check("a room shut on every side reads shut", hall.enclosure > 0.99f);
    check("  and as big as its walls make it", std::abs(hall.meanFreePath - walls) < 0.35f * walls);

    const Acoustics::Room field = measure(Ground{}, ear);
    std::printf("      field: shut %.2f\n", static_cast<double>(field.enclosure));
    check("open ground reads open", field.enclosure < 0.25f);
    // Two frames' rays, as a scene's first frames have: the ground below as well as the sky.
    const Acoustics::Room glimpse = measure(Ground{}, ear, 2 * Acoustics::ROOM_RAYS_PER_FRAME);
    check("  and a window part refilled sees the ground as well as the sky", glimpse.enclosure > 0.0f);

    const Acoustics::Reverb inHall  = Acoustics::reverbFor(hall, 1.0f);
    const Acoustics::Reverb inField = Acoustics::reverbFor(field, 1.0f);
    check("the room rings and the field does not", inHall.wet > 0.0f && inField.wet == 0.0f);

    const Acoustics::Room cathedral = measure(BoxRoom{{-20.0f, 0.0f, -12.0f}, {20.0f, 25.0f, 12.0f}}, ear);
    const float hallDecay = inHall.decaySeconds;
    check("a bigger room rings longer", Acoustics::reverbFor(cathedral, 1.0f).decaySeconds > hallDecay);
    check("  and a strength of 0 rings none", Acoustics::reverbFor(hall, 0.0f).wet == 0.0f);
}

// The way a room ray went, which the acoustics view draws.
void testARoomRayKeepsTheWayItWent() {
    std::printf("The way a room ray went:\n");

    const glm::vec3 ear(0.0f, 1.6f, 0.0f);
    const glm::vec3 lo(-5.0f, 0.0f, -3.0f);
    const glm::vec3 hi(5.0f, 4.0f, 3.0f);
    const glm::vec3 out = glm::normalize(glm::vec3(1.0f, 0.3f, 0.2f));
    const Acoustics::RoomRay shut = Acoustics::traceRoomRay(BoxRoom{lo, hi}, ear, out);
    bool  onWalls = true;
    float legs    = 0.0f;
    for (size_t k = 1; k <= static_cast<size_t>(shut.strikes); ++k) {
        const glm::vec3& p = shut.path[k];
        bool onOne = false;
        for (int a = 0; a < 3; ++a) {
            onOne = onOne || std::abs(p[a] - lo[a]) < 1e-3f || std::abs(p[a] - hi[a]) < 1e-3f;
        }
        onWalls = onWalls && onOne;
        legs += glm::distance(shut.path[k - 1], p);
    }
    check("a ray in a shut room strikes on every leg", shut.strikes == 1 + Acoustics::ROOM_BOUNCES);
    check("  from the ear", shut.path[0] == ear);
    check("  off the walls", onWalls);
    check("  along its legs", std::abs(legs - shut.length) < 1e-2f);
    check("  and never out into the open", shut.escape == glm::vec3(0.0f));

    const glm::vec3          up      = glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3          down    = glm::normalize(glm::vec3(0.3f, -1.0f, 0.0f));
    const Acoustics::RoomRay grazed  = Acoustics::traceRoomRay(Ground{}, ear, down);
    const Acoustics::RoomRay skyward = Acoustics::traceRoomRay(Ground{}, ear, up);
    const bool onTheGround = grazed.strikes == 1 && std::abs(grazed.path[1].y) < 1e-4f;
    check("on open ground a ray down strikes once", onTheGround);
    const glm::vec3 sentOut = glm::reflect(down, up);
    check("  and goes out the way the ground sent it", glm::distance(grazed.escape, sentOut) < 1e-4f);
    check("  and one up goes straight out", skyward.strikes == 0 && skyward.escape == up);
}

// A floor at y = 0 and a wall at x = 0.2 meeting it: the nearer of the two along a ray.
struct Corner {
    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) const {
        bool struck = Ground{}(origin, direction, reach, strike);
        if (direction.x > 1e-6f) {
            const float t = (0.2f - origin.x) / direction.x;
            if (t > 0.0f && t <= reach && (!struck || t < strike.distance)) {
                strike = {t, origin + direction * t, {-1.0f, 0.0f, 0.0f}};
                struck = true;
            }
        }
        return struck;
    }
};

// A sound on a floor, or in a corner, with nothing between it and the ear: the surfaces it sits by
// do not muffle it.
void testASoundIsNotMuffledByWhatItSitsOn() {
    std::printf("A sound by the surfaces it sits on, in plain view:\n");

    const glm::vec3 ear(0.0f, 1.6f, 5.0f);
    const glm::vec3 onTheFloor(0.0f, 0.1f, 0.0f);
    const float floor  = Acoustics::blockedShare(Ground{}, ear, onTheFloor, 1.0f);
    const float corner = Acoustics::blockedShare(Corner{}, ear, onTheFloor, 1.0f);
    check("a sound on the floor is heard straight", floor == 0.0f);
    check("  and one in a corner", corner == 0.0f);
}

// The rays the view draws to a sound are the ones its share counts.
void testTheRaysToASoundAreTheOnesItsShareCounts() {
    std::printf("The rays to a sound, each:\n");

    const glm::vec3 ear(0.0f, 1.5f, 0.0f);
    // Through the doorway, past its edge, behind the wall.
    for (const float z : {0.0f, 0.9f, 3.0f}) {
        const glm::vec3 source(4.0f, 1.5f, z);
        size_t cast      = 0;
        size_t stopped   = 0;
        bool   onTheWall = true;
        const auto count = [&](const glm::vec3&, const Acoustics::Strike* strike) {
            ++cast;
            if (!strike) return;
            ++stopped;
            onTheWall = onTheWall && std::abs(strike->point.x - 2.0f) < 1e-4f;
        };
        Acoustics::castOcclusionRays(WallWithDoor{}, ear, source, 0.5f, count);
        const float share = Acoustics::blockedShare(WallWithDoor{}, ear, source, 0.5f);
        std::printf("      to z = %.1f: %zu of %zu stopped\n", static_cast<double>(z), stopped, cast);
        check("every ray to a sound is cast", cast == Acoustics::OCCLUSION_RAYS);
        check("  and the share is of those stopped", share * Acoustics::OCCLUSION_RAYS == stopped);
        check("  where the wall stands", onTheWall);
    }
}

void testAWallStopsASoundAndADoorwayLetsItThrough() {
    std::printf("What stands between the ear and a sound:\n");

    const WallWithDoor wall;
    const glm::vec3 ear(0.0f, 1.5f, 0.0f);
    const float throughTheDoor = Acoustics::blockedShare(wall, ear, {4.0f, 1.5f, 0.0f}, 0.5f);
    const float behindTheWall  = Acoustics::blockedShare(wall, ear, {4.0f, 1.5f, 3.0f}, 0.5f);
    const float byTheJamb      = Acoustics::blockedShare(wall, ear, {4.0f, 1.5f, 1.0f}, 0.5f);
    std::printf(
        "      door %.2f, wall %.2f, jamb %.2f\n",
        static_cast<double>(throughTheDoor),
        static_cast<double>(behindTheWall),
        static_cast<double>(byTheJamb)
    );
    check("a sound through a doorway is heard straight", throughTheDoor == 0.0f);
    check("one behind the wall is stopped", behindTheWall == 1.0f);
    check("  and one by the jamb is partly", byTheJamb > 0.0f && byTheJamb < 1.0f);

    // A sound pressed to a surface - an impact where it struck - is forgiven the last stretch; one a
    // hand's breadth behind a wall is not. The body a sound sits in is the caster's to pass through.
    const auto faceAt = [](float x) {
        return [x](glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) {
            const float t = (x - origin.x) / direction.x;
            if (t <= 0.0f || t > reach) return false;
            strike = {t, origin + direction * t, {-1.0f, 0.0f, 0.0f}};
            return true;
        };
    };
    const float pressed    = Acoustics::blockedShare(faceAt(3.95f), ear, {4.0f, 1.5f, 0.0f}, 0.5f);
    const float justBehind = Acoustics::blockedShare(faceAt(3.75f), ear, {4.0f, 1.5f, 0.0f}, 0.5f);
    check("  but not by a surface it is pressed to", pressed == 0.0f);
    check("  though one just behind a wall is", justBehind == 1.0f);

    check("an occluded sound is quieter", Acoustics::occludedGain(1.0f) < Acoustics::occludedGain(0.0f));
    check("  and duller", Acoustics::occludedCutoff(1.0f) < Acoustics::occludedCutoff(0.0f));
}

void testTheEasingIsTheSameAtAnyFramerate() {
    std::printf("How occlusion and the room ease:\n");

    const float whole  = Acoustics::approach(1.0f, 0.0f, 0.02f, 0.12f);
    const float half   = Acoustics::approach(1.0f, 0.0f, 0.01f, 0.12f);
    const float halves = Acoustics::approach(half, 0.0f, 0.01f, 0.12f);
    check("two half frames ease as far as one whole one", std::abs(whole - halves) < 1e-6f);
    check("  and a paused frame not at all", Acoustics::approach(1.0f, 0.0f, 0.0f, 0.12f) == 1.0f);
    float eased = 1.0f;
    for (int frame = 0; frame < 120; ++frame) eased = Acoustics::approach(eased, 0.0f, 1.0f / 60.0f, 0.12f);
    check("eased for two seconds, it lands on its target exactly", eased == 0.0f);
}

// The same claims against colliders.
void testTheColliderWorldIsHeardAsDrawn() {
    std::printf("Rays against the world's colliders:\n");

    Scene scene;
    // A wall at x = 2 with a doorway at |z| < 0.6 up to 2.2 m.
    addBox(scene, {2.0f, 1.5f, 3.3f}, {0.1f, 1.5f, 2.7f});
    addBox(scene, {2.0f, 1.5f, -3.3f}, {0.1f, 1.5f, 2.7f});
    addBox(scene, {2.0f, 2.6f, 0.0f}, {0.1f, 0.4f, 0.6f});
    const Colliders wall(scene);
    const glm::vec3 ear(0.0f, 1.5f, 0.0f);
    const float throughTheDoor = Acoustics::blockedShare(wall, ear, {4.0f, 1.5f, 0.0f}, 0.5f);
    const float behindTheWall  = Acoustics::blockedShare(wall, ear, {4.0f, 1.5f, 3.0f}, 0.5f);
    check("a doorway of colliders lets a sound through", throughTheDoor == 0.0f);
    check("  and its wall stops one behind it", behindTheWall == 1.0f);

    // A room of six boxes: floor, ceiling, four walls.
    Scene room;
    addBox(room, {0.0f, -0.1f, 0.0f}, {5.2f, 0.1f, 3.2f});
    addBox(room, {0.0f, 4.1f, 0.0f}, {5.2f, 0.1f, 3.2f});
    addBox(room, {-5.1f, 2.0f, 0.0f}, {0.1f, 2.2f, 3.2f});
    addBox(room, {5.1f, 2.0f, 0.0f}, {0.1f, 2.2f, 3.2f});
    addBox(room, {0.0f, 2.0f, -3.1f}, {5.2f, 2.2f, 0.1f});
    addBox(room, {0.0f, 2.0f, 3.1f}, {5.2f, 2.2f, 0.1f});
    const Acoustics::Room heard = measure(Colliders(room), {0.0f, 1.6f, 0.0f});
    check("a room of colliders reads shut", heard.enclosure > 0.99f);
    check("  and the size of the space inside them", heard.meanFreePath > 2.5f && heard.meanFreePath < 5.5f);
}

// The power of a mono signal over the tenth of a second around @p seconds, in decibels.
double decibelsAround(const std::vector<float>& signal, double seconds) {
    const auto from = static_cast<size_t>((seconds - 0.05) * RATE);
    const auto to   = static_cast<size_t>((seconds + 0.05) * RATE);
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += static_cast<double>(signal[i]) * signal[i];
    return 10.0 * std::log10(sum / static_cast<double>(to - from) + 1e-30);
}

std::vector<float> impulseThrough(float decaySeconds, float wet, float seconds) {
    ReverbTank tank;
    tank.prepare(RATE, 1);
    tank.setDecay(decaySeconds);
    tank.setWet(wet);
    std::vector<float> in(static_cast<size_t>(RATE * seconds), 0.0f);
    std::vector<float> out(in.size(), 0.0f);
    in[0] = 1.0f;
    tank.process(in.data(), out.data(), static_cast<uint32_t>(in.size()));
    return out;
}

void testTheReverbFadesAsTold() {
    std::printf("The room's reverberation:\n");

    // From 0.1 s to 1 s, a one-second decay falls 54 dB.
    const std::vector<float> second = impulseThrough(1.0f, 1.0f, 1.2f);
    const double fell = decibelsAround(second, 0.1) - decibelsAround(second, 1.0);
    std::printf("      a 1 s decay falls %.1f dB from 0.1 s to 1 s, against 54\n", fell);
    check("a tail falls about as fast as its decay says", fell > 42.0 && fell < 66.0);
    bool finite = true;
    float loudest = 0.0f;
    for (float s : second) {
        finite = finite && std::isfinite(s);
        loudest = std::max(loudest, std::abs(s));
    }
    check("  and never runs away", finite && loudest < 1.0f);

    const std::vector<float> longer = impulseThrough(3.0f, 1.0f, 1.2f);
    const double louder = decibelsAround(longer, 1.0) - decibelsAround(second, 1.0);
    check("a longer decay is louder a second on", louder > 10.0);

    const std::vector<float> dry = impulseThrough(1.0f, 0.0f, 0.2f);
    const bool silent = std::all_of(dry.begin(), dry.end(), [](float s) { return s == 0.0f; });
    check("a level of 0 rings nothing", silent);

    // A voice gone wrong for a sample - a position never set - is not every tail's from then on.
    ReverbTank tank;
    tank.prepare(RATE, 1);
    tank.setDecay(1.0f);
    tank.setWet(1.0f);
    std::vector<float> in(RATE / 10, 0.0f);
    std::vector<float> out(in.size(), 0.0f);
    in[0] = std::numeric_limits<float>::quiet_NaN();
    tank.process(in.data(), out.data(), static_cast<uint32_t>(in.size()));
    in[0] = 1.0f;
    tank.process(in.data(), out.data(), static_cast<uint32_t>(in.size()));
    const bool clean = std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s); });
    check("a sample that is not a number rings in nothing after it", clean);
}

constexpr bool STOPPED = true;

// One positioned tone in front of the ear, heard for a tenth of a second after @p skip frames;
// @p stopped, every voice is first stopped a tenth of a second in.
double heardThrough(
    const AudioClipAsset& tone,
    float occlusion,
    bool reverb,
    float roomLevel,
    uint64_t skip,
    bool stopped = false
) {
    AudioDevice device;
    device.openOffline(RATE, CHANNELS);
    placeEar(device);
    device.setRoomReverb(1.5f, roomLevel);
    VoiceParams params;
    params.spatial   = true;
    params.position  = {0.0f, 0.0f, -2.0f};
    params.occlusion = occlusion;
    params.reverb    = reverb;
    device.play(tone, params);
    if (stopped) {
        mix(device, RATE / 10);
        device.stopAllVoices();
    }
    if (skip > 0) mix(device, skip);
    const double heard = loudness(mix(device, RATE / 10));
    device.close();
    return heard;
}

void testAnOccludedVoiceIsQuieterAndDuller() {
    std::printf("A voice heard through a wall:\n");

    const AudioClipAsset low  = makeTone(1, 0.5f, 220.0);
    const AudioClipAsset high = makeTone(1, 0.5f, 6000.0);
    const double lowOpen  = heardThrough(low, 0.0f, false, 0.0f, 0);
    const double lowShut  = heardThrough(low, 1.0f, false, 0.0f, 0);
    const double highOpen = heardThrough(high, 0.0f, false, 0.0f, 0);
    const double highShut = heardThrough(high, 1.0f, false, 0.0f, 0);
    std::printf("      220 Hz kept %.2f, 6 kHz kept %.2f\n", lowShut / lowOpen, highShut / highOpen);
    check("an occluded voice is quieter", lowShut < 0.6 * lowOpen);
    check("  and loses its treble more than its bass", highShut / highOpen < 0.5 * (lowShut / lowOpen));

    // A voice rings in the room by running through it: the sound itself must come out whole.
    const double inTheRoom = heardThrough(low, 0.0f, true, 0.35f, 0);
    check("a voice in the room is heard whole while it plays", inTheRoom > 0.99 * lowOpen);

    // The tone is half a second; measured from 0.6 s, only a tail is left.
    const double ringing = heardThrough(low, 0.0f, true, 0.35f, RATE * 6 / 10);
    const double dry     = heardThrough(low, 0.0f, false, 0.35f, RATE * 6 / 10);
    const double off     = heardThrough(low, 0.0f, true, 0.0f, RATE * 6 / 10);
    std::printf("      after the tone: with the room %.5f, without %.5f, room off %.5f\n", ringing, dry, off);
    check("a voice in the room rings on after it ends", ringing > 1e-3);
    check("  one that does not ask for it does not", dry < 1e-5);
    check("  nor does any with the room's level at 0", off < 1e-5);
    const double stopped = heardThrough(low, 0.0f, true, 0.35f, RATE * 6 / 10, STOPPED);
    check("every voice stopped takes the room's tail along", stopped < 1e-5);
}

// The whole loop: a wall in the scene, the system's rays through it, the mixer's sound.
constexpr bool A_WALL     = true;
constexpr bool NO_WALL    = false;
constexpr bool SCENE_ON   = true;
constexpr bool SCENE_OFF  = false;
constexpr bool SOURCE_ON  = true;
constexpr bool SOURCE_OFF = false;

double heardInAScene(bool wall, bool sceneAcoustics, bool sourceAcoustics) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = sceneAcoustics;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});

    const EntityId hum = world.addSource(PLAY_ON_START);
    world.scene.get<Transform>(hum).position    = {0.0f, 0.0f, -4.0f};
    world.scene.get<AudioSource>(hum).loop      = true;
    world.scene.get<AudioSource>(hum).acoustics = sourceAcoustics;
    if (wall) addBox(world.scene, {0.0f, 0.0f, -2.0f}, {3.0f, 3.0f, 0.1f});

    for (int i = 0; i < 4; ++i) world.tick();
    return loudness(mix(world.audio.device(), RATE / 10));
}

void testAWallInTheSceneMufflesASoundBehindIt() {
    std::printf("A wall between the ear and a sound, in a scene:\n");

    const double open     = heardInAScene(NO_WALL, SCENE_ON, SOURCE_ON);
    const double walled   = heardInAScene(A_WALL, SCENE_ON, SOURCE_ON);
    const double sceneOff = heardInAScene(A_WALL, SCENE_OFF, SOURCE_ON);
    const double optedOut = heardInAScene(A_WALL, SCENE_ON, SOURCE_OFF);
    std::printf(
        "      open %.4f, behind the wall %.4f, scene off %.4f, source off %.4f\n",
        open,
        walled,
        sceneOff,
        optedOut
    );
    check("a sound behind a wall is heard muffled", walled < 0.6 * open);
    check("  and heard straight with the scene's acoustics off", std::abs(sceneOff / open - 1.0) < 0.05);
    check("  or by a source that opts out", std::abs(optedOut / open - 1.0) < 0.05);
}

// A hum behind a wall that is taken away while it plays: heard before, and once it has had time
// to ease.
struct HeardAsTheWallGoes {
    double walled = 0.0;
    double opened = 0.0;
};

HeardAsTheWallGoes heardAsTheWallGoes() {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});
    const EntityId hum = world.addSource(PLAY_ON_START);
    world.scene.get<Transform>(hum).position = {0.0f, 0.0f, -4.0f};
    world.scene.get<AudioSource>(hum).loop   = true;
    const EntityId wall = addBox(world.scene, {0.0f, 0.0f, -2.0f}, {3.0f, 3.0f, 0.1f});

    HeardAsTheWallGoes heard;
    for (int i = 0; i < 4; ++i) world.tick();
    heard.walled = loudness(mix(world.audio.device(), RATE / 10));
    world.scene.get<Collider>(wall).enabled = false;
    // Frames a tenth of a second long, so the easing runs its course in a few.
    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        world.tick();
    }
    heard.opened = loudness(mix(world.audio.device(), RATE / 10));
    return heard;
}

// What a playing sound hears is asked again as the world changes: a door opening is heard opening.
void testAPlayingSoundHearsTheWallGo() {
    std::printf("A wall taken away while a sound plays behind it:\n");

    const double open  = heardInAScene(NO_WALL, SCENE_ON, SOURCE_ON);
    const auto   heard = heardAsTheWallGoes();
    std::printf("      open %.4f, walled %.4f, once it is gone %.4f\n", open, heard.walled, heard.opened);
    check("a playing sound behind a wall is muffled", heard.walled < 0.6 * open);
    check("  and heard straight once the wall is gone", std::abs(heard.opened / open - 1.0) < 0.05);
}

// A long one-shot asked for in a world, heard a frame after that world is replaced in the same asset
// graph - the editor's Stop or restart - or after a frame of the same world.
double heardAfterTheWorld(bool replaced) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    const AudioClipHandle long_ = world.frame.resources.add(makeTone(1, 2.0f), "test:long");
    const auto placeEar = [&] {
        const EntityId ear = world.scene.createEntity();
        world.scene.add(ear, Transform{});
        world.scene.add(ear, AudioListener{});
    };
    placeEar();
    world.frame.events.enqueue(PlaySoundEvent::at(long_, {0.0f, 0.0f, -2.0f}));
    world.tick();
    world.tick();
    if (replaced) {
        world.scene.clear();
        placeEar();
    }
    world.tick();
    mix(world.audio.device(), RATE / 20);
    return loudness(mix(world.audio.device(), RATE / 10));
}

// Asked for as a world is replaced: one the old world asked for on its way out - a behavior's
// onDestroy at Stop - and one the new world asked for in its first frame. The voices it starts.
size_t voicesStartedAcrossAWorld() {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    const AudioClipHandle long_ = world.frame.resources.add(makeTone(1, 2.0f), "test:long");
    const auto placeEar = [&] {
        const EntityId ear = world.scene.createEntity();
        world.scene.add(ear, Transform{});
        world.scene.add(ear, AudioListener{});
    };
    placeEar();
    world.tick();
    world.frame.events.enqueue(PlaySoundEvent::at(long_, {0.0f, 0.0f, -2.0f}));
    world.frame.events.flush();
    world.scene.clear();
    placeEar();
    world.frame.events.enqueue(PlaySoundEvent::at(long_, {0.0f, 0.0f, -2.0f}));
    world.frame.events.flush();
    world.tick();
    return world.audio.device().voiceCount();
}

// A world replaced takes its sounds along, the one-shots it asked for too.
void testANewWorldStartsInSilence() {
    std::printf("A one-shot playing as its world is replaced:\n");

    const double kept     = heardAfterTheWorld(false);
    const double replaced = heardAfterTheWorld(true);
    std::printf("      the same world %.4f, a new one %.6f\n", kept, replaced);
    check("it plays on in the same world", kept > 0.01);
    check("  and stops with the world", replaced < 1e-5);
    check("what it asked for on its way out goes too, the new one's stays", voicesStartedAcrossAWorld() == 1);
}

// A request's tail in an open field, played the frame after a world shut in a room of colliders.
double tailAfterARoom() {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const auto placeEar = [&] {
        const EntityId ear = world.scene.createEntity();
        Transform at;
        at.position = {0.0f, 1.6f, 0.0f};
        world.scene.add(ear, at);
        world.scene.add(ear, AudioListener{});
    };
    placeEar();
    addBox(world.scene, {0.0f, -0.1f, 0.0f}, {5.2f, 0.1f, 3.2f});
    addBox(world.scene, {0.0f, 4.1f, 0.0f}, {5.2f, 0.1f, 3.2f});
    addBox(world.scene, {-5.1f, 2.0f, 0.0f}, {0.1f, 2.2f, 3.2f});
    addBox(world.scene, {5.1f, 2.0f, 0.0f}, {0.1f, 2.2f, 3.2f});
    addBox(world.scene, {0.0f, 2.0f, -3.1f}, {5.2f, 2.2f, 0.1f});
    addBox(world.scene, {0.0f, 2.0f, 3.1f}, {5.2f, 2.2f, 0.1f});
    // Long enough for the window to fill and the reverb to ease well into the room's.
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        world.tick();
    }

    // Replaced as a level change replaces it: its assets swapped, so every sound stops.
    world.scene.clear();
    world.audio.stopEverything();
    world.scene.environment().acoustics.enabled = true;
    placeEar();
    world.frame.events.enqueue(PlaySoundEvent::at(world.clip, {0.0f, 1.6f, -2.0f}));
    world.tick();
    world.tick();
    mix(world.audio.device(), RATE * 4 / 10);
    return loudness(mix(world.audio.device(), RATE / 10));
}

// A world replaced takes its room along: the next one's first sounds ring in their own.
void testANewWorldRingsInNoOldRoom() {
    std::printf("A sound the frame after its world replaced one shut in a room:\n");

    const double tail = tailAfterARoom();
    std::printf("      its tail %.6f\n", tail);
    check("it rings with none of the old room's reverb", tail < 1e-5);
}

// A sound asked for far from the ear, past the room rays' reach, with or without a wall beside it.
double heardFromAfar(bool wall, bool shaped = true) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});

    const float at = Acoustics::ROOM_REACH + 10.0f;
    if (wall) addBox(world.scene, {at - 2.0f, 0.0f, 0.0f}, {0.1f, 4.0f, 4.0f});
    VoiceParams distant;
    distant.spatial     = true;
    distant.position    = {at, 0.0f, 0.0f};
    distant.minDistance = 2.0f * at;  // as loud there as beside the ear
    distant.maxDistance = 3.0f * at;
    // Two ticks: the bus delivers at the end of one frame, and the next starts it.
    PlaySoundEvent asked{world.clip, distant};
    asked.acoustics = shaped;
    world.frame.events.enqueue(asked);
    world.tick();
    world.tick();
    return loudness(mix(world.audio.device(), RATE / 10));
}

// The colliders gathered for the rays reach every sound asked for, not only the ear's room.
void testAFarSoundIsMuffledByAWallBesideIt() {
    std::printf("A sound asked for far from the ear, a wall beside it:\n");

    const double open     = heardFromAfar(NO_WALL);
    const double walled   = heardFromAfar(A_WALL);
    const double optedOut = heardFromAfar(A_WALL, SOURCE_OFF);
    std::printf("      open %.4f, walled %.4f, opted out %.4f\n", open, walled, optedOut);
    check("a far sound behind a wall is heard muffled", open > 0.0 && walled < 0.6 * open);
    check("  and heard straight by a request that opts out", std::abs(optedOut / open - 1.0) < 0.05);
}

// A source starting this frame, past the ear's room, a wall beside it: shaped as it starts.
double heardStartingFromAfar(bool wall) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});

    const float at = Acoustics::ROOM_REACH + 10.0f;
    if (wall) addBox(world.scene, {at - 2.0f, 0.0f, 0.0f}, {0.1f, 4.0f, 4.0f});
    const EntityId hum = world.addSource(PLAY_ON_START);
    world.scene.get<Transform>(hum).position      = {at, 0.0f, 0.0f};
    world.scene.get<AudioSource>(hum).loop        = true;
    world.scene.get<AudioSource>(hum).minDistance = 2.0f * at;  // as loud there as beside the ear
    world.scene.get<AudioSource>(hum).maxDistance = 3.0f * at;
    // The frame it starts, before a second could re-ask it.
    world.tick();
    return loudness(mix(world.audio.device(), RATE / 50));
}

void testAFarSourceStartsAsMuffledAsItIs() {
    std::printf("A sound starting far from the ear, a wall beside it:\n");

    const double open   = heardStartingFromAfar(NO_WALL);
    const double walled = heardStartingFromAfar(A_WALL);
    std::printf("      open %.4f, walled %.4f\n", open, walled);
    check("a far source behind a wall starts muffled", open > 0.0 && walled < 0.6 * open);
}

// A request near the ear, a wall between them.
double heardThroughAWall(bool shaped) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});
    addBox(world.scene, {0.0f, 0.0f, -2.0f}, {3.0f, 3.0f, 0.1f});

    VoiceParams behind;
    behind.spatial     = true;
    behind.position    = {0.0f, 0.0f, -4.0f};
    behind.minDistance = 8.0f;
    behind.maxDistance = 50.0f;
    PlaySoundEvent asked{world.clip, behind};
    asked.acoustics = shaped;
    world.frame.events.enqueue(asked);
    world.tick();
    world.tick();
    return loudness(mix(world.audio.device(), RATE / 10));
}

void testARequestCanCutThrough() {
    std::printf("A request behind a wall, shaped or not:\n");

    const double shaped  = heardThroughAWall(SOURCE_ON);
    const double through = heardThroughAWall(SOURCE_OFF);
    std::printf("      shaped %.4f, opted out %.4f\n", shaped, through);
    const bool cuts = through > 0.0 && shaped < 0.6 * through;
    check("a request behind a wall is muffled, and heard straight opted out", cuts);
}

constexpr bool NO_BODY      = false;
constexpr bool ITS_BODY     = true;
constexpr int  ON_ITSELF    = 0;
constexpr int  ON_A_CHILD   = 1;
constexpr int  ON_A_TRIGGER = 2;  ///< A child of the body with a trigger of its own
constexpr bool A_SOURCE     = false;
constexpr bool A_REQUEST    = true;

// A hum in plain view, inside a static body of its own - on the body's entity, or on a child of it -
// or a one-shot from the body's entity, as Behavior::playSound sends it.
double heardInsideItsBody(bool body, int on, bool asked = A_SOURCE) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});

    const EntityId machine = world.scene.createEntity();
    Transform at;
    at.position = {0.0f, 0.0f, -4.0f};
    world.scene.add(machine, at);
    if (body) {
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = glm::vec3(1.0f);
        Collider shape;
        shape.parts = {part};
        world.scene.add(machine, std::move(shape));
        Rigidbody fixed;
        fixed.motion = RigidbodyMotion::Static;
        world.scene.add(machine, std::move(fixed));
    }
    // On the body's entity, or on a child of it, its world pose as the hierarchy resolves it.
    EntityId hum = machine;
    if (on != ON_ITSELF) {
        hum = world.scene.createEntity();
        world.scene.add(hum, Transform{});
        world.scene.add(hum, WorldTransform{glm::translate(glm::mat4(1.0f), at.position)});
        HierarchyOperations::setParent(world.scene, hum, machine);
    }
    if (on == ON_A_TRIGGER) {
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = glm::vec3(0.2f);
        Collider trigger;
        trigger.parts     = {part};
        trigger.isTrigger = true;
        world.scene.add(hum, std::move(trigger));
        Rigidbody carried;
        carried.motion = RigidbodyMotion::Kinematic;
        world.scene.add(hum, std::move(carried));
    }
    if (asked) {
        PlaySoundEvent request = PlaySoundEvent::at(world.clip, at.position);
        request.from           = machine;
        world.frame.events.enqueue(request);
        world.tick();
        world.tick();
        return loudness(mix(world.audio.device(), RATE / 10));
    }
    AudioSource source;
    source.clip        = world.clip;
    source.playOnStart = true;
    source.loop        = true;
    world.scene.add(hum, std::move(source));

    for (int i = 0; i < 4; ++i) world.tick();
    return loudness(mix(world.audio.device(), RATE / 10));
}

// The body a sound sits in does not muffle it, however thick.
void testASoundIsNotMuffledByItsOwnBody() {
    std::printf("A sound inside the body it belongs to:\n");

    const double open    = heardInsideItsBody(NO_BODY, ON_ITSELF);
    const double inside  = heardInsideItsBody(ITS_BODY, ON_ITSELF);
    const double carried = heardInsideItsBody(ITS_BODY, ON_A_CHILD);
    const double trigger = heardInsideItsBody(ITS_BODY, ON_A_TRIGGER);
    std::printf(
        "      open %.4f, inside %.4f, on a child %.4f, on a trigger %.4f\n",
        open,
        inside,
        carried,
        trigger
    );
    const bool straight = open > 0.0 && std::abs(inside / open - 1.0) < 0.05;
    check("a sound inside its own body is heard straight", straight);
    check("  and one on a child of the body", std::abs(carried / open - 1.0) < 0.05);
    check("  even a child with a trigger of its own", std::abs(trigger / open - 1.0) < 0.05);

    const double openAsked   = heardInsideItsBody(NO_BODY, ON_ITSELF, A_REQUEST);
    const double insideAsked = heardInsideItsBody(ITS_BODY, ON_ITSELF, A_REQUEST);
    std::printf("      asked: open %.4f, inside %.4f\n", openAsked, insideAsked);
    check("  and one asked for from it", openAsked > 0.0 && std::abs(insideAsked / openAsked - 1.0) < 0.05);
}

constexpr bool POSITIONED = true;

// A request carrying its own occlusion, near the ear with nothing between.
double heardAsked(bool spatial, float occlusion) {
    SilentAudioWorld world;
    world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = true;

    const EntityId ear = world.scene.createEntity();
    world.scene.add(ear, Transform{});
    world.scene.add(ear, AudioListener{});

    VoiceParams asked;
    asked.spatial     = spatial;
    asked.position    = {0.0f, 0.0f, -1.0f};
    asked.minDistance = 5.0f;
    asked.maxDistance = 50.0f;
    asked.occlusion   = occlusion;
    world.frame.events.enqueue(PlaySoundEvent{world.clip, asked});
    world.tick();
    world.tick();
    return loudness(mix(world.audio.device(), RATE / 10));
}

// What shapes a request is AudioSystem's to say, whatever the request carried.
void testARequestCannotMuffleItself() {
    std::printf("A request carrying an occlusion of its own:\n");

    const double flat        = heardAsked(NON_SPATIAL, 0.0f);
    const double flatAsked   = heardAsked(NON_SPATIAL, 1.0f);
    const double placed      = heardAsked(POSITIONED, 0.0f);
    const double placedAsked = heardAsked(POSITIONED, 1.0f);
    std::printf("      flat %.4f / %.4f, placed %.4f / %.4f\n", flat, flatAsked, placed, placedAsked);
    const bool flatOpen   = flat > 0.0 && std::abs(flatAsked / flat - 1.0) < 0.05;
    const bool placedOpen = placed > 0.0 && std::abs(placedAsked / placed - 1.0) < 0.05;
    check("a flat request is never shaped", flatOpen);
    check("  and a placed one only by what stands between", placedOpen);
}

constexpr bool SHOWN  = true;
constexpr bool HIDDEN = false;

// What a frame of the acoustics view drew.
struct DrawnAcoustics {
    size_t lines   = 0;  ///< Every segment
    size_t fromEar = 0;  ///< Of them, those leaving the ear
    size_t stopped = 0;  ///< In the color of a ray stopped short of its sound
};

// The ear in a room of colliders, a wall across it between the ear and a hum, a few frames on.
DrawnAcoustics drawnInARoom(bool shown, bool sceneAcoustics, bool device = true) {
    SilentAudioWorld world;
    if (device) world.audio.device().openOffline(RATE, CHANNELS);
    world.scene.environment().acoustics.enabled = sceneAcoustics;
    DebugDraw draw;
    draw.setShown(DebugCategory::Acoustics, shown);
    world.frame.ctx.debug = &draw;

    addBox(world.scene, {0.0f, -0.1f, 0.0f}, {5.2f, 0.1f, 3.2f});
    addBox(world.scene, {0.0f, 4.1f, 0.0f}, {5.2f, 0.1f, 3.2f});
    addBox(world.scene, {-5.1f, 2.0f, 0.0f}, {0.1f, 2.2f, 3.2f});
    addBox(world.scene, {5.1f, 2.0f, 0.0f}, {0.1f, 2.2f, 3.2f});
    addBox(world.scene, {0.0f, 2.0f, -3.1f}, {5.2f, 2.2f, 0.1f});
    addBox(world.scene, {0.0f, 2.0f, 3.1f}, {5.2f, 2.2f, 0.1f});
    addBox(world.scene, {0.0f, 2.0f, -1.0f}, {5.0f, 2.0f, 0.1f});

    const glm::vec3 earAt(0.0f, 1.6f, 1.5f);
    const EntityId  ear = world.scene.createEntity();
    world.scene.add(ear, Transform{earAt});
    world.scene.add(ear, AudioListener{});
    const EntityId hum = world.addSource(PLAY_ON_START);
    world.scene.get<Transform>(hum).position = {0.0f, 1.6f, -2.0f};
    world.scene.get<AudioSource>(hum).loop   = true;

    // The last frame's alone, as a host collects each.
    for (int i = 0; i < 4; ++i) {
        world.tick();
        draw.collect(0.0f, 0.0f, world.scene.epoch());
        draw.endFrame();
    }

    DrawnAcoustics drawn;
    const uint32_t stopped = glm::packUnorm4x8(DebugPalette::OCCLUDED.value());
    for (const DebugLine& line : draw.lines()) {
        ++drawn.lines;
        if (glm::distance(glm::vec3(line.a), earAt) < 1e-4f) ++drawn.fromEar;
        if (line.color == stopped) ++drawn.stopped;
    }
    return drawn;
}

void testTheAcousticsViewDrawsTheRaysCast() {
    std::printf("The acoustics, drawn:\n");

    const DrawnAcoustics on = drawnInARoom(SHOWN, SCENE_ON);
    std::printf(
        "      %zu segments, %zu from the ear, %zu stopped short\n",
        on.lines,
        on.fromEar,
        on.stopped
    );
    const size_t window = 4 * Acoustics::ROOM_RAYS_PER_FRAME;
    check("the room's rays and the sound's leave the ear", on.fromEar == window + Acoustics::OCCLUSION_RAYS);
    // A line up to what stopped it and a dot there, for each.
    check("  and a wall between stops each ray to the sound", on.stopped == 2 * Acoustics::OCCLUSION_RAYS);
    check("nothing while the host does not show them", drawnInARoom(HIDDEN, SCENE_ON).lines == 0);
    check("  or with the scene's acoustics off", drawnInARoom(SHOWN, SCENE_OFF).lines == 0);
    check("  or on a host with no sound to hear", drawnInARoom(SHOWN, SCENE_ON, false).lines == 0);
}

} // namespace

void runAudioTests() {
    testTheOfflineMixerMakesSound();
    testAPositionedSourceIsHeardFromWhereItIs();
    testAStereoClipLeansButNeverCrosses();
    testDistanceTakesTheSoundAway();
    testWithNoEarASpatialSourceIsSilentAndAFlatOneIsNot();
    testOneBadGainCannotTakeTheWholeMixWithIt();
    testAPitchOfZeroDoesNotStopTheClock();
    testAVoiceIsHeldAtItsCursorRatherThanRestarted();
    testTheVoiceBudgetMakesRoomByPriority();
    testStoppingASoundDoesNotClickAndSeekingStaysInTheClip();

    testASourceIsOnlyStartedByTimeMoving();
    testARequestTooFarToHearDoesNotSpendAVoice();
    testASilentHostRunsTheWholeSystemAnyway();
    testASourceWithNothingToPlayStopsAsking();
    testASourceWithNoPoseIsStillHeard();
    testAVoiceDoesNotOutliveTheEntityThatOwnedIt();
    testAPlayingSourceRePointedAtAnotherClipPlaysIt();
    testTheEarIsTheLowestSlotListener();
    testAnInverseRolloffFallsAsOneOverDistanceToSilence();
    testALoopGoesBackToTheClipsLoopStart();

    testTheRaysTellAShutRoomFromOpenGround();
    testARoomRayKeepsTheWayItWent();
    testTheRaysToASoundAreTheOnesItsShareCounts();
    testASoundIsNotMuffledByWhatItSitsOn();
    testAWallStopsASoundAndADoorwayLetsItThrough();
    testTheEasingIsTheSameAtAnyFramerate();
    testTheColliderWorldIsHeardAsDrawn();
    testTheReverbFadesAsTold();
    testAnOccludedVoiceIsQuieterAndDuller();
    testAWallInTheSceneMufflesASoundBehindIt();
    testAPlayingSoundHearsTheWallGo();
    testANewWorldRingsInNoOldRoom();
    testANewWorldStartsInSilence();
    testAFarSoundIsMuffledByAWallBesideIt();
    testAFarSourceStartsAsMuffledAsItIs();
    testARequestCannotMuffleItself();
    testARequestCanCutThrough();
    testASoundIsNotMuffledByItsOwnBody();
    testTheAcousticsViewDrawsTheRaysCast();
}

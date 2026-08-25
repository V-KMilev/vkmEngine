#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace Vkm::Engine {

struct AudioClipAsset;

/**
 * @brief Identifies one playing sound; 0 is the null sentinel, like a handle index.
 */
using VoiceId = uint32_t;

/**
 * @brief Everything the mixer needs to know about one voice, pushed every frame.
 *
 * A plain snapshot rather than a reference to the component, because the device
 * must not know what an ECS is - it is the only part of the engine allowed to
 * see the audio backend, and keeping it ignorant of the scene is what makes the
 * seam hold in both directions.
 */
struct VoiceParams {
    /**
     * @brief Linear gain for this voice, multiplied into the master.
     *
     * Negative and non-finite values are heard as silence. That is not
     * tidiness: every voice sums into one master, so a single infinite gain
     * takes the whole mix non-finite and silences every other sound until
     * the voice is reaped. Gameplay fills this in arithmetic nobody checks -
     * a PlaySoundEvent carries whatever the caller computed - so the floor
     * lives here, where every path already passes through.
     */
    float     volume      = 1.0f;
    float     pitch       = 1.0f;
    bool      loop        = false;
    bool      spatial     = true;
    glm::vec3 position    = {0.0f, 0.0f, 0.0f};
    float     minDistance = 1.0f;
    float     maxDistance = 50.0f;
};

/**
 * @brief The engine's whole surface onto the audio backend.
 *
 * This class is the seam: it is the only place in the engine that includes
 * miniaudio's header, and nothing above it - not the components, not the
 * system's callers, not gameplay - has any way to reach the backend. Swapping
 * the backend is re-implementing this file.
 *
 * The backend owns the output device, the audio thread and its callback,
 * mixing, resampling, format conversion, the spatialization maths and each
 * voice's playback cursor. The engine owns everything with a name: which
 * clips exist (ResourceManager), which entities want to be heard (AudioSource),
 * where the ear is (AudioListener), and the lifetime of every voice - all of
 * which is driven from the main thread by AudioSystem, never from the mixer.
 *
 * A voice plays a clip's samples in place rather than copying them, so fifty
 * footsteps cost fifty cursors and no duplicate PCM. It shares ownership of
 * those samples for as long as it lives, because the mixer reads them from the
 * audio thread while a scene load frees assets from the main one; that is what
 * makes a sound outlive the graph it came from instead of reading freed memory
 * until AudioSystem next runs.
 *
 * No device is a normal state. On a headless cooker, a CI box, or a machine
 * whose driver is broken, open() returns false, isOpen() stays false, and every
 * other call becomes a no-op that costs a branch. The engine runs silently
 * rather than refusing to run, and says so once instead of once a frame.
 *
 * All of this, render() included, is main-thread only. Nothing here is guarded,
 * and it does not need to be: every engine call arrives from
 * AudioSystem::update or from an editor panel, both on the main thread, while
 * the mixer thread is miniaudio's own and reaches back only through the log
 * bridge. A second thread calling render() while the main one calls close()
 * reads a graph being torn down, which is a segfault rather than a wrong sample
 * - so a harness that lends the offline mixer a thread must join it before
 * closing the device. A lock would put a mutex in the frame's hot path to serve
 * a caller the engine does not have.
 *
 * Nothing outside vkm_core and vkm_cook can reach miniaudio at all: it is
 * linked privately, so its include path stops at those two targets and the
 * editor, the backend and gameplay cannot name the header even if they tried.
 * Inside them the path is target-wide, so "one file" describes this file's
 * discipline rather than something the build enforces.
 *
 * The known races are miniaudio's, and a vendored backend's bugs are ours to
 * carry. ThreadSanitizer reports two of them on every run that mixes while the
 * main thread pushes voice parameters: ma_gainer::masterVolume (a plain float,
 * written here by ma_sound_set_volume from apply(), read by the mixer in
 * ma_gainer_process_pcm_frames_internal) and ma_spatializer_listener::isEnabled
 * (a plain ma_bool32, written by setListenerActive, read by the mixer). A third
 * joins them only while something reads a playback cursor:
 * ma_audio_buffer_ref::cursor is a plain ma_uint64 the mixer advances and
 * voiceCursor() reads, and its one caller is the editor's audition card, where
 * a value a frame stale is a slider a pixel behind. Seeking is not part of
 * that: miniaudio hands a seek to the mixer through an atomic on purpose, which
 * is why seekVoice() is safe to call from here at all. All three are single
 * aligned scalars with no invariant spanning them, and miniaudio uses
 * ma_atomic_float for exactly this kind of field elsewhere -
 * ma_engine_node::volume and ma_device::masterVolumeFactor are both atomic - so
 * they read as oversights upstream rather than a design. They are left alone
 * deliberately: patching them means carrying a fork of the backend, and
 * quieting some of them from this side would hide the rest.
 */
class AudioDevice {
    public:
        AudioDevice();
        ~AudioDevice();

        AudioDevice(const AudioDevice& other) = delete;
        AudioDevice& operator=(const AudioDevice& other) = delete;

        AudioDevice(AudioDevice && other) = delete;
        AudioDevice& operator=(AudioDevice && other) = delete;

    public:
        /**
         * @brief Open the default playback device and start its mixer thread.
         *
         * The backend list deliberately excludes the null backend, which would
         * otherwise succeed on a machine with no audio hardware and hide the
         * fact behind a thread that mixes into nowhere. Failing here is what
         * makes "this host has no sound" a state the engine can see, report and
         * be tested against.
         *
         * @return True when a device opened; false leaves the engine silent.
         */
        bool open();

        /**
         * @brief Open the mixer with no device, to be pumped by render().
         *
         * The same graph, the same spatializer, the same voices - only the
         * output goes to the caller's buffer instead of to hardware. This exists
         * because the audible half of audio is not machine-checkable and the
         * measurable half only becomes so if something can read the signal: the
         * audio harness opens the mixer this way to prove that a source is
         * positioned where its entity is and that attenuation falls off with
         * distance. It is also how a CI box exercises the whole audio path
         * without hardware.
         *
         * @param sampleRate Mix rate; the caller's buffer is at this rate.
         * @param channels Output channel count (2 to hear panning at all).
         * @return True when the mixer initialised.
         */
        bool openOffline(uint32_t sampleRate, uint32_t channels);

        /**
         * @brief Stop every voice and shut the mixer down.
         *
         * Idempotent: a device that is already closed absorbs the call.
         */
        void close();

        bool isOpen() const noexcept { return m_open; }

        /**
         * @brief Pull mixed frames from an offline mixer into @p frames.
         *
         * Only meaningful after openOffline(); a device-backed mixer is pumped
         * by its own thread and answers 0 here.
         *
         * @param frames Destination for interleaved 32-bit float samples; must
         *        hold frameCount * the channel count openOffline() was given.
         * @param frameCount Frames to mix.
         * @return Frames actually written.
         */
        uint64_t render(float* frames, uint64_t frameCount);

        /**
         * @brief Start playing @p clip and return the voice driving it.
         *
         * The clip's samples are read where they lie, so it must stay alive and
         * unmoved until the voice is stopped.
         *
         * @param clip Decoded clip to play; an empty one yields no voice.
         * @param params Initial gain, pitch, looping and position.
         * @return The new voice, or 0 if the device is closed or the sound
         *         could not be created.
         */
        VoiceId play(const AudioClipAsset& clip, const VoiceParams& params);

        /**
         * @brief Push a voice's parameters to the mixer.
         *
         * Called every frame for every live voice; an unknown or finished voice
         * is ignored.
         */
        void updateVoice(VoiceId voice, const VoiceParams& params);

        /**
         * @brief Whether @p voice is still there with audio left to play.
         *
         * False once a one-shot has reached its end, which is how AudioSystem
         * learns that a sound finished on its own. A looping voice never
         * reports false until it is stopped.
         *
         * A paused voice answers true. It is holding a place in a clip it has
         * not finished, which is why this asks whether the voice is still
         * there rather than whether sound is coming out of it - isVoicePaused
         * answers the second half, and the two together are the only three
         * states a caller can see: gone, sounding, held.
         */
        bool isVoiceActive(VoiceId voice) const;

        /**
         * @brief Hold @p voice at its cursor, silently, without ending it.
         *
         * The mixer keeps the sound and only its clock stops, so resuming
         * continues from the sample the pause landed on: a held voice is a
         * place in a clip rather than a sound that has to be started again.
         *
         * Ramped over the same few milliseconds a stop is, for the same
         * measured reason - a pause is a cut at whatever sample the cursor
         * happens to be on. Swept across the phases of a 220 Hz tone at gain
         * 0.8, cutting outright steps by 0.80, thirty-five times the
         * waveform's own steepest sample-to-sample step, at the pause AND
         * again at the resume; ramped, both stay inside that own step. What
         * it costs is that the pause lands about ten milliseconds late, the
         * sound playing through its own fade, so the cursor moves that far
         * before it stops.
         *
         * A voice pauseAllVoices() is already holding changes hands rather
         * than being ramped a second time, so the world resuming underneath it
         * leaves it where this call put it. An unknown voice is ignored.
         */
        void pauseVoice(VoiceId voice);

        /**
         * @brief Let @p voice play on from where it was held.
         *
         * Ramped back in over the same few milliseconds, so the resume is no
         * more of an edge than the pause was. Lets go of a voice the transport
         * is holding as readily as one pauseVoice() held: this is the caller
         * saying it wants that voice heard, whoever stopped it. A voice that is
         * not held, and an unknown one, are ignored.
         */
        void resumeVoice(VoiceId voice);

        /**
         * @brief Whether @p voice is being held by a pause rather than playing.
         */
        bool isVoicePaused(VoiceId voice) const;

        /**
         * @brief How far into its clip @p voice has played, in seconds.
         *
         * This is why AudioSource carries no playback position, unlike
         * Animator::time, whose inspector card looks much the same. An
         * animation's time is component state: nothing advances it but the
         * system that reads it, both on this thread, so the component can BE
         * the position and a scrubber writes it directly. A voice's cursor is
         * advanced by the mixer thread between our frames, at the device's own
         * rate, so a field mirroring it would be a copy of a number that
         * changes without us. Whichever way that copy was pushed each frame,
         * one side would lose: writing the component from the device throws a
         * scrub away the moment it is made, and writing the device from the
         * component re-seeks the mixer to a frame-old position sixty times a
         * second, which is a stutter rather than a sound. Telling those two
         * apart needs a dirty flag, and that is a synchronisation problem that
         * would outlive whoever added it. Reading the device is a read of the
         * only copy there is.
         *
         * It also settles what a scrub means when nothing is playing: there
         * is no cursor, so the editor offers none. A mirrored field would
         * have had to invent an answer and remember it somewhere.
         *
         * @param voice Voice to read; an unknown or finished one answers 0.
         * @return Seconds from the clip's start, counting a seek that has been
         *         asked for but not yet applied by the mixer.
         */
        float voiceCursor(VoiceId voice) const;

        /**
         * @brief Move @p voice's cursor to @p seconds into its clip.
         *
         * Safe while the voice plays and while it is paused: the seek is
         * handed to the mixer as a target it applies before its next read, and
         * voiceCursor() reports that target from the moment it is asked for,
         * so a scrubber reads back what it just wrote rather than where the
         * sound was a frame ago. A paused voice keeps the seek and starts from
         * it when it resumes.
         *
         * Clamped to the clip here rather than passed through, because
         * miniaudio refuses an out-of-range seek at the data source and still
         * moves the sound's own clock to the position it refused - measured,
         * a voice left playing from where it was with a time nine seconds in
         * the future. Seeking to the very end is allowed and does what playing
         * to the end does: the voice finishes, and the next reap releases it.
         *
         * @param voice Voice to move; unknown ids are ignored.
         * @param seconds Position from the clip's start; negative and
         *        non-finite values land at 0.
         */
        void seekVoice(VoiceId voice, float seconds);

        /**
         * @brief Ramp @p voice to silence and let it go.
         *
         * The ramp is a few milliseconds and exists because releasing a sound
         * outright cuts its waveform at whatever sample the cursor is on, which
         * is a click at up to the signal's full amplitude - and three of the
         * four things that stop a voice (a source's entity destroyed, its
         * component removed, the editor's audition Stop) have no caller left to
         * fade it themselves.
         *
         * Returns at once; the voice is silent within the ramp and released by
         * the next reapFinishedVoices(). Until then the id answers exactly as
         * it did when this released outright - unknown to updateVoice, finished
         * to isVoiceActive - so nothing above has a second state to know
         * about. What it does mean is that a voice restarted on the following
         * frame briefly overlaps the tail of the one it replaced, which is the
         * crossfade it sounds like rather than a fault.
         *
         * A held voice is stopped like any other, and stops being held: it is
         * silent already, so it is let go on the next reap rather than ramped
         * again, and the pause does not keep it out of that sweep.
         *
         * @param voice Voice to stop; unknown ids are ignored.
         */
        void stopVoice(VoiceId voice);

        /**
         * @brief Release every voice that has played to its end.
         *
         * A one-shot leaves a finished sound sitting in the mixer until someone
         * stops it, and not every caller has somewhere to keep the id: the
         * editor's clip audition plays a file with no entity behind it and
         * nothing to reconcile it against later. Sweeping here means play() is
         * safe for a caller that never looks back, without a second lifetime
         * rule for one kind of voice.
         *
         * Safe for the voices AudioSystem does track, because ids are never
         * reused and stopping one twice is a no-op - a source whose voice was
         * swept still reads as finished on the next reconcile.
         *
         * It is also what releases a voice stopVoice() ramped out, so a mixer
         * nobody ever reaps holds those until it is closed. A device-backed one
         * is reaped every frame by AudioSystem; an offline one is reaped by
         * whoever pumps it.
         *
         * Finished is decided against the mixer's own clock, which advances a
         * device period at a time, so a ramped voice is freed some way after
         * it fell silent - 19 to 74 ms here, against PulseAudio. Inaudible for
         * all of it, and bounded: a source toggled every frame at 60 Hz holds
         * five voices rather than one.
         */
        void reapFinishedVoices();

        /**
         * @brief Cut and release every voice at once, leaving the device open.
         *
         * The answer to a scene load: the clips the voices are reading are
         * about to be freed, so nothing may still be pointing at them.
         *
         * Deliberately not ramped, unlike stopVoice(). This is the teardown
         * path - close() uninitialises the mixer on the next line, so a ramp
         * scheduled here would never be mixed at all, and the scene load that
         * calls it has already replaced the world those sounds belonged to.
         */
        void stopAllVoices();

        /**
         * @brief Hold every voice that is sounding right now.
         *
         * The editor's transport, and nothing in the engine. Pausing a shipped
         * game must not cut its music, silence a menu or swallow a UI click,
         * and AudioSystem is written so that it does not - see its header. The
         * editor's pause is a different pause wearing the same word: there the
         * world was frozen deliberately to be looked at, and a level's ambience
         * playing on underneath it is noise nobody asked for. So the editor
         * holds the voices itself, through the same device handle it already
         * auditions clips with, and no system in the engine learns that an
         * editor exists.
         *
         * Exactly the voices live at the call, deliberately. One started
         * afterwards plays: the only caller that starts a voice while the world
         * is frozen is the editor auditioning a clip, and not being able to
         * hear a file because the world is paused would be the same mistake
         * pointing the other way.
         */
        void pauseAllVoices();

        /**
         * @brief Let every voice pauseAllVoices() is still holding play on.
         *
         * Only those. A voice held on its own - an audition stopped to look at
         * one moment of a clip - stays where it was put, because that hold was
         * about the clip rather than about the world, and so does one that was
         * held here and then asked for by pauseVoice(). A voice stopped while
         * it was held is not started again either: stopping hands the hold
         * back, so what this resumes is only what is still waiting.
         */
        void resumeAllVoices();

        /**
         * @brief Number of voices the mixer currently holds.
         *
         * Which includes any still ramping out of a stopVoice(), and any being
         * held by a pause, because that is what the mixer is holding - the
         * first are inaudible but not yet freed, the second are waiting.
         */
        size_t voiceCount() const;

        /**
         * @brief Place the ear.
         *
         * @param position World position of the listener.
         * @param forward World-space direction the listener faces (+Z in this
         *        engine's convention).
         * @param up World-space up for the listener.
         */
        void setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

        /**
         * @brief Whether anything is listening.
         *
         * With no listener there is nothing for a distance to be measured from,
         * so spatial voices go silent while 2D ones (music, UI) play on
         * untouched - the backend's own answer to a disabled listener, and
         * exactly the contract the engine wants. Voices keep running either
         * way, so a looping ambience is audible again the moment a listener
         * appears rather than having to be restarted.
         */
        void setListenerActive(bool active);

        /**
         * @brief Master gain applied to the whole mix.
         *
         * A linear gain, passed through as written. Anything not finite, and
         * anything below zero, is heard as silence rather than as itself: an
         * infinite master takes every sample of the mix non-finite, which is
         * every sound in the game gone rather than one gain being wrong.
         *
         * @param volume Linear master gain; 0 and below silence the mix.
         */
        void setMasterVolume(float volume);

        /**
         * @brief The master gain the mix is running at.
         *
         * What setMasterVolume last took, after its own sanitising, so a caller
         * reads the gain the mixer has rather than the number somebody asked
         * for. Zero is the one value worth asking about: every voice is silent
         * however loud it was asked to be, and an editor has to be able to say
         * that out loud - a clip auditioned under a muted mix shows a running
         * cursor and cannot be heard, which reads as a broken file.
         *
         * @return Linear master gain; 1 until something sets it otherwise.
         */
        float masterVolume() const noexcept { return m_masterVolume; }

    private:
        // Everything backend-shaped lives behind this: ma_engine, the voice
        // table and the ma_sound / ma_audio_buffer_ref pair each voice owns.
        // Held by pointer so miniaudio's header stays out of this one.
        struct Backend;

    private:
        std::unique_ptr<Backend> m_backend;
        bool                     m_open = false;
        float                    m_masterVolume = 1.0f;
};

} // namespace Vkm::Engine

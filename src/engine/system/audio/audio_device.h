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
 * What lives on each side of that line is worth stating, because it is what the
 * design is. The backend owns the output device, the audio thread and its
 * callback, mixing, resampling, format conversion, the spatialization maths and
 * each voice's playback cursor. The engine owns everything with a name: which
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
 * NO DEVICE IS A NORMAL STATE. A headless cooker, a CI box, a machine whose
 * driver is broken: open() returns false, isOpen() stays false, and every other
 * call becomes a no-op that costs a branch. The engine runs silently rather
 * than refusing to run, and says so once instead of once a frame.
 *
 * WHICH THREAD MAY CALL THIS. All of it, render() included, is main-thread
 * only. Nothing here is guarded, and it does not need to be: every engine call
 * arrives from AudioSystem::update or from an editor panel, both on the main
 * thread, while the mixer thread is miniaudio's own and reaches back only
 * through the log bridge. A second thread calling render() while the main one
 * calls close() reads a graph being torn down, which is a segfault rather than
 * a wrong sample - so a harness that lends the offline mixer a thread must
 * join it before closing the device. A lock would put a mutex in the frame's
 * hot path to serve a caller the engine does not have.
 *
 * WHAT THE SEAM IS, EXACTLY. Nothing outside vkm_core and vkm_cook can reach
 * miniaudio at all: it is linked privately, so its include path stops at those
 * two targets and the editor, the backend and gameplay cannot name the header
 * even if they tried. Inside them the path is target-wide, so "one file"
 * describes this file's discipline rather than something the build enforces.
 *
 * THE TWO KNOWN RACES ARE MINIAUDIO'S, and they are worth naming because a
 * vendored backend's bugs are ours to carry. ThreadSanitizer reports both
 * every run of scratchpad/adv_audio/race_check.cpp: ma_gainer::masterVolume
 * (a plain float, written here by ma_sound_set_volume from apply(), read by
 * the mixer in ma_gainer_process_pcm_frames_internal) and
 * ma_spatializer_listener::isEnabled (a plain ma_bool32, written by
 * setListenerActive, read by the mixer). Both are single aligned scalars with
 * no invariant spanning them, and miniaudio uses ma_atomic_float for exactly
 * this kind of field elsewhere - ma_engine_node::volume and
 * ma_device::masterVolumeFactor are both atomic - so these two read as
 * oversights upstream rather than a design. They are left alone deliberately:
 * patching them means carrying a fork of the backend, and quieting half of
 * them from this side would hide the other half.
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
         * @brief Stop every voice and shut the mixer down. Idempotent.
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
         * @brief Whether @p voice still has audio left to play.
         *
         * False once a one-shot has reached its end, which is how AudioSystem
         * learns that a sound finished on its own. A looping voice never
         * reports false until it is stopped.
         */
        bool isVoicePlaying(VoiceId voice) const;

        /**
         * @brief Stop @p voice and release it. Unknown ids are ignored.
         */
        void stopVoice(VoiceId voice);

        /**
         * @brief Stop and release every voice, leaving the device open.
         *
         * The answer to a scene load: the clips the voices are reading are
         * about to be freed, so nothing may still be pointing at them.
         */
        void stopAllVoices();

        /// Number of voices the mixer currently holds.
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
         * @brief Master gain applied to the whole mix. Negative values clamp to 0.
         */
        void setMasterVolume(float volume);

    private:
        // Everything backend-shaped lives behind this: ma_engine, the voice
        // table and the ma_sound / ma_audio_buffer_ref pair each voice owns.
        // Held by pointer so miniaudio's header stays out of this one.
        struct Backend;

    private:
        std::unique_ptr<Backend> m_backend;
        bool                     m_open = false;
};

} // namespace Vkm::Engine

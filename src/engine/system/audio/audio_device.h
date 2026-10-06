#pragma once

#include <algorithm>
#include <cmath>
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
 * @brief The most voices the mixer will hold at once.
 *
 * Well above any scene's need, so reaching it means a runaway (usually a
 * PlaySoundEvent every frame) and play() refuses rather than making room.
 */
inline constexpr size_t MAX_ACTIVE_VOICES = 128;

/**
 * @brief Everything the mixer needs to know about one voice, pushed every frame.
 *
 * A snapshot rather than a component reference: the device must not know the ECS.
 */
struct VoiceParams {
    /**
     * @brief Linear gain for this voice, multiplied into the master.
     *
     * Negative and non-finite values are heard as silence, since one infinite gain
     * would take the whole mix non-finite.
     */
    float     volume      = 1.0f;
    float     pitch       = 1.0f;
    bool      loop        = false;
    bool      spatial     = true;
    glm::vec3 position    = {0.0f, 0.0f, 0.0f};
    float     minDistance = 1.0f;
    float     maxDistance = 50.0f;

    /**
     * @brief The near distance as the mixer takes it.
     *
     * It is multiplied into the per-sample gain, so bad values are read as zero.
     *
     * @return minDistance, or 0 where it is negative or not finite.
     */
    float heardMinDistance() const { return std::isfinite(minDistance) ? std::max(0.0f, minDistance) : 0.0f; }

    /**
     * @brief The far distance as the mixer takes it.
     *
     * @return maxDistance, or 0 where it is negative or not finite.
     */
    float heardMaxDistance() const { return std::isfinite(maxDistance) ? std::max(0.0f, maxDistance) : 0.0f; }

    /**
     * @brief Whether the voice fades with distance and so has a range past which it is silent.
     *
     * @return true when the far distance opens past the near one.
     */
    bool hasFar() const { return heardMaxDistance() > heardMinDistance(); }
};

/**
 * @brief The engine's whole surface onto the audio backend.
 *
 * audio_device.cpp is the only file under src/engine that includes miniaudio. A voice
 * shares ownership of its clip's samples, so a sound outlives its graph. Main-thread only,
 * render() included, and unguarded; with no device open() returns false and every other
 * call is a no-op. See docs/reference/audio.md, "Which thread may call the device" and
 * "The known races are miniaudio's".
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
         * Excludes the null backend, which would succeed with no audio hardware.
         *
         * @return True when a device opened; false leaves the engine silent.
         */
        bool open();

        /**
         * @brief Open the mixer with no device, to be pumped by render().
         *
         * The same mix, output to the caller's buffer instead of hardware.
         *
         * @param sampleRate Mix rate; the caller's buffer is at this rate.
         * @param channels Output channel count (2 to hear panning at all).
         * @return True when the mixer initialised.
         */
        bool openOffline(uint32_t sampleRate, uint32_t channels);

        /**
         * @brief Stop every voice and shut the mixer down; idempotent.
         */
        void close();

        /**
         * @brief Pull mixed frames from an offline mixer into @p frames.
         *
         * Only meaningful after openOffline().
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
         * @param clip Decoded clip to play; an empty one yields no voice.
         * @param params Initial gain, pitch, looping and position.
         * @return The new voice, or 0 if the device is closed or the sound
         *         could not be created.
         */
        VoiceId play(const AudioClipAsset& clip, const VoiceParams& params);

        /**
         * @brief Push a voice's parameters to the mixer; an unknown or finished voice is ignored.
         *
         * @param voice  Voice to update.
         * @param params Gain, pitch, looping and position from now on.
         */
        void updateVoice(VoiceId voice, const VoiceParams& params);

        /**
         * @brief Whether @p voice is still there with audio left to play.
         *
         * False once a one-shot reaches its end; a looping voice stays true until
         * stopped. A paused voice answers true; see isVoicePaused.
         *
         * @param voice Voice asked about.
         * @return Whether it still has audio to play, held or not.
         */
        bool isVoiceActive(VoiceId voice) const;

        /**
         * @brief Hold @p voice at its cursor, silently, without ending it.
         *
         * Ramped like a stop, so the cursor moves a few milliseconds past the call. A voice
         * pauseAllVoices() already holds changes hands, so a world resume leaves it held.
         *
         * @param voice Voice to hold; unknown ids are ignored.
         */
        void pauseVoice(VoiceId voice);

        /**
         * @brief Let @p voice play on from where it was held, ramped back in.
         *
         * Releases a pauseAllVoices() hold as readily as a pauseVoice() one.
         *
         * @param voice Voice to let go; one not held, or unknown, is ignored.
         */
        void resumeVoice(VoiceId voice);

        /**
         * @brief Whether @p voice is being held by a pause rather than playing.
         *
         * @param voice Voice asked about.
         * @return Whether a pause is holding it; false for an unknown one.
         */
        bool isVoicePaused(VoiceId voice) const;

        /**
         * @brief How far into its clip @p voice has played, in seconds.
         *
         * The only copy of the cursor; see docs/reference/audio.md, "The cursor is
         * the device's, not the component's".
         *
         * @param voice Voice to read; an unknown or finished one answers 0.
         * @return Seconds from the clip's start, counting a seek that has been
         *         asked for but not yet applied by the mixer.
         */
        float voiceCursor(VoiceId voice) const;

        /**
         * @brief Move @p voice's cursor to @p seconds into its clip.
         *
         * The mixer applies it before its next read. Clamped to the clip: miniaudio refuses an
         * out-of-range seek yet still moves the sound's clock. Seeking to the end finishes it.
         *
         * @param voice Voice to move; unknown ids are ignored.
         * @param seconds Position from the clip's start; negative and
         *        non-finite values land at 0.
         */
        void seekVoice(VoiceId voice, float seconds);

        /**
         * @brief Ramp @p voice to silence and let it go.
         *
         * The few-millisecond ramp avoids a click; the id answers as released at once, and
         * reapFinishedVoices() frees it after. A held voice stops being held.
         *
         * @param voice Voice to stop; unknown ids are ignored.
         */
        void stopVoice(VoiceId voice);

        /**
         * @brief Release every voice that has played to its end, or been ramped out by stopVoice().
         *
         * Makes play() safe for a caller that never keeps the id; ids are never reused. Also
         * reopens a device that stopped by itself (reopenLostDevice), on the frame thread.
         */
        void reapFinishedVoices();

        /**
         * @brief Cut and release every voice at once, leaving the device open.
         *
         * For when the voices' clips are about to be freed, so not ramped: a ramping voice still
         * reads its clip.
         */
        void stopAllVoices();

        /**
         * @brief Hold every voice that is sounding right now.
         *
         * For a tool's transport, not a game's pause (see AudioSystem). A voice
         * started afterwards plays.
         */
        void pauseAllVoices();

        /**
         * @brief Let every voice pauseAllVoices() is still holding play on.
         *
         * A voice held by pauseVoice(), or stopped while held, stays as it is.
         */
        void resumeAllVoices();

        /**
         * @brief Number of voices the mixer currently holds.
         *
         * @return The voices held now, including any ramping out or paused.
         */
        size_t voiceCount() const;

        /**
         * @brief Place the ear.
         *
         * @param position World position of the listener.
         * @param forward World-space facing; the engine's forward is -Z, so this is
         *        what Math::computeForward returns, not the entity's +Z column.
         * @param up World-space up for the listener.
         */
        void setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

        /**
         * @brief Whether anything is listening.
         *
         * Without one, spatial voices go silent and 2D ones play on.
         *
         * @param active Whether a listener exists this frame.
         */
        void setListenerActive(bool active);

        /**
         * @brief Master gain applied to the whole mix.
         *
         * @param volume Linear master gain; 0 and below, or non-finite, silence the mix.
         */
        void setMasterVolume(float volume);

    public:
        bool isOpen() const noexcept { return m_backend != nullptr; }

        /**
         * @brief The master gain the mix is running at, after setMasterVolume's sanitising.
         *
         * @return Linear master gain; 1 until something sets it otherwise.
         */
        float masterVolume() const noexcept { return m_masterVolume; }

    private:
        // ma_engine, the voice table and each voice's sound; held by pointer so
        // miniaudio's header stays out of this one.
        struct Backend;

    private:
        /**
         * @brief Open the default device again under the running mixer.
         *
         * Voices carry on where the old device left them; while none opens, another
         * is tried every REOPEN_INTERVAL.
         */
        void reopenLostDevice();

    private:
        std::unique_ptr<Backend> m_backend;
        float                    m_masterVolume = 1.0f;

        /// Latched while the voice budget is full, so the warning is said once per saturation.
        bool                     m_voiceBudgetSpent = false;
};

} // namespace Vkm::Engine

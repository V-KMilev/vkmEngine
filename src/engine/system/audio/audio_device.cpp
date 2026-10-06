#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_device.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>

#include "logger.h"
#include "miniaudio.h"

#include "resource/asset/audio_clip_asset.h"

namespace Vkm::Engine {

namespace {

// The supported platforms' backends (engine.md), in miniaudio's priority order. Stated
// because ma_engine's default list ends in the null backend, which succeeds with no hardware.
constexpr std::array<ma_backend, 7> PLAYBACK_BACKENDS = {
    ma_backend_wasapi,
    ma_backend_dsound,
    ma_backend_winmm,
    ma_backend_pulseaudio,
    ma_backend_alsa,
    ma_backend_jack,
    ma_backend_oss,
};

// How long a stopped voice takes to reach silence; measured in docs/reference/audio.md.
constexpr uint32_t STOP_FADE_MS = 5;

/// Wait between attempts to reopen a lost device: opening blocks, and a host with no
/// output would otherwise pay it every frame.
constexpr std::chrono::seconds REOPEN_INTERVAL{2};

/**
 * @brief Forward a miniaudio log line into vkmLog.
 *
 * Called from the mixer thread too, so it trims into a stack buffer.
 *
 * @param userData Unused; the callback is registered with none.
 * @param level miniaudio's level for the line; only warnings and errors pass.
 * @param message The line, newline-terminated as miniaudio writes it.
 */
void forwardBackendLog(void* userData, ma_uint32 level, const char* message) {
    // Below warnings, miniaudio dumps ninety lines of device capabilities per launch.
    if (message == nullptr || (level != MA_LOG_LEVEL_WARNING && level != MA_LOG_LEVEL_ERROR)) return;

    char text[512];
    std::size_t length = 0;
    while (length + 1 < sizeof(text) && message[length] != '\0') {
        text[length] = message[length];
        ++length;
    }
    // Strip miniaudio's trailing newline, which would break vkmLog's line.
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) --length;
    if (length == 0) return;
    text[length] = '\0';

    // Both at WARNING: a backend "error" is usually one backend routinely declining;
    // open() reports once every one has.
    LOG_WARNING("miniaudio: %s", text);
}

/**
 * @brief The device's data callback: pull the next block of the mix.
 *
 * @param device     The device asking; its user data is the engine.
 * @param out        Where the device wants the frames.
 * @param in         Unused; a playback device has no input.
 * @param frameCount How many frames it wants.
 */
void pullMix(ma_device* device, void* out, const void* in, ma_uint32 frameCount) {
    (void)in;
    ma_engine_read_pcm_frames(static_cast<ma_engine*>(device->pUserData), out, frameCount, nullptr);
}

} // namespace

/**
 * @brief The backend half of AudioDevice: the mixer, its context, and its voices.
 *
 * A voice's ma_sound reads a ma_audio_buffer_ref into the clip's samples; neither may
 * move once initialised, so each voice is heap-allocated.
 */
struct AudioDevice::Backend {
    struct Voice {
        /**
         * @brief Who is holding a voice at its cursor, if anyone.
         */
        enum class Hold {
            None,
            Own,   ///< Asked for by pauseVoice; only resumeVoice lets it go.
            Bulk,  ///< Taken by pauseAllVoices; resumeAllVoices gives it back.
        };

        ma_sound            sound;
        ma_audio_buffer_ref buffer;
        // Keeps the PCM the buffer points at alive for as long as this voice is.
        ClipSamples samples;
        // Ramping to silence, awaiting the reap; hidden from find(), so it reads as gone.
        bool retiring = false;
        // Held at its cursor, and by whom. The mixer cannot say: a held sound and one
        // that ran out are both stopped nodes, and only the latter is reaped.
        Hold hold = Hold::None;
    };

    ma_log    log;
    bool      hasLog = false;
    ma_engine engine;

    // Device-backed mixers only; outlives the engine, which does not free a context it
    // did not create.
    std::unique_ptr<ma_context> context;

    // Owned here, not by the engine, so a lost one is rebuilt under a mixer that keeps
    // its voices; on the heap, because the engine holds a pointer to it.
    std::unique_ptr<ma_device> device;

    // When a lost device may next be reopened, and whether the last try failed, so a
    // host with no output says so once.
    std::chrono::steady_clock::time_point reopenAt{};
    bool reopenFailed = false;

    std::unordered_map<VoiceId, std::unique_ptr<Voice>> voices;

    // Never reused, so a stale id cannot name a later voice; nothing reaches the wrap.
    VoiceId nextVoice = 1;

    Voice* find(VoiceId id) const {
        auto it = voices.find(id);
        if (it == voices.end() || it->second->retiring) return nullptr;
        return it->second.get();
    }

    void release(Voice& voice) const {
        // The buffer goes second: until the node is detached, the mixer may still read it.
        ma_sound_uninit(&voice.sound);
        ma_audio_buffer_ref_uninit(&voice.buffer);
    }

    static void apply(Voice& voice, const VoiceParams& params) {
        const float gain = std::isfinite(params.volume) ? std::max(0.0f, params.volume) : 0.0f;
        ma_sound_set_volume(&voice.sound, gain);

        // Floored: miniaudio ignores pitch <= 0, and a NaN passes that check and is stored.
        // A max, not std::clamp, which would hand the NaN on.
        ma_sound_set_pitch(&voice.sound, std::max(0.01f, params.pitch));
        ma_sound_set_looping(&voice.sound, params.loop ? MA_TRUE : MA_FALSE);

        ma_sound_set_spatialization_enabled(&voice.sound, params.spatial ? MA_TRUE : MA_FALSE);
        if (!params.spatial) return;

        ma_sound_set_position(&voice.sound, params.position.x, params.position.y, params.position.z);

        // Clamped, because a non-finite distance survives the attenuation curve's own
        // divide-by-zero guard (`min >= max` is false for a NaN).
        ma_sound_set_min_distance(&voice.sound, params.heardMinDistance());
        ma_sound_set_max_distance(&voice.sound, params.heardMaxDistance());
    }

    /**
     * @brief Ramp @p voice to silence and hold its cursor there.
     *
     * Ramped like a stop (STOP_FADE_MS), so the hold lands a few milliseconds late.
     *
     * @param voice Voice to hold.
     * @param hold Who is holding it, which decides who may let it go.
     */
    static void pauseSound(Voice& voice, Voice::Hold hold) {
        voice.hold = hold;
        ma_sound_stop_with_fade_in_milliseconds(&voice.sound, STOP_FADE_MS);
    }

    /**
     * @brief Undo pauseSound and let @p voice play on from where it was held.
     *
     * Clears both things the ramped pause leaves silencing it: a past stop time and a
     * fader at zero.
     *
     * @param voice Voice to let go.
     */
    static void resumeSound(Voice& voice) {
        ma_sound_set_stop_time_in_pcm_frames(&voice.sound, ~static_cast<ma_uint64>(0));
        ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f, STOP_FADE_MS);
        voice.hold = Voice::Hold::None;
        ma_sound_start(&voice.sound);
    }

    /**
     * @brief Bring the log bridge up first, to catch the context walking its backend list.
     *
     * @return The log for the context and engine configs, or nullptr if it could not
     *         be created, which costs only the messages.
     */
    ma_log* initLog() {
        if (ma_log_init(nullptr, &log) != MA_SUCCESS) return nullptr;
        ma_log_register_callback(&log, ma_log_callback_init(forwardBackendLog, nullptr));
        hasLog = true;
        return &log;
    }

    /**
     * @brief Bring up the mixer, given a config already told whether it drives a device.
     *
     * @param config Engine config; its listener count is set here.
     * @return Whether ma_engine_init succeeded.
     */
    bool initEngine(ma_engine_config& config) {
        // One ear (see findActiveListener); an unused one still costs a spatializer pass.
        config.listenerCount = 1;
        return ma_engine_init(&config, &engine) == MA_SUCCESS;
    }

    /**
     * @brief Open the context's default playback device, pulling from the engine.
     *
     * WASAPI's automatic rerouting is off: it restarts the device from miniaudio's
     * thread and would race our rebuild; reopenLostDevice replaces a lost one instead.
     *
     * @param channels   Channels to open with; 0 for the device's own.
     * @param sampleRate Rate to open with; 0 for the device's own.
     * @return Whether the device opened. It is not started.
     */
    bool initDevice(ma_uint32 channels, ma_uint32 sampleRate) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format            = ma_format_f32;
        config.playback.channels          = channels;
        config.sampleRate                 = sampleRate;
        config.dataCallback               = pullMix;
        config.pUserData                  = &engine;
        config.noPreSilencedOutputBuffer  = MA_TRUE;
        config.noClip                     = MA_TRUE;
        config.wasapi.noAutoStreamRouting = MA_TRUE;
        if (!device) device = std::make_unique<ma_device>();
        return ma_device_init(context.get(), &config, device.get()) == MA_SUCCESS;
    }

    /**
     * @brief Bring up the mixer on the context's default playback device.
     *
     * @return Whether the mixer came up; the engine starts the device.
     */
    bool initDeviceEngine() {
        if (!initDevice(0, 0)) return false;

        ma_engine_config config = ma_engine_config_init();
        config.pDevice = device.get();
        config.pLog    = hasLog ? &log : nullptr;
        if (initEngine(config)) return true;

        ma_device_uninit(device.get());
        return false;
    }

    /**
     * @brief Whether the device stopped without being asked to, or a reopen has not yet found one.
     *
     * Only close() stops a device and it takes the backend, so a stopped device on an
     * open backend was lost (unplugged, driver reset). Read on our thread, where it
     * can be acted on.
     *
     * @return True when a device-backed mixer has no running device.
     */
    bool deviceLost() const {
        if (!device) return false;
        const ma_device_state state = ma_device_get_state(device.get());
        return state == ma_device_state_stopped || state == ma_device_state_uninitialized;
    }

    void uninitLog() {
        if (!hasLog) return;
        ma_log_uninit(&log);
        hasLog = false;
    }
};

AudioDevice::AudioDevice()  = default;
AudioDevice::~AudioDevice() { close(); }

bool AudioDevice::open() {
    if (m_backend) return true;

    auto backend = std::make_unique<Backend>();
    backend->context = std::make_unique<ma_context>();

    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.pLog = backend->initLog();

    const ma_result contextResult = ma_context_init(
        PLAYBACK_BACKENDS.data(),
        static_cast<ma_uint32>(PLAYBACK_BACKENDS.size()),
        &contextConfig,
        backend->context.get()
    );
    if (contextResult != MA_SUCCESS) {
        backend->uninitLog();
        LOG_WARNING("No audio backend available on this host - the engine runs silently");
        return false;
    }

    if (!backend->initDeviceEngine()) {
        ma_context_uninit(backend->context.get());
        backend->uninitLog();
        LOG_WARNING("No audio device could be opened - the engine runs silently");
        return false;
    }

    m_backend = std::move(backend);

    LOG_INFO(
        "Audio device open (%s, %u Hz, %u channel(s))",
        ma_get_backend_name(m_backend->context->backend),
        ma_engine_get_sample_rate(&m_backend->engine),
        ma_engine_get_channels(&m_backend->engine)
    );
    return true;
}

bool AudioDevice::openOffline(uint32_t sampleRate, uint32_t channels) {
    if (m_backend) return true;

    auto backend = std::make_unique<Backend>();

    ma_engine_config config = ma_engine_config_init();
    config.pLog       = backend->initLog();
    config.noDevice   = MA_TRUE;
    config.sampleRate = sampleRate;
    config.channels   = channels;

    if (!backend->initEngine(config)) {
        backend->uninitLog();
        LOG_WARNING("Offline audio mixer failed to initialise");
        return false;
    }

    m_backend = std::move(backend);

    LOG_INFO("Audio mixer open with no device (%u Hz, %u channel(s))", sampleRate, channels);
    return true;
}

void AudioDevice::close() {
    if (!m_backend) return;

    stopAllVoices();
    // The engine stops a borrowed device before taking its graph down; the device goes after.
    ma_engine_uninit(&m_backend->engine);
    if (m_backend->device) ma_device_uninit(m_backend->device.get());
    if (m_backend->context) ma_context_uninit(m_backend->context.get());
    // Last: the three above log as they wind down.
    m_backend->uninitLog();

    m_backend.reset();
    LOG_INFO("Audio device closed");
}

uint64_t AudioDevice::render(float* frames, uint64_t frameCount) {
    if (!m_backend || frames == nullptr) return 0;

    ma_uint64 read = 0;
    if (ma_engine_read_pcm_frames(&m_backend->engine, frames, frameCount, &read) != MA_SUCCESS) {
        return 0;
    }
    return read;
}

VoiceId AudioDevice::play(const AudioClipAsset& clip, const VoiceParams& params) {
    if (!m_backend || clip.sampleCount() == 0 || clip.channels == 0) return 0;

    // Refused, not stolen: the oldest voice may be a deliberate loop.
    if (m_backend->voices.size() >= MAX_ACTIVE_VOICES) {
        if (!m_voiceBudgetSpent) {
            m_voiceBudgetSpent = true;
            LOG_WARNING(
                "Audio voice budget of %zu reached; further sounds are dropped until voices free up. "
                "A game firing one PlaySoundEvent a frame is the usual cause.",
                MAX_ACTIVE_VOICES
            );
        }
        return 0;
    }
    // Once per saturation, not per run.
    m_voiceBudgetSpent = false;

    auto voice = std::make_unique<Backend::Voice>();
    voice->samples = clip.samples;
    const ma_result bufferResult = ma_audio_buffer_ref_init(
        ma_format_s16,
        clip.channels,
        voice->samples->data(),
        clip.frameCount(),
        &voice->buffer
    );
    if (bufferResult != MA_SUCCESS) return 0;
    // ma_audio_buffer_ref_init takes no rate, and a zero rate mixes as the device's:
    // a 22 kHz clip would play an octave high.
    voice->buffer.sampleRate = clip.sampleRate;

    const ma_result soundResult = ma_sound_init_from_data_source(
        &m_backend->engine,
        &voice->buffer,
        0,
        nullptr,
        &voice->sound
    );
    if (soundResult != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&voice->buffer);
        return 0;
    }

    // Linear: the only model where minDistance and maxDistance mean what they say.
    // No Doppler: nothing tracks velocity.
    ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_linear);
    ma_sound_set_rolloff(&voice->sound, 1.0f);
    ma_sound_set_doppler_factor(&voice->sound, 0.0f);

    const VoiceId id = m_backend->nextVoice++;
    Backend::Voice& stored = *(m_backend->voices[id] = std::move(voice));

    Backend::apply(stored, params);
    if (ma_sound_start(&stored.sound) != MA_SUCCESS) {
        stopVoice(id);
        return 0;
    }
    return id;
}

void AudioDevice::updateVoice(VoiceId voice, const VoiceParams& params) {
    if (!m_backend) return;
    if (Backend::Voice* v = m_backend->find(voice)) Backend::apply(*v, params);
}

bool AudioDevice::isVoiceActive(VoiceId voice) const {
    if (!m_backend) return false;
    const Backend::Voice* v = m_backend->find(voice);
    // Not ma_sound_is_playing: a held voice is a stopped node but has not finished.
    return v != nullptr && ma_sound_at_end(&v->sound) == MA_FALSE;
}

void AudioDevice::pauseVoice(VoiceId voice) {
    if (!m_backend) return;
    Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr) return;

    // The hold may change hands, but the ramp is not repeated: that would restart the fade.
    const bool alreadyHeld = v->hold != Backend::Voice::Hold::None;
    v->hold = Backend::Voice::Hold::Own;
    if (!alreadyHeld) Backend::pauseSound(*v, Backend::Voice::Hold::Own);
}

void AudioDevice::resumeVoice(VoiceId voice) {
    if (!m_backend) return;
    Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr || v->hold == Backend::Voice::Hold::None) return;
    Backend::resumeSound(*v);
}

bool AudioDevice::isVoicePaused(VoiceId voice) const {
    if (!m_backend) return false;
    const Backend::Voice* v = m_backend->find(voice);
    return v != nullptr && v->hold != Backend::Voice::Hold::None;
}

float AudioDevice::voiceCursor(VoiceId voice) const {
    if (!m_backend) return 0.0f;
    const Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr) return 0.0f;

    float cursor = 0.0f;
    ma_sound_get_cursor_in_seconds(&v->sound, &cursor);
    return cursor;
}

void AudioDevice::seekVoice(VoiceId voice, float seconds) {
    if (!m_backend) return;
    Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr) return;

    const double at = std::isfinite(seconds) ? std::max(0.0, static_cast<double>(seconds)) : 0.0;
    const ma_uint64 frame = static_cast<ma_uint64>(at * static_cast<double>(v->buffer.sampleRate));
    ma_sound_seek_to_pcm_frame(&v->sound, std::min(frame, v->buffer.sizeInFrames));
}

void AudioDevice::stopVoice(VoiceId voice) {
    if (!m_backend) return;

    auto it = m_backend->voices.find(voice);
    if (it == m_backend->voices.end()) return;

    Backend::Voice& stopping = *it->second;
    // Already on its way out; asking again would restart the fade.
    if (stopping.retiring) return;

    // Clear the hold, or this voice would be kept out of every reap.
    stopping.hold = Backend::Voice::Hold::None;

    // Not released here: the mixer needs the sound until the ramp ends; the reap takes it.
    stopping.retiring = true;
    ma_sound_stop_with_fade_in_milliseconds(&stopping.sound, STOP_FADE_MS);
}

void AudioDevice::reapFinishedVoices() {
    if (!m_backend) return;
    if (m_backend->deviceLost()) {
        reopenLostDevice();
        return;
    }

    for (auto it = m_backend->voices.begin(); it != m_backend->voices.end(); ) {
        // Finished: the clip ran out, or a ramped stop ended. A held voice also has a
        // stopped node; the hold tells them apart.
        const Backend::Voice& voice = *it->second;
        const ma_sound& sound = voice.sound;
        const bool finished = ma_sound_at_end(&sound) == MA_TRUE
            || (ma_sound_is_playing(&sound) == MA_FALSE && voice.hold == Backend::Voice::Hold::None);
        if (!finished) {
            ++it;
            continue;
        }
        m_backend->release(*it->second);
        it = m_backend->voices.erase(it);
    }
}

void AudioDevice::reopenLostDevice() {
    Backend& backend = *m_backend;
    const auto now = std::chrono::steady_clock::now();
    if (now < backend.reopenAt) return;
    backend.reopenAt = now + REOPEN_INTERVAL;

    if (!backend.reopenFailed) {
        LOG_WARNING("The audio device stopped by itself (unplugged?) - opening the default device again");
    }

    // Only the device is replaced; miniaudio converts the standing mix to the new output.
    ma_device_uninit(backend.device.get());
    const ma_uint32 channels   = ma_engine_get_channels(&backend.engine);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&backend.engine);
    const bool opened = backend.initDevice(channels, sampleRate);
    if (!opened || ma_device_start(backend.device.get()) != MA_SUCCESS) {
        if (opened) ma_device_uninit(backend.device.get());
        if (!backend.reopenFailed) {
            LOG_WARNING(
                "No audio device to open - what was playing waits, and one is tried every %llds",
                static_cast<long long>(REOPEN_INTERVAL.count())
            );
        }
        backend.reopenFailed = true;
        return;
    }
    backend.reopenFailed = false;
    LOG_INFO("Audio device open again; what was playing carries on");
}

void AudioDevice::stopAllVoices() {
    if (!m_backend) return;
    for (auto& entry : m_backend->voices) m_backend->release(*entry.second);
    m_backend->voices.clear();
}

void AudioDevice::pauseAllVoices() {
    if (!m_backend) return;
    for (auto& entry : m_backend->voices) {
        Backend::Voice& voice = *entry.second;
        // Skip voices on their way out; one already held keeps its hold.
        if (voice.retiring || voice.hold != Backend::Voice::Hold::None) continue;
        Backend::pauseSound(voice, Backend::Voice::Hold::Bulk);
    }
}

void AudioDevice::resumeAllVoices() {
    if (!m_backend) return;
    for (auto& entry : m_backend->voices) {
        Backend::Voice& voice = *entry.second;
        if (voice.hold == Backend::Voice::Hold::Bulk) Backend::resumeSound(voice);
    }
}

size_t AudioDevice::voiceCount() const {
    return m_backend ? m_backend->voices.size() : 0;
}

void AudioDevice::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_backend) return;
    ma_engine_listener_set_position(&m_backend->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_backend->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_backend->engine, 0, up.x, up.y, up.z);
}

void AudioDevice::setListenerActive(bool active) {
    if (!m_backend) return;
    ma_engine_listener_set_enabled(&m_backend->engine, 0, active ? MA_TRUE : MA_FALSE);
}

void AudioDevice::setMasterVolume(float volume) {
    m_masterVolume = std::isfinite(volume) ? std::max(0.0f, volume) : 0.0f;
    if (!m_backend) return;
    ma_engine_set_volume(&m_backend->engine, m_masterVolume);
}

} // namespace Vkm::Engine

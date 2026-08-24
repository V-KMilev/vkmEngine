#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logger.h"

#include "resource/asset/audio_clip_asset.h"

#include "miniaudio.h"

namespace Vkm::Engine {

namespace {

// Every backend miniaudio knows except the null one, in its own priority order.
// Passed explicitly because ma_engine builds its context from the full default
// list, which ends in the null backend: on a machine with no audio hardware that
// would succeed, spend a thread mixing into nowhere, and hide the one state the
// engine most needs to be able to report.
constexpr std::array<ma_backend, 12> PLAYBACK_BACKENDS = {
    ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm,  ma_backend_coreaudio,
    ma_backend_sndio,  ma_backend_audio4, ma_backend_oss,    ma_backend_pulseaudio,
    ma_backend_alsa,   ma_backend_jack,   ma_backend_aaudio, ma_backend_opensl,
};

// miniaudio speaks from its own thread as well as ours - a device disconnecting
// is reported from the mixer - so this trims into a stack buffer rather than a
// string: there is no reason to reach the allocator from that thread on the way
// to a log line. vkmLog serialises the rest, the way it already does for the
// lines the asset loaders write from ThreadPool workers.
void forwardBackendLog(void* userData, ma_uint32 level, const char* message) {
    // Warnings and errors only. Below that miniaudio writes a ninety-line dump
    // of the device's capabilities on every launch, and the one line of it
    // worth having - which backend, at what rate and layout - open() already
    // writes for itself. What it cannot say for itself is why a device
    // declined, or that one has just disconnected, and that is what this is for.
    if (message == nullptr || (level != MA_LOG_LEVEL_WARNING && level != MA_LOG_LEVEL_ERROR)) return;

    char text[512];
    std::size_t length = 0;
    while (length + 1 < sizeof(text) && message[length] != '\0') {
        text[length] = message[length];
        ++length;
    }
    // miniaudio ends its messages with a newline, which would break the line
    // vkmLog is building around it.
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) --length;
    if (length == 0) return;
    text[length] = '\0';

    // Both at WARNING: a backend "error" is usually one backend declining while
    // miniaudio walks its list, which is routine. The engine's own error is the
    // sentence open() writes once every one of them has declined.
    LOG_WARNING("miniaudio: %s", text);
}

} // namespace

/**
 * @brief The backend half of AudioDevice: the mixer, its context, and its voices.
 *
 * A voice is a ma_sound reading a ma_audio_buffer_ref that points into the
 * clip's own samples, so the two live and die together and neither may move
 * once the sound is initialised - which is why each is heap-allocated rather
 * than sitting in a vector that could reallocate under the mixer.
 */
struct AudioDevice::Backend {
    struct Voice {
        ma_sound            sound;
        ma_audio_buffer_ref buffer;
        // Keeps the PCM the buffer points at alive for as long as this voice is.
        std::shared_ptr<const std::vector<int16_t>> samples;
    };

    ma_log    log;
    bool      hasLog = false;
    ma_engine engine;

    // Only a device-backed mixer has one, and it outlives the engine that
    // borrows it: ma_engine_uninit does not free a context it did not create.
    std::unique_ptr<ma_context> context;

    std::unordered_map<VoiceId, std::unique_ptr<Voice>> voices;

    // Monotonic and never reused, so a voice id that outlives its voice cannot
    // come to name a later one. At one increment per sound started, nothing
    // reaches the wrap.
    VoiceId nextVoice = 1;

    Voice* find(VoiceId id) const {
        auto it = voices.find(id);
        return it == voices.end() ? nullptr : it->second.get();
    }

    void release(Voice& voice) const {
        // Uninit stops the sound and detaches its node from the graph; the
        // buffer it was reading goes second, because until the node is out
        // nothing guarantees the mixer has finished with it.
        ma_sound_uninit(&voice.sound);
        ma_audio_buffer_ref_uninit(&voice.buffer);
    }

    static void apply(Voice& voice, const VoiceParams& params) {
        ma_sound_set_volume(&voice.sound, std::max(0.0f, params.volume));
        // Zero would hold one sample forever instead of advancing the cursor,
        // and miniaudio requires the rate positive.
        ma_sound_set_pitch(&voice.sound, std::max(0.01f, params.pitch));
        ma_sound_set_looping(&voice.sound, params.loop ? MA_TRUE : MA_FALSE);

        ma_sound_set_spatialization_enabled(&voice.sound, params.spatial ? MA_TRUE : MA_FALSE);
        if (!params.spatial) return;

        ma_sound_set_position(&voice.sound, params.position.x, params.position.y, params.position.z);
        ma_sound_set_min_distance(&voice.sound, std::max(0.0f, params.minDistance));
        ma_sound_set_max_distance(&voice.sound, params.maxDistance);
    }

    /**
     * @brief Bring the log bridge up, before anything that might have news.
     *
     * First, because the interesting messages are the ones the context writes
     * while it walks the backend list - which is exactly the run that ends in
     * "this host has no audio" and the one worth being able to read.
     *
     * @return The log to hand to the context and engine configs, or nullptr if
     *         it could not be created, which costs the messages and nothing else.
     */
    ma_log* initLog() {
        if (ma_log_init(nullptr, &log) != MA_SUCCESS) return nullptr;
        ma_log_register_callback(&log, ma_log_callback_init(forwardBackendLog, nullptr));
        hasLog = true;
        return &log;
    }

    /**
     * @brief Bring up the mixer, given a config already told whether it drives a device.
     */
    bool initEngine(ma_engine_config& config) {
        // One ear. miniaudio offers four; a second has no meaning while the
        // engine picks exactly one AudioListener, and an unused one is still a
        // spatializer pass per voice per buffer.
        config.listenerCount = 1;
        return ma_engine_init(&config, &engine) == MA_SUCCESS;
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
    if (m_open) return true;

    auto backend = std::make_unique<Backend>();
    backend->context = std::make_unique<ma_context>();

    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.pLog = backend->initLog();

    if (ma_context_init(PLAYBACK_BACKENDS.data(), static_cast<ma_uint32>(PLAYBACK_BACKENDS.size()),
                        &contextConfig, backend->context.get()) != MA_SUCCESS) {
        backend->uninitLog();
        LOG_WARNING("No audio backend available on this host - the engine runs silently");
        return false;
    }

    ma_engine_config config = ma_engine_config_init();
    config.pContext = backend->context.get();
    config.pLog     = contextConfig.pLog;

    if (!backend->initEngine(config)) {
        ma_context_uninit(backend->context.get());
        backend->uninitLog();
        LOG_WARNING("No audio device could be opened - the engine runs silently");
        return false;
    }

    m_backend = std::move(backend);
    m_open    = true;

    LOG_INFO("Audio device open (%s, %u Hz, %u channel(s))",
             ma_get_backend_name(m_backend->context->backend),
             ma_engine_get_sample_rate(&m_backend->engine),
             ma_engine_get_channels(&m_backend->engine));
    return true;
}

bool AudioDevice::openOffline(uint32_t sampleRate, uint32_t channels) {
    if (m_open) return true;

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
    m_open    = true;

    LOG_INFO("Audio mixer open with no device (%u Hz, %u channel(s))", sampleRate, channels);
    return true;
}

void AudioDevice::close() {
    if (!m_backend) return;

    stopAllVoices();
    ma_engine_uninit(&m_backend->engine);
    if (m_backend->context) ma_context_uninit(m_backend->context.get());
    // Last: the two above log as they wind down.
    m_backend->uninitLog();

    m_backend.reset();
    m_open = false;
    LOG_INFO("Audio device closed");
}

uint64_t AudioDevice::render(float* frames, uint64_t frameCount) {
    if (!m_open || frames == nullptr) return 0;

    ma_uint64 read = 0;
    if (ma_engine_read_pcm_frames(&m_backend->engine, frames, frameCount, &read) != MA_SUCCESS) {
        return 0;
    }
    return read;
}

VoiceId AudioDevice::play(const AudioClipAsset& clip, const VoiceParams& params) {
    if (!m_open || clip.sampleCount() == 0 || clip.channels == 0) return 0;

    auto voice = std::make_unique<Backend::Voice>();
    // The voice shares the clip's samples rather than pointing at them: the
    // mixer reads from the audio thread and a scene load frees assets from the
    // main one, so the buffer has to outlive the asset by however long it takes
    // AudioSystem to notice.
    voice->samples = clip.samples;
    if (ma_audio_buffer_ref_init(ma_format_s16, clip.channels, voice->samples->data(),
                                 clip.frameCount(), &voice->buffer) != MA_SUCCESS) {
        return 0;
    }
    // Assigned rather than passed in: ma_audio_buffer_ref_init takes no rate yet
    // (miniaudio's own TODO), and a buffer reporting zero is mixed as though it
    // were already at the device's rate - a 22 kHz clip played an octave high.
    voice->buffer.sampleRate = clip.sampleRate;

    if (ma_sound_init_from_data_source(&m_backend->engine, &voice->buffer, 0, nullptr,
                                       &voice->sound) != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&voice->buffer);
        return 0;
    }

    // Linear attenuation, always. It is the only model under which the two
    // authored distances mean what they say: full volume inside the first,
    // silent at the second. miniaudio's default inverse model never reaches
    // zero, which leaves maxDistance meaning nothing but a clamp and every
    // sound in a level faintly audible from everywhere in it. Doppler is off
    // because nothing here tracks velocity.
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
    if (!m_open) return;
    if (Backend::Voice* v = m_backend->find(voice)) Backend::apply(*v, params);
}

bool AudioDevice::isVoicePlaying(VoiceId voice) const {
    if (!m_open) return false;
    const Backend::Voice* v = m_backend->find(voice);
    return v != nullptr && ma_sound_at_end(&v->sound) == MA_FALSE;
}

void AudioDevice::stopVoice(VoiceId voice) {
    if (!m_backend) return;

    auto it = m_backend->voices.find(voice);
    if (it == m_backend->voices.end()) return;

    m_backend->release(*it->second);
    m_backend->voices.erase(it);
}

void AudioDevice::reapFinishedVoices() {
    if (!m_backend) return;

    for (auto it = m_backend->voices.begin(); it != m_backend->voices.end(); ) {
        if (ma_sound_at_end(&it->second->sound) == MA_FALSE) {
            ++it;
            continue;
        }
        m_backend->release(*it->second);
        it = m_backend->voices.erase(it);
    }
}

void AudioDevice::stopAllVoices() {
    if (!m_backend) return;
    for (auto& entry : m_backend->voices) m_backend->release(*entry.second);
    m_backend->voices.clear();
}

size_t AudioDevice::voiceCount() const {
    return m_backend ? m_backend->voices.size() : 0;
}

void AudioDevice::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_open) return;
    ma_engine_listener_set_position (&m_backend->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_backend->engine, 0, forward.x,  forward.y,  forward.z);
    ma_engine_listener_set_world_up (&m_backend->engine, 0, up.x,       up.y,       up.z);
}

void AudioDevice::setListenerActive(bool active) {
    if (!m_open) return;
    ma_engine_listener_set_enabled(&m_backend->engine, 0, active ? MA_TRUE : MA_FALSE);
}

void AudioDevice::setMasterVolume(float volume) {
    if (!m_open) return;
    ma_engine_set_volume(&m_backend->engine, std::max(0.0f, volume));
}

} // namespace Vkm::Engine

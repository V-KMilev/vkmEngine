#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_device.h"

#include <algorithm>
#include <array>
#include <cmath>
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

// How long a stopped voice takes to reach silence. Releasing a sound outright
// cuts its waveform at whatever sample the cursor happens to be on, and a
// vertical edge is a click: measured against a 220 Hz tone at gain 0.8, the
// worst cut across one cycle is a step of 0.63 - the signal's full amplitude -
// where its own steepest sample-to-sample step is 0.018, thirty-five times
// smaller. Five milliseconds spreads that over 240 frames at 48 kHz, which
// puts every step of the ramp an order of magnitude below the waveform's own
// slope, and is short enough that a source restarted on the next frame does
// not audibly overlap the tail of the one it replaced.
constexpr uint32_t STOP_FADE_MS = 5;

// Carries what miniaudio cannot say for itself: why a device declined, or that
// one has just disconnected. It speaks from its own thread as well as ours - a
// disconnect is reported from the mixer - so this trims into a stack buffer
// rather than a string: there is no reason to reach the allocator from that
// thread on the way to a log line. vkmLog serialises the rest, the way it
// already does for the lines the asset loaders write from ThreadPool workers.
void forwardBackendLog(void* userData, ma_uint32 level, const char* message) {
    // Warnings and errors only: below that miniaudio dumps ninety lines of device
    // capabilities on every launch, and the one line worth having - backend, rate
    // and layout - open() writes for itself.
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
        // Who is holding a voice at its cursor, if anyone. One field and not a
        // held flag beside an owner flag, because a pair can disagree and the
        // disagreement is expensive: a voice no longer held but still
        // remembered as the bulk pause's comes back on the next
        // resumeAllVoices, and find() hides it, so nothing above is left able
        // to stop it again.
        enum class Hold {
            None,
            Own,   ///< Asked for by pauseVoice; only resumeVoice lets it go.
            Bulk,  ///< Taken by pauseAllVoices; resumeAllVoices gives it back.
        };

        ma_sound            sound;
        ma_audio_buffer_ref buffer;
        // Keeps the PCM the buffer points at alive for as long as this voice is.
        std::shared_ptr<const std::vector<int16_t>> samples;
        // Ramping to silence, waiting for the reap. Hidden from find(), so an
        // id whose voice is fading behaves exactly as one whose voice is gone.
        bool retiring = false;
        // Held at its cursor, and by whom. Kept here because the mixer cannot
        // say it: a held sound is a stopped node, which is also what a voice
        // that ran out looks like, and only one of the two should be reaped.
        Hold hold = Hold::None;
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
        if (it == voices.end() || it->second->retiring) return nullptr;
        return it->second.get();
    }

    void release(Voice& voice) const {
        // Uninit stops the sound and detaches its node from the graph; the
        // buffer it was reading goes second, because until the node is out
        // nothing guarantees the mixer has finished with it.
        ma_sound_uninit(&voice.sound);
        ma_audio_buffer_ref_uninit(&voice.buffer);
    }

    static void apply(Voice& voice, const VoiceParams& params) {
        // A NaN gain is already answered with silence, because std::max keeps
        // its first argument when the comparison against a NaN comes back
        // false. An infinite one was not, and it is not one loud voice: every
        // voice sums into the same master, so the whole mix reads non-finite
        // for as long as it lives. Measured with two healthy voices beside it,
        // all 4800 samples of the output went NaN, and came back only once the
        // bad voice was reaped. Silence is the failure that stays local.
        const float gain = std::isfinite(params.volume) ? std::max(0.0f, params.volume) : 0.0f;
        ma_sound_set_volume(&voice.sound, gain);
        // Zero would hold one sample forever instead of advancing the cursor,
        // and miniaudio requires the rate positive. A NaN rate lands on the
        // same floor by the rule above, which is why this stays a max rather
        // than becoming a clamp - std::clamp would hand the NaN straight on.
        ma_sound_set_pitch(&voice.sound, std::max(0.01f, params.pitch));
        ma_sound_set_looping(&voice.sound, params.loop ? MA_TRUE : MA_FALSE);

        ma_sound_set_spatialization_enabled(&voice.sound, params.spatial ? MA_TRUE : MA_FALSE);
        if (!params.spatial) return;

        ma_sound_set_position(&voice.sound, params.position.x, params.position.y, params.position.z);
        ma_sound_set_min_distance(&voice.sound, std::max(0.0f, params.minDistance));
        ma_sound_set_max_distance(&voice.sound, params.maxDistance);
    }

    /**
     * @brief Ramp @p voice to silence and hold its cursor there.
     *
     * Ramped rather than cut for the reason STOP_FADE_MS exists: a pause is a
     * cut at whatever sample the cursor is on, and measured across the phases
     * of a tone it steps by the signal's full amplitude both going in and
     * coming out. The sound therefore keeps playing through its own fade, so
     * the hold lands a few milliseconds after it is asked for.
     */
    static void pauseSound(Voice& voice, Voice::Hold hold) {
        voice.hold = hold;
        ma_sound_stop_with_fade_in_milliseconds(&voice.sound, STOP_FADE_MS);
    }

    /**
     * @brief Undo pauseSound and let @p voice play on from where it was held.
     *
     * The ramped pause leaves two things behind that would keep the sound
     * silent however loud it is asked to be: a stop time now in the past,
     * which the node reads as stopped whatever its state says, and a fader
     * sitting at zero. Both are cleared here, and the ramp back in is the
     * same length as the ramp out for the same reason.
     */
    static void resumeSound(Voice& voice) {
        ma_sound_set_stop_time_in_pcm_frames(&voice.sound, ~static_cast<ma_uint64>(0));
        ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f, STOP_FADE_MS);
        voice.hold = Voice::Hold::None;
        ma_sound_start(&voice.sound);
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

bool AudioDevice::isVoiceActive(VoiceId voice) const {
    if (!m_open) return false;
    const Backend::Voice* v = m_backend->find(voice);
    // Deliberately not ma_sound_is_playing: a held voice is a stopped node
    // and it has not finished, so what is asked here is whether the clip has
    // run out. isVoicePaused is the other half.
    return v != nullptr && ma_sound_at_end(&v->sound) == MA_FALSE;
}

void AudioDevice::pauseVoice(VoiceId voice) {
    if (!m_open) return;
    Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr) return;

    // Whoever asks for the hold owns it, and asking is the claim - a voice the
    // transport is already holding becomes this caller's, so the world resuming
    // underneath it leaves it where it was put. What must not be repeated is
    // the ramp, which would restart a fade already halfway down.
    const bool alreadyHeld = v->hold != Backend::Voice::Hold::None;
    v->hold = Backend::Voice::Hold::Own;
    if (!alreadyHeld) Backend::pauseSound(*v, Backend::Voice::Hold::Own);
}

void AudioDevice::resumeVoice(VoiceId voice) {
    if (!m_open) return;
    Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr || v->hold == Backend::Voice::Hold::None) return;
    Backend::resumeSound(*v);
}

bool AudioDevice::isVoicePaused(VoiceId voice) const {
    if (!m_open) return false;
    const Backend::Voice* v = m_backend->find(voice);
    return v != nullptr && v->hold != Backend::Voice::Hold::None;
}

float AudioDevice::voiceCursor(VoiceId voice) const {
    if (!m_open) return 0.0f;
    const Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr) return 0.0f;

    float cursor = 0.0f;
    ma_sound_get_cursor_in_seconds(&v->sound, &cursor);
    return cursor;
}

void AudioDevice::seekVoice(VoiceId voice, float seconds) {
    if (!m_open) return;
    Backend::Voice* v = m_backend->find(voice);
    if (v == nullptr) return;

    const double at = std::isfinite(seconds) ? std::max(0.0, static_cast<double>(seconds)) : 0.0;
    const ma_uint64 frame = static_cast<ma_uint64>(at * static_cast<double>(v->buffer.sampleRate));
    // Clamped rather than passed through: miniaudio refuses a seek past the
    // end at the data source and moves the sound's own clock to the position
    // it refused anyway, leaving the voice playing from where it was with a
    // clock nine seconds ahead of it. The clip's length is right here.
    ma_sound_seek_to_pcm_frame(&v->sound, std::min(frame, v->buffer.sizeInFrames));
}

void AudioDevice::stopVoice(VoiceId voice) {
    if (!m_backend) return;

    auto it = m_backend->voices.find(voice);
    if (it == m_backend->voices.end()) return;

    Backend::Voice& stopping = *it->second;
    // Already on its way out. Asking again would re-schedule the ramp from
    // wherever it had got to, restarting a fade that is halfway done.
    if (stopping.retiring) return;

    // A stopped voice is held by nobody. It is already silent if it was held,
    // so it needs no ramp of its own; leaving it marked held would keep it out
    // of every future reap, since the hold is exactly why it reads as not
    // playing, and would leave resumeAllVoices a stopped voice to start again.
    stopping.hold = Backend::Voice::Hold::None;

    // Ramped rather than cut, and therefore not released here: the mixer needs
    // the sound for as long as the ramp lasts. reapFinishedVoices takes it
    // once the scheduled stop has passed, and until then find() hides it, so
    // the id behaves exactly as it did when this released outright - unknown
    // to updateVoice, finished to isVoiceActive.
    stopping.retiring = true;
    ma_sound_stop_with_fade_in_milliseconds(&stopping.sound, STOP_FADE_MS);
}

void AudioDevice::reapFinishedVoices() {
    if (!m_backend) return;

    for (auto it = m_backend->voices.begin(); it != m_backend->voices.end(); ) {
        // Two ways to be finished: the clip ran out, or a ramped stop reached
        // the end of its fade. A looping voice satisfies neither until someone
        // stops it, which is what keeps an ambience alive here.
        //
        // A held voice looks like the second one and is not it: holding one
        // stops the node, so the only thing separating a sound that is waiting
        // from one that is over is who has it. stopVoice hands it back, so a
        // voice stopped while held is still swept.
        const Backend::Voice& voice = *it->second;
        const ma_sound& sound = voice.sound;
        const bool finished = ma_sound_at_end(&sound) == MA_TRUE
                           || (ma_sound_is_playing(&sound) == MA_FALSE
                               && voice.hold == Backend::Voice::Hold::None);
        if (!finished) {
            ++it;
            continue;
        }
        m_backend->release(*it->second);
        it = m_backend->voices.erase(it);
    }
}

void AudioDevice::stopAllVoices() {
    if (!m_backend) return;
    // Cut, not ramped, unlike stopVoice. This is the teardown path: close()
    // uninitialises the mixer on the next line, so a ramp scheduled here would
    // never be mixed, and the scene load that calls it has already replaced
    // the world the sounds belonged to. Waiting for a fade would mean holding
    // the frame open for it or inventing somewhere for the voices to live in
    // the meantime, to smooth an edge under a load that is not quiet anyway.
    for (auto& entry : m_backend->voices) m_backend->release(*entry.second);
    m_backend->voices.clear();
}

void AudioDevice::pauseAllVoices() {
    if (!m_backend) return;
    for (auto& entry : m_backend->voices) {
        Backend::Voice& voice = *entry.second;
        // A voice on its way out is not held back to be resumed later, and one
        // already held keeps the hold it has rather than changing hands - which
        // is what lets an audition held on purpose survive a Resume.
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
    // The same guard the per-voice gain carries, for the same measured reason:
    // an infinite master takes the entire mix non-finite, and this one is
    // authored - AudioListener::volume is a serialized field a slider writes.
    // Kept rather than only pushed, so masterVolume() answers what the mixer
    // was given instead of making every caller sanitise the number again.
    m_masterVolume = std::isfinite(volume) ? std::max(0.0f, volume) : 0.0f;
    if (!m_open) return;
    ma_engine_set_volume(&m_backend->engine, m_masterVolume);
}

} // namespace Vkm::Engine

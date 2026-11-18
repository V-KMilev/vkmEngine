// Sound as a player hears it: the engine's mix, pulled from the mixer with no device, a frame's worth
// after every frame.

#include <cmath>
#include <vector>

#include "vkm_audio/acoustics.h"

#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/render/camera.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/audio/audio_events.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Audio;
using namespace Vkm::Test;

namespace {

constexpr uint32_t RATE      = 48000;
constexpr uint32_t PER_FRAME = RATE / 60;

/// A mono sine of @p hertz, @p seconds long, at half scale.
AudioClipHandle tone(
    ResourceManager& resources,
    float hertz,
    float seconds,
    const char* name,
    uint32_t channels = 1
) {
    AudioClipAsset clip;
    clip.sampleRate = RATE;
    clip.channels   = channels;
    std::vector<int16_t> samples(static_cast<size_t>(RATE * seconds) * channels);
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = static_cast<float>(i / channels) / RATE;
        samples[i] = static_cast<int16_t>(16384.0f * std::sin(6.2831853f * hertz * t));
    }
    clip.samples = ClipSamples(std::move(samples));
    return resources.add(std::move(clip), name);
}

/**
 * @brief The game's mix, heard through a mixer the test pulls: left and right, a frame at a time.
 */
struct Ear {
    std::vector<float> left, right;  // the last frame's

    explicit Ear(Game& game) : m_game(game) {
        REQUIRE(game.systems.audio.device().openOffline(RATE, 2));
    }

    void hear() {
        std::vector<float> both(PER_FRAME * 2);
        m_game.systems.audio.device().render(both.data(), PER_FRAME);
        left.resize(PER_FRAME);
        right.resize(PER_FRAME);
        for (uint32_t i = 0; i < PER_FRAME; ++i) {
            left[i]  = both[2 * i];
            right[i] = both[2 * i + 1];
        }
    }

    static float level(const std::vector<float>& channel) {
        double sum = 0.0;
        for (const float s : channel) sum += double(s) * s;
        return channel.empty() ? 0.0f : static_cast<float>(std::sqrt(sum / channel.size()));
    }
    float leftLevel() const  { return level(left); }
    float rightLevel() const { return level(right); }

    /// Sign changes a second in the left channel: twice a sine's frequency.
    float crossings() const {
        int n = 0;
        for (size_t i = 1; i < left.size(); ++i) n += (left[i - 1] < 0.0f) != (left[i] < 0.0f);
        return n * 60.0f / 2.0f;
    }

    private:
        Game& m_game;
};

/// A camera at the origin looking down -Z: the ear when no listener is placed.
EntityId addEye(Scene& scene, const glm::vec3& at = glm::vec3(0.0f)) {
    const EntityId eye = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(eye, pose);
    scene.add(eye, Camera{});
    return eye;
}

EntityId addSource(Scene& scene, AudioClipHandle clip, const glm::vec3& at, bool spatial = true) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(id, pose);
    AudioSource source;
    source.clip    = clip;
    source.spatial = spatial;
    scene.add(id, source);
    return id;
}

} // namespace

TEST("a source puts its clip in the mix at its volume, and a one-shot ends and says so") {
    Game game;
    Scene& scene = game.scene();
    scene.environment().acoustics.enabled = false;
    addEye(scene);
    const AudioClipHandle clip = tone(game.resources(), 220.0f, 0.5f, "test:tone");
    const EntityId full = addSource(scene, clip, {0.0f, 0.0f, 0.0f}, false);
    Ear ear(game);

    float loud = 0.0f, quiet = 0.0f, after = 1.0f;
    bool playingAt20 = false;
    game.run(50, [&](int frame) {
        ear.hear();
        if (frame == 10) loud = ear.leftLevel();
        if (frame == 20) {
            playingAt20 = scene.get<AudioSource>(full).playing;
            quiet       = ear.leftLevel();
        }
        if (frame == 15) scene.get<AudioSource>(full).volume = 0.25f;
        if (frame == 45) after = ear.leftLevel();
    });

    CHECK(loud > 0.1f);
    CHECK_NEAR(quiet / loud, 0.25f, 0.02f);
    CHECK(playingAt20);
    CHECK(!scene.get<AudioSource>(full).playing);
    CHECK(after < 1e-4f);
}

TEST("a source's pitch speeds its clip, and a looping one plays on past its end") {
    Game game;
    Scene& scene = game.scene();
    addEye(scene);
    const AudioClipHandle clip = tone(game.resources(), 300.0f, 0.25f, "test:short");
    const EntityId high = addSource(scene, clip, {0.0f, 0.0f, 0.0f}, false);
    scene.get<AudioSource>(high).pitch = 2.0f;
    scene.get<AudioSource>(high).loop  = true;
    Ear ear(game);

    float hertz = 0.0f;
    game.run(60, [&](int frame) {
        ear.hear();
        if (frame == 40) hertz = ear.crossings();
    });

    CHECK_NEAR(hertz, 600.0f, 15.0f);
    CHECK(scene.get<AudioSource>(high).playing);
}

TEST("a mono source pans to the side it is on, and a stereo one's channels never cross") {
    Game game;
    Scene& scene = game.scene();
    scene.environment().acoustics.enabled = false;
    addEye(scene);
    const EntityId right = addSource(
        scene,
        tone(game.resources(), 200.0f, 2.0f, "test:mono"),
        {5.0f, 0.0f, 0.0f}
    );
    scene.get<AudioSource>(right).loop = true;
    // A stereo clip with sound in its right channel alone.
    AudioClipAsset rightOnly;
    rightOnly.sampleRate = RATE;
    rightOnly.channels   = 2;
    std::vector<int16_t> samples(RATE * 2 * 2, 0);
    for (size_t i = 1; i < samples.size(); i += 2) samples[i] = static_cast<int16_t>(
        16384.0f * std::sin(i * 0.013f)
    );
    rightOnly.samples = ClipSamples(std::move(samples));
    const AudioClipHandle stereo = game.resources().add(std::move(rightOnly), "test:stereo");
    Ear ear(game);

    float panLeft = 0.0f, panRight = 0.0f, crossLeft = 1.0f, crossRight = 0.0f;
    game.run(40, [&](int frame) {
        ear.hear();
        if (frame == 15) {
            panLeft  = ear.leftLevel();
            panRight = ear.rightLevel();
            scene.destroyEntity(right);
            // On the left of the ear, where a panned sound would go.
            const EntityId left = addSource(scene, stereo, {-5.0f, 0.0f, 0.0f});
            scene.get<AudioSource>(left).loop = true;
        }
        if (frame == 35) {
            crossLeft  = ear.leftLevel();
            crossRight = ear.rightLevel();
        }
    });

    CHECK(panRight > 0.05f);
    CHECK(panLeft < panRight * 0.5f);
    CHECK(crossLeft < 1e-4f);
    CHECK(crossRight > 0.01f);
}

TEST("a source fades between its distances, linearly or by the inverse, and is silent past the far one") {
    Game game;
    Scene& scene = game.scene();
    scene.environment().acoustics.enabled = false;
    addEye(scene);
    const AudioClipHandle clip = tone(game.resources(), 200.0f, 2.0f, "test:far");
    const EntityId source = addSource(scene, clip, {0.0f, 0.0f, -1.0f});
    AudioSource& s = scene.get<AudioSource>(source);
    s.loop        = true;
    s.minDistance = 1.0f;
    s.maxDistance = 11.0f;
    Ear ear(game);

    // Frames 10, 20, 30, 40: near, halfway linear, halfway inverse, past the far distance.
    float near = 0.0f, linearHalf = 0.0f, inverseHalf = 0.0f, beyond = 1.0f;
    game.run(40, [&](int frame) {
        ear.hear();
        Transform& at = scene.get<Transform>(source);
        if (frame == 10) {
            near          = ear.leftLevel();
            at.position.z = -6.0f;
        }
        if (frame == 20) {
            linearHalf = ear.leftLevel();
            scene.get<AudioSource>(source).rolloff = AudioRolloff::Inverse;
        }
        if (frame == 30) {
            inverseHalf   = ear.leftLevel();
            at.position.z = -12.0f;
        }
        if (frame == 40) beyond = ear.leftLevel();
    });

    CHECK(near > 0.05f);
    CHECK_NEAR(linearHalf / near, 0.5f, 0.05f);
    CHECK(inverseHalf < linearHalf * 0.6f);
    CHECK(beyond < 1e-4f);
}

TEST("an AudioListener is the ear when there is one, and the camera is when there is none") {
    Game game;
    Scene& scene = game.scene();
    scene.environment().acoustics.enabled = false;
    addEye(scene, {0.0f, 0.0f, 0.0f});
    const AudioClipHandle clip = tone(game.resources(), 200.0f, 2.0f, "test:ear");
    const EntityId source = addSource(scene, clip, {-5.0f, 0.0f, 0.0f});
    scene.get<AudioSource>(source).loop = true;
    Ear ear(game);

    // From the camera the source is on the left; from a listener turned round, on the right.
    bool cameraHearsLeft = false, listenerHearsRight = false;
    float halfVolume     = 0.0f, fullVolume = 0.0f;
    game.run(40, [&](int frame) {
        ear.hear();
        if (frame == 10) {
            cameraHearsLeft = ear.leftLevel() > ear.rightLevel() * 2.0f;
            fullVolume      = ear.leftLevel();
            const EntityId listener = scene.createEntity();
            Transform turned;
            turned.rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            scene.add(listener, turned);
            AudioListener ears;
            ears.volume = 0.5f;
            scene.add(listener, ears);
        }
        if (frame == 30) {
            listenerHearsRight = ear.rightLevel() > ear.leftLevel() * 2.0f;
            halfVolume         = ear.rightLevel();
        }
    });

    CHECK(cameraHearsLeft);
    CHECK(listenerHearsRight);
    CHECK_NEAR(halfVolume / fullVolume, 0.5f, 0.05f);
}

TEST("a sound asked for by event plays with no entity, and one out of reach is dropped") {
    Game game;
    Scene& scene = game.scene();
    scene.environment().acoustics.enabled = false;
    addEye(scene);
    const AudioClipHandle clip = tone(game.resources(), 200.0f, 1.0f, "test:shot");
    Ear ear(game);

    float heard = 0.0f, farHeard = 1.0f;
    size_t farVoices = 1;
    game.run(30, [&](int frame) {
        ear.hear();
        if (frame == 2) game.events().emit(PlaySoundEvent::at(clip, {0.0f, 0.0f, -1.0f}));
        if (frame == 10) {
            heard = ear.leftLevel();
            game.systems.audio.stopEverything();
            game.events().emit(PlaySoundEvent::at(clip, {0.0f, 0.0f, -500.0f}));
        }
        if (frame == 20) {
            farHeard  = ear.leftLevel();
            farVoices = game.systems.audio.device().voiceCount();
        }
    });

    CHECK(heard > 0.05f);
    CHECK(farHeard < 1e-4f);
    CHECK(farVoices == 0u);
}

TEST("a static wall between a source and the ear makes it quieter and duller") {
    // Two sounds, a low and a high tone, each heard open and then walled off: the wall takes more of
    // the high one.
    auto hear = [](float hertz, bool walled) {
        Game game;
        Scene& scene = game.scene();
        addEye(scene);
        if (walled) addBody(scene, {0.0f, 0.0f, -5.0f}, box({4.0f, 4.0f, 0.2f}), RigidbodyMotion::Static);
        const EntityId source = addSource(
            scene,
            tone(game.resources(), hertz, 2.0f, "test:wall"),
            {0.0f, 0.0f, -10.0f}
        );
        scene.get<AudioSource>(source).loop        = true;
        scene.get<AudioSource>(source).maxDistance = 100.0f;
        scene.environment().acoustics.reverb = 0.0f;
        Ear ear(game);
        float level = 0.0f;
        game.run(40, [&](int frame) {
            ear.hear();
            if (frame == 40) level = ear.leftLevel() + ear.rightLevel();
        });
        return level;
    };
    const float lowOpen  = hear(150.0f, false), lowWalled = hear(150.0f, true);
    const float highOpen = hear(6000.0f, false), highWalled = hear(6000.0f, true);

    CHECK(lowWalled < lowOpen * 0.9f);
    CHECK(highWalled / highOpen < lowWalled / lowOpen * 0.8f);
}

TEST("the ear listens as often a second at a frame rate the listening rate does not divide") {
    // 144 frames a second: a listen falls due every 2.4 frames.
    Game game(144);
    Scene& scene = game.scene();
    addEye(scene);
    const AudioClipHandle clip = tone(game.resources(), 440.0f, 1.0f, "test:listen");
    const EntityId source = addSource(scene, clip, {0.0f, 0.0f, -5.0f});
    scene.get<AudioSource>(source).loop = true;
    Ear ear(game);
    uint64_t atOneSecond = 0;
    game.run(2 * 144, [&](int frame) {
        if (frame == 144) atOneSecond = game.systems.audio.listens();
    });

    const uint64_t inASecond = game.systems.audio.listens() - atOneSecond;
    CHECK(inASecond >= static_cast<uint64_t>(Acoustics::LISTENS_A_SECOND) - 1);
    CHECK(inASecond <= static_cast<uint64_t>(Acoustics::LISTENS_A_SECOND) + 1);
}

TEST("at the voice cap a new sound takes a lower-priority or quieter voice, never a paused one") {
    Device device;
    REQUIRE(device.openOffline(RATE, 2));
    AudioClipAsset clip;
    clip.sampleRate = RATE;
    clip.channels   = 1;
    clip.samples    = ClipSamples(std::vector<int16_t>(RATE, 1000));

    VoiceParams flat;
    flat.spatial = false;
    flat.loop    = true;
    std::vector<VoiceId> voices;
    VoiceParams paused = flat;
    paused.priority = -5;
    const VoiceId held = device.play(clip, paused);
    device.pauseVoice(held);
    VoiceParams low = flat;
    low.priority = -1;
    const VoiceId lowest = device.play(clip, low);
    REQUIRE(lowest != 0);
    while (device.voiceCount() < MAX_ACTIVE_VOICES) voices.push_back(device.play(clip, flat));

    // Of its own priority and louder: the low one goes; the paused one never does.
    VoiceParams loud = flat;
    loud.volume = 2.0f;
    CHECK(device.play(clip, loud) != 0);
    CHECK(!device.isVoiceActive(lowest));
    CHECK(device.isVoiceActive(held));
    // Of its own priority and as loud as every voice: none gives way.
    CHECK(device.play(clip, flat) == 0);
    VoiceParams quiet = flat;
    quiet.volume = 0.5f;
    CHECK(device.play(clip, quiet) == 0);
    VoiceParams urgent = flat;
    urgent.priority = 3;
    CHECK(device.play(clip, urgent) != 0);
    CHECK(device.isVoiceActive(held));
}

TEST("a stopped sound ramps to silence instead of cutting") {
    Device device;
    REQUIRE(device.openOffline(RATE, 2));
    AudioClipAsset clip;
    clip.sampleRate = RATE;
    clip.channels   = 1;
    clip.samples    = ClipSamples(std::vector<int16_t>(RATE, 16384));
    VoiceParams flat;
    flat.spatial = false;
    const VoiceId voice = device.play(clip, flat);
    std::vector<float> mix(PER_FRAME * 2);
    device.render(mix.data(), PER_FRAME);
    const float playing = mix[2 * (PER_FRAME - 1)];
    REQUIRE(playing > 0.1f);

    device.stopVoice(voice);
    device.render(mix.data(), PER_FRAME);

    float biggestStep = std::fabs(mix[0] - playing);
    for (uint32_t i = 1; i < PER_FRAME; ++i) biggestStep = std::max(
        biggestStep,
        std::fabs(mix[2 * i] - mix[2 * (i - 1)])
    );
    CHECK(mix[0] > playing * 0.5f);
    CHECK(biggestStep < playing * 0.1f);
    CHECK(std::fabs(mix[2 * (PER_FRAME - 1)]) < 1e-4f);
}

TEST("with no device the engine runs silent, and a one-shot still ends") {
    Game game;
    Scene& scene = game.scene();
    addEye(scene);
    const EntityId source = addSource(
        scene,
        tone(game.resources(), 200.0f, 0.1f, "test:silent"),
        {0.0f, 0.0f, 0.0f}
    );

    game.run(30);

    CHECK(!game.systems.audio.device().isOpen());
    CHECK(!scene.get<AudioSource>(source).playing);
    CHECK(scene.get<AudioSource>(source).started);
}

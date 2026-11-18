// Which camera a game is seen through: one rule, and one holder every reader of it asks.

#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "core/frame/visibility.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/scene/transform.h"
#include "resource/asset/audio_clip_asset.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Audio;
using namespace Vkm::Test;

namespace {

EntityId addCamera(Scene& scene, bool active, const glm::quat& facing = glm::quat(1, 0, 0, 0)) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.rotation = facing;
    scene.add(id, pose);
    Camera lens;
    lens.active = active;
    scene.add(id, lens);
    return id;
}

/**
 * @brief What a frame looked through, and what its ticks aimed with.
 */
struct Witness : System {
    EntityId  seen;
    glm::quat aim{1, 0, 0, 0};
    void fixedUpdate(FrameContext& ctx) override { aim = ctx.input.command().view; }
    void update(
        FrameContext& ctx
    ) override { seen = ctx.visibility ? ctx.visibility->cameraEntity : EntityId{}; }
};

/**
 * @brief The game's mix pulled with no device: how loud each side was over the last frame.
 */
struct Ear {
    static constexpr uint32_t RATE = 48000;
    float                     left = 0.0f, right = 0.0f;

    explicit Ear(Game& game) : m_game(game) { REQUIRE(game.systems.audio.device().openOffline(RATE, 2)); }

    void hear() {
        std::vector<float> both(RATE / 60 * 2);
        m_game.systems.audio.device().render(both.data(), RATE / 60);
        double l = 0.0, r = 0.0;
        for (size_t i = 0; i < both.size(); i += 2) {
            l += double(both[i]) * both[i];
            r += double(both[i + 1]) * both[i + 1];
        }
        left  = static_cast<float>(std::sqrt(l));
        right = static_cast<float>(std::sqrt(r));
    }

    private:
        Game& m_game;
};

/// A looping mono tone at @p at.
void addTone(Game& game, const glm::vec3& at) {
    AudioClipAsset clip;
    clip.sampleRate = Ear::RATE;
    clip.channels   = 1;
    std::vector<int16_t> samples(Ear::RATE);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<int16_t>(
        12000.0f * std::sin(i * 0.03f)
    );
    clip.samples = ClipSamples(std::move(samples));
    const EntityId id = game.scene().createEntity();
    Transform pose;
    pose.position = at;
    game.scene().add(id, pose);
    AudioSource source;
    source.clip = game.resources().add(std::move(clip), "test:tone");
    source.loop = true;
    game.scene().add(id, source);
}

} // namespace

TEST("the lowest-slot active camera is the eye, and stays the eye until it goes") {
    Scene scene;
    CHECK(!findActiveCamera(scene));
    const EntityId first  = addCamera(scene, false);
    const EntityId second = addCamera(scene, true);
    const EntityId third  = addCamera(scene, true);
    CHECK(findActiveCamera(scene) == second);

    // Held, it stays the answer when a lower one wakes, so the view does not jump.
    ActiveCamera eye;
    CHECK(eye.of(scene) == second);
    scene.get<Camera>(first).active = true;
    CHECK(eye.of(scene) == second);
    CHECK(findActiveCamera(scene) == first);

    // Off, or gone, the lowest active one takes over.
    scene.get<Camera>(second).active = false;
    CHECK(eye.of(scene) == first);
    scene.destroyEntity(first);
    CHECK(eye.of(scene) == third);
    // A camera with no pose to see from is no eye.
    scene.remove<Transform>(third);
    CHECK(!eye.of(scene));
}

TEST("with two cameras active, the frame, the ticks' aim and the ear all go by the same one") {
    Game game;
    Scene& scene = game.scene();
    scene.environment().acoustics.enabled = false;
    // The lower slot faces the other way, so its right is the other's left.
    const EntityId behind = addCamera(scene, false, glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0)));
    const EntityId ahead  = addCamera(scene, true);
    addTone(game, {6.0f, 0.0f, 0.0f});
    Witness& witness = game.engine.addSystem<Witness>(SystemStage::Editor);
    Ear ear(game);

    int lookedElsewhere = 0, aimedElsewhere = 0, heardElsewhere = 0;
    game.run(30, [&](int frame) {
        ear.hear();
        // Both are active from here on; the one already looked through stays the eye.
        if (frame == 5) scene.get<Camera>(behind).active = true;
        if (frame < 8) return;
        lookedElsewhere += witness.seen != ahead;
        aimedElsewhere  += std::fabs(glm::dot(witness.aim, glm::quat(1, 0, 0, 0))) < 0.999f;
        heardElsewhere  += !(ear.right > 2.0f * ear.left);
    });
    CHECK(lookedElsewhere == 0);
    CHECK(aimedElsewhere == 0);
    CHECK(heardElsewhere == 0);
}

TEST("a replaced world is seen through its own lowest-slot camera, not the slot the last one held") {
    Game game;
    Scene& scene = game.scene();
    addCamera(scene, false);
    const EntityId held = addCamera(scene, true);
    Witness& witness    = game.engine.addSystem<Witness>(SystemStage::Editor);

    EntityId afterSwap;
    game.run(6, [&](int frame) {
        if (frame == 1) CHECK(witness.seen == held);
        if (frame != 3) return;
        // A world built as the first was, so its second slot is the id the last world's eye had.
        Scene next;
        const EntityId lowest = addCamera(next, true, glm::angleAxis(1.0f, glm::vec3(0, 1, 0)));
        const EntityId again  = addCamera(next, true);
        CHECK(again == held);
        scene.swap(next);
        afterSwap = lowest;
    });
    CHECK(witness.seen == afterSwap);
    CHECK(std::fabs(glm::dot(witness.aim, glm::angleAxis(1.0f, glm::vec3(0, 1, 0)))) > 0.999f);
}

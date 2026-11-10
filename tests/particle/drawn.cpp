// What a frame draws of the live particles: each at its share of its life, in its blend's list.

#include "core/frame/live_particles.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/scene/transform.h"
#include "system/render/render_view.h"
#include "system/visibility/visibility_system.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/**
 * @brief A frame seen through a camera 10 m up +Z, looking at the origin.
 */
struct Seen {
    Frame            frame;
    VisibilitySystem cull;
    LiveParticles    live;
    RenderView       view;

    Seen() {
        frame.chrome.viewport = {0, 0, 320, 180};
        const EntityId eye = frame.scene.createEntity();
        Transform pose;
        pose.position = {0.0f, 0.0f, 10.0f};
        frame.scene.add(eye, pose);
        frame.scene.add(eye, Camera{});
    }

    EntityId emitter(ParticleBlend blend, SeenBy seenBy = SeenBy::All) {
        const EntityId id = frame.scene.createEntity();
        frame.scene.add(id, Transform{});
        ParticleEmitter emitter;
        emitter.blend  = blend;
        emitter.seenBy = seenBy;
        frame.scene.add(id, emitter);
        return id;
    }

    /// A live particle of @p emitter, @p age seconds into a life of @p lifetime.
    void particle(EntityId emitter, float age, float lifetime, const glm::vec3& at = glm::vec3(0.0f)) {
        LiveParticles::Pool& pool = live.pools[emitter.slot()];
        pool.emitter = emitter;
        Particle p;
        p.position = at;
        p.age      = age;
        p.lifetime = lifetime;
        p.size     = 2.0f;
        pool.particles.push_back(p);
    }

    void build() {
        cull.update(frame.ctx);
        view.build(frame.scene, *frame.ctx.visibility, nullptr, nullptr, nullptr, nullptr, &live);
    }
};

} // namespace

TEST("a particle is drawn at its share of its life on its size curve and color gradient, one with no life at its end") {
    Seen seen;
    const EntityId id = seen.emitter(ParticleBlend::Additive);
    ParticleEmitter& emitter = seen.frame.scene.get<ParticleEmitter>(id);
    emitter.intensity      = 3.0f;
    emitter.lightIntensity = 10.0f;
    // A quarter through, at its end with no life at all, and just born.
    seen.particle(id, 1.0f, 4.0f);
    seen.particle(id, 0.5f, 0.0f);
    seen.particle(id, 0.0f, 2.0f);

    seen.build();

    const auto& drawn = seen.view.lists.particlesAdditive;
    REQUIRE(drawn.size() == 3);
    const float lives[] = {0.25f, 1.0f, 0.0f};
    for (size_t i = 0; i < 3; ++i) {
        const glm::vec4 color = emitter.color.at(lives[i]);
        CHECK_NEAR(drawn[i].positionSize.w, 2.0f * emitter.sizeOverLife.at(lives[i]), 1e-6f);
        CHECK_NEAR(drawn[i].color, glm::vec4(glm::vec3(color) * 3.0f, color.a), 1e-6f);
    }
    // The light they give off together is weighed by each one's alpha: the dead one gives none.
    const float shine = emitter.color.at(0.25f).a + emitter.color.at(1.0f).a + emitter.color.at(0.0f).a;
    REQUIRE(seen.view.lists.lights.size() == 1);
    CHECK_NEAR(seen.view.lists.lights[0].intensity, 10.0f * shine, 1e-4f);
    CHECK(!seen.view.lists.lights[0].inCaptures);
}

TEST("particles are listed by their blend, and none of the eye's own are drawn for a body it does not see") {
    Seen seen;
    const EntityId fire  = seen.emitter(ParticleBlend::Additive);
    const EntityId smoke = seen.emitter(ParticleBlend::Alpha);
    const EntityId haze  = seen.emitter(ParticleBlend::Distortion);
    const EntityId body  = seen.emitter(ParticleBlend::Additive, SeenBy::AllButEye);
    const EntityId away  = seen.emitter(ParticleBlend::Additive);
    for (const EntityId id : {fire, smoke, haze, body}) seen.particle(id, 0.1f, 1.0f);
    seen.particle(fire, 0.2f, 1.0f, {0.5f, 0.0f, 0.0f});
    // Behind the camera: out of its view.
    seen.particle(away, 0.1f, 1.0f, {0.0f, 0.0f, 30.0f});

    seen.build();

    CHECK(seen.view.lists.particlesAdditive.size() == 2);
    CHECK(seen.view.lists.particlesAlpha.size() == 1);
    CHECK(seen.view.lists.particlesDistortion.size() == 1);
}

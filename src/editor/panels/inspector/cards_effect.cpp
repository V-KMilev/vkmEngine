#include "panels/inspector/cards.h"

#include <imgui.h>

#include "ecs/component/effect/decal.h"
#include "ecs/component/effect/particle_emitter.h"
#include "editor_state.h"
#include "core/frame/live_particles.h"
#include "ui/editor_widgets.h"
namespace Vkm::Engine {

namespace {

/**
 * @brief Play, pause, restart and stop the viewport's preview of the world's particle effects,
 *        and whether it plays every emitter or the selection's.
 *
 * For outside a play session, where the world stands still; see HostChrome::EffectPreview.
 *
 * @param effects The editor's preview state, edited in place.
 */
void drawEffectPreviewControls(EffectPreviewState& effects) {
    const bool running = effects.playing && !effects.paused;
    if (ImGui::SmallButton(running ? "Pause" : "Play")) {
        if (!effects.playing) {
            effects.playing = true;
            effects.paused  = false;
        } else {
            effects.paused = !effects.paused;
        }
    }
    const char* tip = running
        ? "Hold the effect where it is, still drawn"
        : "Play the effect in the viewport while the world stands still; changes nothing of the scene";
    ImGui::SetItemTooltip("%s", tip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Restart")) {
        effects.playing = true;
        effects.paused  = false;
        ++effects.restarts;
    }
    ImGui::SetItemTooltip("Start it over from nothing, its burst fired again");
    ImGui::SameLine();
    ImGui::BeginDisabled(!effects.playing);
    if (ImGui::SmallButton("Stop")) {
        effects.playing = false;
        effects.paused  = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("All", &effects.all);
    ImGui::SetItemTooltip("Every emitter in the scene, not only the selection and what is under it");
}

} // namespace

CardInfo cardInfo(Tag<Decal>) {
    return {"Decal", AddSection::Effect, "A picture projected onto what lies in its box", EditorIcon::Decal};
}

void drawCard(Rows<Decal>& c, CardContext&) {
    c.row<&Decal::material>();
    // The renderer skips a decal with no material outright.
    if (!c.value().material) cardNote(Note::Danger, "No material: this projects nothing.");
    c.slider<&Decal::angleFade>();
    c.slider<&Decal::opacity>();
    c.row<&Decal::enabled>();
}

CardInfo cardInfo(Tag<ParticleEmitter>) {
    const char* hint = "Sparks, smoke, fire: particles it spawns";
    return {"Particle Emitter", AddSection::Effect, hint, EditorIcon::Particle};
}

void drawCard(Rows<ParticleEmitter>& c, CardContext& card) {
    const ParticleEmitter& e = c.value();
    // The world stands still outside a play session, so the viewport plays the effect on its own
    // while the fields below are tuned.
    if (!card.playing) drawEffectPreviewControls(card.state.effects);

    sectionLabel("Emission");
    c.row<&ParticleEmitter::emitting>();
    c.row<&ParticleEmitter::rate>("%.1f /s");
    c.row<&ParticleEmitter::ratePerMeter>("%.1f /m");
    c.row<&ParticleEmitter::maxParticles>();
    c.row<&ParticleEmitter::burst>();
    c.row<&ParticleEmitter::destroyWhenSpent>();

    sectionLabel("Shape");
    c.row<&ParticleEmitter::shape>();
    if (e.shape == EmitShape::Box)        c.row<&ParticleEmitter::halfExtents>("%.2f m");
    else if (e.shape != EmitShape::Point) c.row<&ParticleEmitter::radius>("%.2f m");
    if (e.shape == EmitShape::Cone || e.shape == EmitShape::Box) c.angle<&ParticleEmitter::angle>();
    if (e.shape != EmitShape::Point) c.row<&ParticleEmitter::fromSurface>();

    sectionLabel("Motion");
    c.row<&ParticleEmitter::lifetime>("%.2f s");
    c.row<&ParticleEmitter::speed>("%.2f m/s");
    c.slider<&ParticleEmitter::inheritVelocity>();
    c.row<&ParticleEmitter::acceleration>();
    c.row<&ParticleEmitter::drag>("%.2f /s");
    c.row<&ParticleEmitter::turbulence>();
    if (e.turbulence > 0.0f) c.row<&ParticleEmitter::turbulenceScale>("%.2f m");

    sectionLabel("Look");
    // A streak lies along its travel and a strip along its points: only a card turns.
    const bool turns = e.render == ParticleRender::Billboard;
    const bool strip = e.render == ParticleRender::Trail;
    c.row<&ParticleEmitter::blend>();
    c.row<&ParticleEmitter::size>("%.3f m");
    c.row<&ParticleEmitter::sizeOverLife>();
    c.row<&ParticleEmitter::color>();
    if (e.blend == ParticleBlend::Distortion) {
        cardNote(Note::Info, "Its alpha over its life fades its bend; the color is unread.");
        c.row<&ParticleEmitter::distortion>("%.3f");
    } else {
        c.row<&ParticleEmitter::intensity>();
    }
    if (e.blend == ParticleBlend::Alpha) c.slider<&ParticleEmitter::emission>();
    if (turns) {
        c.angle<&ParticleEmitter::rotation>("%.0f deg");
        c.angle<&ParticleEmitter::spin>("%.0f deg/s");
    }
    c.row<&ParticleEmitter::texture>();
    if (!e.texture) {
        c.slider<&ParticleEmitter::softness>();
    } else if (!strip) {
        c.row<&ParticleEmitter::sheetColumns>();
        c.row<&ParticleEmitter::sheetRows>();
        if (sheetFrames(e) > 1) {
            c.row<&ParticleEmitter::sheetFps>("%.1f");
            c.row<&ParticleEmitter::randomFrame>();
            c.row<&ParticleEmitter::sheetBlend>();
        }
    }
    if (e.blend == ParticleBlend::Alpha && !strip) c.row<&ParticleEmitter::normalMap>();

    sectionLabel("Draw");
    c.row<&ParticleEmitter::render>();
    if (e.render == ParticleRender::Stretched) c.row<&ParticleEmitter::stretch>("%.3f s");
    if (strip) c.row<&ParticleEmitter::trailLength>("%.2f s");
    c.row<&ParticleEmitter::seenBy>();

    sectionLabel("Collision");
    c.row<&ParticleEmitter::collide>();
    if (e.collide) {
        c.slider<&ParticleEmitter::bounce>();
        c.slider<&ParticleEmitter::lifeLostOnHit>();
    }

    sectionLabel("Light");
    c.row<&ParticleEmitter::lightIntensity>("%.1f");
    if (e.lightIntensity > 0.0f) {
        c.row<&ParticleEmitter::lightColor>();
        c.row<&ParticleEmitter::lightRadius>("%.1f m");
    }

    const LiveParticles::Pool* live = card.particles ? card.particles->poolOf(card.id) : nullptr;
    ImGui::TextDisabled("Live: %d particle(s).", live ? static_cast<int>(live->particles.size()) : 0);
}

} // namespace Vkm::Engine

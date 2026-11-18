#include "panels/inspector/cards.h"

#include <cstdint>

#include <glm/glm.hpp>
#include <imgui.h>

#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/light/light.h"
#include "ecs/component/light/reflection_probe.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include "ecs/scene.h"
namespace Vkm::Engine {

namespace {

/**
 * @brief Full-width Rebake button: bumps @p bakeVersion when pressed.
 *
 * @param bakeVersion The counter a bake compares against.
 * @return Whether it was pressed this frame.
 */
bool rebakeButton(uint32_t& bakeVersion) {
    if (ImGui::Button("Rebake", ImVec2(-1, 0))) {
        ++bakeVersion;
        return true;
    }
    return false;
}

} // namespace

CardInfo cardInfo(Tag<Light>) {
    return {"Light", AddSection::Light, "A sun, a lamp or a spot", EditorIcon::LightPoint};
}

void drawCard(Rows<Light>& c, CardContext& card) {
    const Light& light = c.value();
    const bool   sun   = light.type == LightType::Directional;
    // SkySystem writes the key light's turn, color and intensity; findKeyLight is how both agree on it.
    const bool sky = card.scene.environment().sky.procedural && findKeyLight(card.scene) == card.id;
    if (sky) cardNote(Note::Info, "World > Procedural Sky sets its turn, color and intensity.");

    c.row<&Light::type>();
    // Disabled, not hidden: still the light's values and the file's.
    ImGui::BeginDisabled(sky);
    c.row<&Light::color>();
    c.row<&Light::intensity>();
    ImGui::EndDisabled();
    if (!sun) c.row<&Light::radius>("%.1f m");
    if (light.type == LightType::Spot) {
        c.angle<&Light::innerConeAngle>();
        c.angle<&Light::outerConeAngle>();
    }
    c.row<&Light::castShadows>();
    if (light.castShadows && sun)  c.row<&Light::shadowDistance>("%.1f m");
    if (light.castShadows && !sun) c.row<&Light::shadowPriority>();
    // Its size, shadowed or not: it sizes the highlight as well as the shadow.
    if (sun) c.angle<&Light::sourceRadius>("%.2f deg");
    else     c.row<&Light::sourceRadius>("%.3f m");
    if (!sun) c.slider<&Light::flicker>();
    c.row<&Light::inCaptures>();
    if (light.castShadows) c.row<&Light::movable>();
    c.row<&Light::enabled>();
}

CardInfo cardInfo(Tag<ReflectionProbe>) {
    return {"Reflection Probe", AddSection::Light, "What shiny things nearby reflect", EditorIcon::Probe};
}

void drawCard(Rows<ReflectionProbe>& c, CardContext&) {
    c.row<&ReflectionProbe::halfExtents>("%.1f m");
    c.slider<&ReflectionProbe::falloff>();
    c.row<&ReflectionProbe::intensity>();

    c.rowWith<&ReflectionProbe::resolution>([](uint32_t& resolution, const FieldInfo&) {
        static const char* const LABELS[] = {"128", "256", "512", "1024"};
        static const uint32_t VALUES[]    = {128u, 256u, 512u, 1024u};
        return valueCombo("##v", LABELS, VALUES, &resolution);
    });

    // Forces a bake after a scene change; automatic re-bakes are the renderer's.
    ImGui::Spacing();
    if (rebakeButton(c.custom<&ReflectionProbe::bakeVersion>())) c.edited();
    ImGui::SetItemTooltip("Captures the scene from the entity's position");
}

CardInfo cardInfo(Tag<IrradianceVolume>) {
    const char* hint = "Baked bounce light for the space in its box";
    return {"Irradiance Volume", AddSection::Light, hint, EditorIcon::Volume};
}

void drawCard(Rows<IrradianceVolume>& c, CardContext& card) {
    const IrradianceVolume& v = c.value();
    c.row<&IrradianceVolume::halfExtents>("%.1f m");
    c.row<&IrradianceVolume::resolutionX>();
    c.row<&IrradianceVolume::resolutionY>();
    c.row<&IrradianceVolume::resolutionZ>();
    c.row<&IrradianceVolume::intensity>();
    c.row<&IrradianceVolume::blendDistance>("%.2f m");

    ImGui::Spacing();
    if (rebakeButton(c.custom<&IrradianceVolume::bakeVersion>())) c.edited();
    ImGui::SetItemTooltip("The editor stops until the bake ends; lights and the sky need none");
    // Six captures a probe: the backend's bake takes one cube of surfels. The clamped grid is
    // what is baked and allocated; the raw fields are the file's.
    const glm::uvec3 grid  = IrradianceVolume::clampedResolution(v);
    const uint32_t   count = grid.x * grid.y * grid.z;
    ImGui::TextDisabled("%u probes, %u scene captures.", count, count * 6u);
    if (grid != glm::uvec3(v.resolutionX, v.resolutionY, v.resolutionZ)) {
        const uint32_t most = IrradianceVolume::MAX_PROBES;
        cardNote(Note::Info, "Baked as %u x %u x %u: at most %u probes.", grid.x, grid.y, grid.z, most);
    }

    // A scene lights from one volume (findIrradianceVolume); a second bakes nothing.
    const EntityId lit = findIrradianceVolume(card.scene);
    if (lit && lit != card.id) {
        char litName[64];
        getEntityDisplayName(card.scene, lit, litName, sizeof(litName));
        cardNote(Note::Warning, "Not the scene's volume: '%s' is baked.", litName);
    }
}

} // namespace Vkm::Engine

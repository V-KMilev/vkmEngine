#include "panels/inspector/cards.h"

#include <filesystem>
#include <memory>
#include <string>

#include <imgui.h>

#include "undo/component_commands.h"
#include "ecs/component/landscape/water.h"
#include "ecs/settings/environment.h"
#include "ecs/settings/physics_settings.h"
#include "ecs/scene.h"
#include "ecs/water/swell.h"
#include "editor_state.h"
#include "io/project/roots.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief A World card over @p value, a setting the Scene holds outside any entity: never removed,
 *        and an edit is one SceneValueEditCommand.
 *
 * @param card  The context.
 * @param title The card's heading.
 * @param icon  Its icon.
 * @param value The scene's setting.
 * @param label The history's name for an edit; kept by pointer.
 * @param rows  Draws the card's rows over @p value.
 */
template <typename S, typename Fn>
void worldCard(CardContext& card, const char* title, EditorIcon icon, S& value, const char* label, Fn rows) {
    if (beginComponentCard(title, icon, true)) {
        const S before = value;
        const AssetSlots slots{card.resources, card.state};
        Rows<S>          edited(value, {}, &slots);
        rows(edited);
        if (edited.changed()) {
            card.state.pushStep(std::make_unique<SceneValueEditCommand<S>>(before, value, label));
        }
    }
    endComponentCard();
}

/// The skybox's HDR, picked from the project's environments: project-relative, as loadHDRImage resolves it.
bool pickHdr(std::string& path, FilePicker& picker) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(path.empty() ? "(none)" : path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse...")) {
        FilePicker::Options options;
        options.title      = "Pick Environment HDR";
        options.root       = Roots::envs();
        options.extensions = {".hdr"};
        picker.open(options);
    }
    std::string picked;
    if (!picker.draw(picked)) return false;
    const std::string relative = std::filesystem::path(picked).generic_string();
    if (relative == path) return false;
    path = relative;
    return true;
}

} // namespace

void drawWorldCards(CardContext& card) {
    Environment& env = card.scene.environment();

    const EditorIcon world = EditorIcon::SpaceWorld;
    worldCard(card, "Environment", world, env, "Edit Environment", [&](Rows<Environment>& c) {
        c.nested<&Environment::sky>([&](Rows<SkySettings>& sky) {
            const auto browse = [&](std::string& path, const FieldInfo&) {
                return pickHdr(path, card.memory.environmentHdr);
            };
            sky.rowWith<&SkySettings::hdrPath>(browse);
            sky.row<&SkySettings::showSkybox>();
            sky.slider<&SkySettings::intensity>();
        });
    });

    // Live: a changed value re-bakes the lighting at once, so a slow drag of one hitches each frame;
    // the sun dragged slowly is followed over frames instead.
    const EditorIcon sun = EditorIcon::LightDir;
    worldCard(card, "Procedural Sky", sun, env, "Edit Procedural Sky", [&](Rows<Environment>& c) {
        const bool on = env.sky.procedural;
        c.nested<&Environment::sky>([&](Rows<SkySettings>& sky) {
            sky.row<&SkySettings::procedural>();
            ImGui::BeginDisabled(!on);
            sky.slider<&SkySettings::sunElevation>("%.0f deg");
            sky.slider<&SkySettings::sunAzimuth>("%.0f deg");
            // The sky owns the key light while on, so its daylight look is authored here.
            sky.row<&SkySettings::lightColor>();
            sky.slider<&SkySettings::lightIntensity>();
            sky.slider<&SkySettings::mie>();
            sky.slider<&SkySettings::sunDiscIntensity>("%.1f");
            ImGui::EndDisabled();
        });
        // Night takes over below the horizon on its own; only its look is set here.
        ImGui::BeginDisabled(!on);
        c.nested<&Environment::night>([&](Rows<NightSkySettings>& night) {
            night.row<&NightSkySettings::radiance>("%.3f", Shown::Color);
            night.slider<&NightSkySettings::moonIntensity>("%.1f");
            night.row<&NightSkySettings::moonlightColor>();
            night.slider<&NightSkySettings::moonlightIntensity>();
        });
        ImGui::EndDisabled();
    });

    worldCard(card, "Volumetric Fog", EditorIcon::Fog, env, "Edit Volumetric Fog", [&](Rows<Environment>& c) {
        c.nested<&Environment::fog>([&](Rows<FogSettings>& fog) {
            fog.row<&FogSettings::enabled>();
            ImGui::BeginDisabled(!env.fog.enabled);
            fog.slider<&FogSettings::density>("%.3f");
            fog.row<&FogSettings::height>("%.1f m");
            fog.slider<&FogSettings::heightFalloff>("%.3f");
            fog.slider<&FogSettings::anisotropy>();
            fog.row<&FogSettings::albedo>(nullptr, Shown::Color);
            fog.row<&FogSettings::maxDistance>("%.0f m");
            ImGui::EndDisabled();
        });
    });

    worldCard(card, "Acoustics", EditorIcon::Listener, env, "Edit Acoustics", [&](Rows<Environment>& c) {
        c.nested<&Environment::acoustics>([&](Rows<AcousticsSettings>& acoustics) {
            acoustics.row<&AcousticsSettings::enabled>();
            ImGui::BeginDisabled(!env.acoustics.enabled);
            acoustics.slider<&AcousticsSettings::reverb>();
            ImGui::EndDisabled();
        });
    });

    worldCard(card, "Waves", EditorIcon::Water, env, "Edit Waves", [&](Rows<Environment>& c) {
        c.nested<&Environment::waves>([&](Rows<WaveSettings>& waves) {
            waves.nested<&WaveSettings::shape>([&](Rows<WaveShape>& shape) {
                shape.row<&WaveShape::windSpeed>("%.1f m/s");
                shape.row<&WaveShape::windDirection>("%.0f deg");
                shape.row<&WaveShape::fetch>("%.1f km");
                shape.row<&WaveShape::depth>("%.1f m");
                shape.slider<&WaveShape::spread>();
                shape.slider<&WaveShape::choppiness>();
                shape.row<&WaveShape::heightScale>();
                shape.row<&WaveShape::minWavelength>("%.2f m");
                shape.row<&WaveShape::maxWavelength>("%.1f m");
                shape.row<&WaveShape::seed>();
            });
            waves.row<&WaveSettings::calmCenter>("%.1f m");
            waves.row<&WaveSettings::calmRadius>("%.1f m");
            waves.row<&WaveSettings::stillRadius>("%.1f m");
        });
        // Made again only when their shape changes: a swell is costly.
        if (card.memory.waveShape != env.waves.shape) {
            const std::shared_ptr<const Swell> swell = Swell::of(env.waves.shape);
            card.memory.waveHeight = swell ? swell->significantHeight() : 0.0f;
            card.memory.waveShape  = env.waves.shape;
        }
        ImGui::TextDisabled("Significant wave height %.2f m.", card.memory.waveHeight);
        // The waves move water that takes a share of them, and nothing else.
        int surfaces = 0;
        card.scene.forEach<Water>([&](EntityId, const Water& water) {
            if (water.waves > 0.0f) ++surfaces;
        });
        if (surfaces == 0) cardNote(Note::Info, "No water takes a share of these waves.");
    });

    PhysicsSettings& physics = card.scene.physics();
    worldCard(card, "Physics", EditorIcon::Weight, physics, "Edit Physics", [&](Rows<PhysicsSettings>& c) {
        c.row<&PhysicsSettings::gravity>();
        c.row<&PhysicsSettings::solverIterations>();
    });
}

} // namespace Vkm::Engine

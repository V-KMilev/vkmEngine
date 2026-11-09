// Quality tiers and a player's graphics choices: what a settings screen changes, and what it can
// never change.

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/graphics.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

namespace {

constexpr GraphicsPreset TIERS[] = {
    GraphicsPreset::Performance, GraphicsPreset::Low, GraphicsPreset::Medium, GraphicsPreset::High, GraphicsPreset::Max
};

/// A project's look with every effect on.
RenderSettings everyEffect() {
    RenderSettings look;
    look.gtao = look.ssr = look.bloom = look.softShadows = true;
    return look;
}

bool sameChoices(const RenderSettings& a, const RenderSettings& b) {
    return a.msaaSamples == b.msaaSamples && a.shadowResolution == b.shadowResolution
        && a.textureAnisotropy == b.textureAnisotropy && a.renderScale == b.renderScale
        && a.ssrHalfResolution == b.ssrHalfResolution && a.gtao == b.gtao && a.ssr == b.ssr
        && a.bloom == b.bloom && a.softShadows == b.softShadows && a.occlusionCulling == b.occlusionCulling;
}

} // namespace

TEST("a quality tier never turns on an effect the project has off, and a higher one brings back what a lower one took") {
    RenderSettings look = everyEffect();
    look.ssr  = false;
    look.gtao = false;
    Graphics graphics;
    graphics.setProject(look);

    graphics.setQuality(GraphicsPreset::Max);
    CHECK(!graphics.settings().ssr);
    CHECK(!graphics.settings().gtao);
    CHECK(graphics.settings().bloom);
    CHECK(graphics.settings().softShadows);

    // Nor does a setting a game writes by hand.
    RenderSettings asked = graphics.settings();
    asked.ssr = true;
    graphics.set(asked);
    CHECK(!graphics.settings().ssr);

    graphics.setQuality(GraphicsPreset::Performance);
    CHECK(!graphics.settings().bloom);
    CHECK(!graphics.settings().softShadows);
    graphics.setQuality(GraphicsPreset::Max);
    CHECK(graphics.settings().bloom);
    CHECK(graphics.settings().softShadows);
}

TEST("each tier sets every field a tier owns, and quality() names the tier the frame is drawn at") {
    for (const bool allEffects : {true, false}) {
        RenderSettings look = everyEffect();
        if (!allEffects) look.ssr = look.softShadows = false;
        Graphics graphics;
        graphics.setProject(look);

        for (const GraphicsPreset tier : TIERS) {
            graphics.setQuality(tier);
            CHECK(graphics.quality() == std::optional<GraphicsPreset>(tier));
        }

        // Performance and Max differ in every field a tier sets, so a field a tier forgot shows.
        graphics.setQuality(GraphicsPreset::Max);
        const RenderSettings max = graphics.settings();
        graphics.setQuality(GraphicsPreset::Performance);
        const RenderSettings low = graphics.settings();
        CHECK(low.msaaSamples < max.msaaSamples);
        CHECK(low.shadowResolution < max.shadowResolution);
        CHECK(low.textureAnisotropy < max.textureAnisotropy);
        CHECK(low.renderScale < max.renderScale);
        CHECK(low.ssrHalfResolution != max.ssrHalfResolution);
        CHECK((!low.gtao && !low.ssr && !low.bloom && !low.softShadows));
        CHECK((max.gtao && max.bloom));
        CHECK(max.ssr == allEffects);
        CHECK(max.softShadows == allEffects);
        graphics.setQuality(GraphicsPreset::Max);
        CHECK(sameChoices(graphics.settings(), max));

        // A field a tier sets, changed by hand, is no tier; one it does not set leaves the tier.
        RenderSettings scaled = graphics.settings();
        scaled.exposure         = 2.5f;
        scaled.occlusionCulling = !scaled.occlusionCulling;
        graphics.set(scaled);
        CHECK(graphics.quality() == std::optional<GraphicsPreset>(GraphicsPreset::Max));
        scaled.renderScale = 0.8f;
        graphics.set(scaled);
        CHECK(!graphics.quality().has_value());
    }
}

TEST("a player's graphics choices are kept in their file and drawn with at the next start") {
    const std::filesystem::path file = scratch() / "player" / "graphics.json";
    RenderSettings look = everyEffect();
    look.exposure = 0.5f;
    {
        Graphics graphics;
        graphics.setProject(look);
        graphics.keepChoicesIn(file);
        graphics.setQuality(GraphicsPreset::Low);
        RenderSettings culled = graphics.settings();
        culled.occlusionCulling = true;
        graphics.set(culled);
        graphics.setWindowMode(WindowMode::Fullscreen);
    }

    Graphics next;
    next.setProject(look);
    CHECK(next.quality() != std::optional<GraphicsPreset>(GraphicsPreset::Low));
    next.keepChoicesIn(file);
    CHECK(next.quality() == std::optional<GraphicsPreset>(GraphicsPreset::Low));
    CHECK(next.settings().occlusionCulling);
    CHECK(next.chosenWindowMode() == std::optional<WindowMode>(WindowMode::Fullscreen));
    // The project's look is not a choice: it is never kept for the player.
    CHECK(next.settings().exposure == 0.5f);
    CHECK(countInLog("graphics.json") == 0);
}

TEST("a damaged graphics file costs the player their choices, never the project's look") {
    const std::filesystem::path file = scratch() / "graphics.json";
    const RenderSettings look = everyEffect();

    // A sample count the renderer does not take: every choice in it is refused.
    writeText(file, R"({"msaaSamples": 3, "bloom": false, "renderScale": 0.75})");
    Graphics refused;
    refused.setProject(look);
    refused.keepChoicesIn(file);
    CHECK(sameChoices(refused.settings(), look));
    CHECK(countInLog("does not take") == 1);

    // A choice of the wrong type keeps the project's alone; the rest are taken.
    writeText(file, R"({"bloom": "off", "renderScale": 0.75, "window": "Sideways"})");
    Graphics kept;
    kept.setProject(look);
    kept.keepChoicesIn(file);
    CHECK(kept.settings().bloom);
    CHECK(kept.settings().renderScale == 0.75f);
    CHECK(!kept.chosenWindowMode().has_value());
    CHECK(countInLog("holds bloom") == 1);
    CHECK(countInLog("holds window") == 1);

    // Not JSON at all: a fresh start, said once.
    writeText(file, "{ not json");
    Graphics fresh;
    fresh.setProject(look);
    fresh.keepChoicesIn(file);
    CHECK(sameChoices(fresh.settings(), look));
    CHECK(countInLog("starting afresh") == 1);
}

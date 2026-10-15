#include "support.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "system/ocean/ocean_surface.h"
#include "system/ocean/ocean_system.h"

namespace {

// An open sea: a fresh breeze over a hundred kilometres, the band wide enough to hold
// nearly all its energy.
Ocean openSea() {
    Ocean sea;
    sea.windSpeed     = 10.0f;
    sea.fetch         = 100.0f;
    sea.minWavelength = 0.5f;
    sea.maxWavelength = 2000.0f;
    sea.waveCount     = Ocean::MAX_WAVES;
    return sea;
}

// Where the water resting at @p rest goes, as a world point.
glm::vec3 movedPoint(const OceanSurface& surface, const glm::vec2& rest) {
    const glm::vec3 moved = surface.displacement(rest);
    return glm::vec3(rest.x + moved.x, moved.y, rest.y + moved.z);
}

// The spectrum is JONSWAP's (Hasselmann et al. 1973): its energy peaks at 22 (g^2 / UF)^(1/3)
// radians a second, and the field fit of the same measurements gives Hs = 0.0016 U^2/g
// sqrt(gF/U^2). The parametric spectrum, peak enhancement and all, stands about a third higher
// than that fit, so the height is held to within 40% of it and the peak to its band.
void testTheSeaIsAsHighAsTheWindMakesIt() {
    std::printf("How high the wind raises the sea:\n");

    const Ocean   sea      = openSea();
    const float   wind     = sea.windSpeed;
    const float   fetch    = sea.fetch * 1000.0f;
    const float   expected = 0.0016f * wind * wind / 9.81f * std::sqrt(9.81f * fetch / (wind * wind));
    const OceanSurface surface = OceanSurface::at(sea, glm::vec3(0.0f), 0.0);
    const float   measured = surface.significantHeight();
    std::printf("      Hs %.2f m against the fetch law's %.2f m\n", measured, expected);
    const bool close = std::fabs(measured - expected) < 0.4f * expected;
    check("a fresh breeze over open water raises JONSWAP's sea", close);

    // Bands are even in log frequency, so the tallest wave is where w S(w) peaks, just above
    // the spectrum's own peak.
    const GerstnerWave* tallest = &surface.waves[0];
    for (uint32_t i = 1; i < surface.count; ++i) {
        if (surface.waves[i].amplitude > tallest->amplitude) tallest = &surface.waves[i];
    }
    const float peak = 22.0f * std::cbrt(9.81f * 9.81f / (wind * fetch));
    const float rolls = tallest->frequency;
    std::printf("      the tallest wave rolls at %.2f rad/s; the spectrum peaks at %.2f\n", rolls, peak);
    check("  its tallest waves are the spectrum's peak", rolls > peak && rolls < 1.3f * peak);

    Ocean stronger = sea;
    stronger.windSpeed = 20.0f;
    const float windier = OceanSurface::at(stronger, glm::vec3(0.0f), 0.0).significantHeight();
    check("  twice the wind raises it more", windier > 1.5f * measured);

    Ocean shallow = sea;
    shallow.depth = 3.0f;
    const float shallower = OceanSurface::at(shallow, glm::vec3(0.0f), 0.0).significantHeight();
    check("  shallow water holds less of it", shallower < 0.8f * measured);

    Ocean calm = sea;
    calm.windSpeed = 0.0f;
    const OceanSurface still = OceanSurface::at(calm, glm::vec3(0.0f), 0.0);
    check("  no wind, no waves", still.count == 0 && still.displacement({3.0f, 4.0f}) == glm::vec3(0.0f));
}

// Each wave's length and speed must be one wave's: w^2 = g k tanh(k d). Shallow water is where
// solving for k is hard - long waves over a few metres, k d far below one.
void testEveryWaveRollsAtItsDepthsSpeed() {
    std::printf("How fast each wave rolls over its depth:\n");

    float worst = 0.0f;
    for (const float depth : {0.0f, 0.5f, 3.0f, 20.0f, 400.0f}) {
        Ocean sea = openSea();
        sea.depth = depth;
        const OceanSurface surface = OceanSurface::at(sea, glm::vec3(0.0f), 0.0);
        for (uint32_t i = 0; i < surface.count; ++i) {
            const double k     = surface.waves[i].wavenumber;
            const double omega = surface.waves[i].frequency;
            const double t     = depth > 0.0f ? std::tanh(k * depth) : 1.0;
            worst = std::max(worst, float(std::abs(9.81 * k * t - omega * omega) / (omega * omega)));
        }
    }
    std::printf("      worst w^2 off g k tanh(kd): %.2e\n", double(worst));
    check("every wave's length is the one its speed has at its depth", worst < 1e-5f);
}

// Every end that builds the sea at the same moment must see the same water: the waves are
// seeded, never drawn from the clock-seeded generator.
void testTheSameSeaIsTheSameEverywhere() {
    std::printf("The same sea twice:\n");

    const Ocean sea = openSea();
    const OceanSurface a = OceanSurface::at(sea, glm::vec3(0.0f), 12.5);
    const OceanSurface b = OceanSurface::at(sea, glm::vec3(0.0f), 12.5);
    const glm::vec2 point = {7.0f, -3.0f};
    check("built twice at one moment, the same water", a.displacement(point) == b.displacement(point));

    Ocean other = sea;
    other.seed = 2;
    const OceanSurface c = OceanSurface::at(other, glm::vec3(0.0f), 12.5);
    check("  another seed, other waves", c.displacement(point) != a.displacement(point));
}

// Phases are wrapped in double: in float, a wave a few seconds long would step by most of a
// radian a frame by the end of a long session.
void testTheSeaStaysSmoothForDays() {
    std::printf("A sea a million seconds in:\n");

    const Ocean      sea   = openSea();
    const glm::vec2  point = {11.0f, 5.0f};
    float            worst = 0.0f;
    for (int frame = 0; frame < 60; ++frame) {
        const double    t    = 1.0e6 + frame / 60.0;
        const glm::vec3 now  = OceanSurface::at(sea, glm::vec3(0.0f), t).displacement(point);
        const glm::vec3 next = OceanSurface::at(sea, glm::vec3(0.0f), t + 1.0 / 60.0).displacement(point);
        worst = std::max(worst, glm::length(next - now));
    }
    std::printf("      the most the water moves in a frame: %.3f m\n", worst);
    check("a frame moves the water a centimetre or two, as it does at the start", worst < 0.03f);
}

// The crests may sharpen but never fold over: where they would, the drawn surface would turn
// inside out and every height asked of it would have two answers.
void testTheSurfaceNeverFolds() {
    std::printf("Steep crests:\n");

    Ocean storm = openSea();
    storm.windSpeed  = 25.0f;
    storm.choppiness = 1.0f;
    const OceanSurface surface = OceanSurface::at(storm, glm::vec3(0.0f), 3.0);
    float least = 1.0f;
    for (int x = -60; x <= 60; ++x) {
        for (int z = -60; z <= 60; ++z) {
            const glm::vec2 rest = {x * 0.7f, z * 0.7f};
            const glm::vec3 p    = movedPoint(surface, rest);
            const glm::vec3 px   = movedPoint(surface, rest + glm::vec2(0.01f, 0.0f));
            const glm::vec3 pz   = movedPoint(surface, rest + glm::vec2(0.0f, 0.01f));
            // How much a patch of the plane is squeezed on its way to the surface.
            const glm::vec2 dx = glm::vec2(px.x - p.x, px.z - p.z) / 0.01f;
            const glm::vec2 dz = glm::vec2(pz.x - p.x, pz.z - p.z) / 0.01f;
            least = std::min(least, dx.x * dz.y - dx.y * dz.x);
        }
    }
    std::printf("      the tightest squeeze leaves %.2f of a patch\n", least);
    check("a full storm at full choppiness never folds the surface over", least > 0.0f);
    check("  but it does draw its crests in", least < 0.9f);
}

// heightAt and normalAt answer for a point fixed in the world, where the water over it came from
// somewhere else: they must agree with the surface the GPU draws, point for point.
void testAWorldPointFindsTheWaterOverIt() {
    std::printf("The water over a point:\n");

    Ocean sea = openSea();
    sea.choppiness = 1.0f;
    sea.surges[0]  = {{5.0f, -3.0f}, glm::normalize(glm::vec2(1.0f, 0.5f)), 1.2f, 2.0f, 10.0f};
    const OceanSurface surface = OceanSurface::at(sea, glm::vec3(0.0f), 7.0);
    float worstHeight = 0.0f;
    float worstNormal = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const glm::vec2 rest  = {std::fmod(i * 7.31f, 80.0f) - 40.0f, std::fmod(i * 3.17f, 80.0f) - 40.0f};
        const glm::vec3 point = movedPoint(surface, rest);
        worstHeight = std::max(worstHeight, std::fabs(surface.heightAt({point.x, point.z}) - point.y));

        // The normal against the surface's own slope, taken across the drawn points.
        const glm::vec2 at  = {point.x, point.z};
        const glm::vec2 dx  = {0.02f, 0.0f};
        const glm::vec2 dz  = {0.0f, 0.02f};
        const float     sx  = (surface.heightAt(at + dx) - surface.heightAt(at - dx)) / 0.04f;
        const float     sz  = (surface.heightAt(at + dz) - surface.heightAt(at - dz)) / 0.04f;
        const glm::vec3 slope = glm::normalize(glm::vec3(-sx, 1.0f, -sz));
        worstNormal = std::max(worstNormal, glm::length(surface.normalAt(at) - slope));
    }
    std::printf("      heights within %.4f m, normals within %.4f\n", worstHeight, worstNormal);
    check("the height over a drawn point is the drawn point's, within a centimetre", worstHeight < 0.01f);
    check("  and the normal there is the surface's slope", worstNormal < 0.02f);
    check("  a surge among them too", surface.surges[0].height > 0.0f);

    // A surge lifts the water at its crest by its height, over the wind's sea there.
    const glm::vec2 crest = sea.surges[0].centre;
    const float     lift  = surface.heightAt(crest) - surface.withoutSurges().heightAt(crest);
    std::printf("      the surge's crest stands %.3f m over the sea\n", lift);
    check("a surge raises the water at its crest by its height", std::fabs(lift - 1.2f) < 0.05f);
    const glm::vec2 past = {40.0f, 40.0f};
    check("  and none past its end", surface.heightAt(past) == surface.withoutSurges().heightAt(past));
}

// A surge is set by gameplay as it runs, its direction however it came: only the way it points
// counts, and one pointing nowhere is none. Where two cross they add, and the cull must reach that.
void testASurgeRollsTheWayItPoints() {
    std::printf("Which way a surge rolls:\n");

    Ocean calm;
    calm.windSpeed = 0.0f;
    calm.surges[0] = {{0.0f, 0.0f}, {0.0f, 1.0f}, 1.0f, 2.0f, 8.0f};
    const glm::vec2 off  = {1.5f, 0.7f};
    const float     unit = OceanSurface::at(calm, glm::vec3(0.0f), 0.0).heightAt(off);
    calm.surges[0].direction = {0.0f, 3.0f};
    const float longer = OceanSurface::at(calm, glm::vec3(0.0f), 0.0).heightAt(off);
    check("a surge's direction is only the way it points", unit > 0.0f && nearly(longer, unit));
    calm.surges[0].direction = glm::vec2(0.0f);
    const OceanSurface nowhere = OceanSurface::at(calm, glm::vec3(0.0f), 0.0);
    check("  and one pointing nowhere raises nothing", nowhere.heightAt(glm::vec2(0.0f)) == 0.0f);

    calm.surges[0] = {{0.0f, 0.0f}, {0.0f, 1.0f}, 1.0f, 2.0f, 8.0f};
    calm.surges[1] = {{0.0f, 0.0f}, {1.0f, 0.0f}, 0.5f, 2.0f, 8.0f};
    const OceanSurface crossed = OceanSurface::at(calm, glm::vec3(0.0f), 0.0);
    const float        top     = crossed.heightAt(glm::vec2(0.0f));
    check("two crossing add", nearly(top, 1.5f));
    check("  and the farthest the water reaches counts both", crossed.farthestReach().y >= top);
}

// Past stillRadius from the ocean's Transform the water lies flat; inside calmRadius it is whole.
void testTheWavesDieAwayFromTheCentre() {
    std::printf("Where the waves reach:\n");

    Ocean sea = openSea();
    sea.calmRadius  = 50.0f;
    sea.stillRadius = 100.0f;
    const OceanSurface surface = OceanSurface::at(sea, glm::vec3(10.0f, 0.0f, 10.0f), 4.0);
    check("past the still radius the sea is flat", surface.displacement({10.0f, 120.0f}) == glm::vec3(0.0f));
    check("  inside the calm radius it is whole", nearly(surface.reach({30.0f, 20.0f}), 1.0f));
    const float between = surface.reach({10.0f, 85.0f});
    check("  and between, it fades", between > 0.0f && between < 1.0f);

    // Where it fades the surface still has one normal: its own slope, the fade's included.
    Ocean edge = openSea();
    edge.choppiness  = 1.0f;
    edge.calmRadius  = 40.0f;
    edge.stillRadius = 60.0f;
    const OceanSurface fading = OceanSurface::at(edge, glm::vec3(0.0f), 4.0);
    float worst = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float     angle = i * 0.37f;
        const float     out   = 42.0f + std::fmod(i * 1.7f, 16.0f);
        const glm::vec2 rest  = out * glm::vec2(std::cos(angle), std::sin(angle));
        const glm::vec3 point = movedPoint(fading, rest);
        const glm::vec2 at    = {point.x, point.z};
        const glm::vec2 dx    = {0.02f, 0.0f};
        const glm::vec2 dz    = {0.0f, 0.02f};
        const float     sx    = (fading.heightAt(at + dx) - fading.heightAt(at - dx)) / 0.04f;
        const float     sz    = (fading.heightAt(at + dz) - fading.heightAt(at - dz)) / 0.04f;
        const glm::vec3 slope = glm::normalize(glm::vec3(-sx, 1.0f, -sz));
        worst = std::max(worst, glm::length(fading.normalAt(at) - slope));
    }
    std::printf("      across the fade, normals within %.4f of the slope\n", worst);
    check("  where it fades, the normal is still the surface's slope", worst < 0.02f);
}

// The ripples are the spectrum on below the waves, for the light alone, drawn over wavenumber:
// over a band, that density must hold the slope the spectrum gives it, and adding them moves no wave.
void testTheRipplesAreTheSpectrumBelowTheWaves() {
    std::printf("The ripples:\n");

    Ocean sea = openSea();
    sea.minWavelength  = 3.0f;
    sea.shortestRipple = 0.03f;
    const OceanRipples ripples = OceanRipples::at(sea, 5.0);
    check(
        "the band runs from the shortest wave to the shortest ripple",
        ripples.any() && nearly(ripples.longest * 3.0f, glm::two_pi<float>())
            && nearly(ripples.shortest * 0.03f, glm::two_pi<float>())
    );

    // The density's slope, k^2 times it over the band's annulus, against the spectrum's own.
    constexpr int RINGS  = 400;
    constexpr int ANGLES = 720;
    const double  ratio  = double(ripples.shortest) / ripples.longest;
    double        slope  = 0.0;
    for (int i = 0; i < RINGS; ++i) {
        const double from = ripples.longest * std::pow(ratio, double(i) / RINGS);
        const double to   = ripples.longest * std::pow(ratio, double(i + 1) / RINGS);
        const double k    = 0.5 * (from + to);
        for (int j = 0; j < ANGLES; ++j) {
            const double    angle = glm::two_pi<double>() * (j + 0.5) / ANGLES;
            const glm::vec2 at    = glm::vec2(float(k * std::cos(angle)), float(k * std::sin(angle)));
            slope += k * k * ripples.density(at) * k * (to - from) * glm::two_pi<double>() / ANGLES;
        }
    }
    const double expected = ripples.slopeVariance(ripples.longest, ripples.shortest);
    std::printf("      slope variance %.5f over wavenumber, %.5f by the spectrum\n", slope, expected);
    const bool holds = std::fabs(slope - expected) < 0.02 * expected;
    check("  over wavenumber they hold the slope the spectrum gives", holds);
    const glm::vec2 k = ripples.wind * 10.0f;
    check("  most of them roll with the wind", ripples.density(k) > 10.0f * ripples.density(-k));

    Ocean shorter = sea;
    shorter.shortestRipple = 0.01f;
    const glm::vec2 point = {4.0f, 9.0f};
    const glm::vec3 with    = OceanSurface::at(sea, glm::vec3(0.0f), 5.0).displacement(point);
    const glm::vec3 further = OceanSurface::at(shorter, glm::vec3(0.0f), 5.0).displacement(point);
    check("  more of them moves no wave", with == further);

    Ocean calm = sea;
    calm.windSpeed = 0.0f;
    check("  no wind, no ripples", !OceanRipples::at(calm, 5.0).any());
}

// OceanSystem builds the one sea everything reads: published every frame, an ocean or none, and
// inside a tick at the tick's own time, so what a tick floats on it does not depend on the frame.
void testTheOceanSystemBuildsTheOneSea() {
    std::printf("The scene's one sea:\n");

    Scene scene;
    TestFrame frame(scene);
    frame.clock.setTickRate(60);
    OceanSystem system;
    system.update(frame.ctx);
    check("with no ocean, the frame still says so", frame.ctx.ocean && !frame.ctx.ocean->present);

    const Ocean     sea    = openSea();
    const glm::vec3 centre = {3.0f, -1.0f, 2.0f};
    const EntityId  entity = scene.createEntity();
    scene.add(entity, Transform{centre, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f)});
    scene.add(entity, sea);
    frame.clock.requestStep(7);
    while (frame.clock.consumeFixedStep()) {}

    frame.ctx.net.beginTick(frame.clock.getTick(), InputCommand{}, 0, frame.ctx.input.actionFingerprint());
    system.fixedUpdate(frame.ctx);
    frame.ctx.net.endTick(scene, frame.clock.getTick());
    const double       tick  = 7.0 * static_cast<double>(frame.clock.getFixedStep());
    const OceanSurface again = OceanSurface::at(sea, centre, tick);
    const glm::vec2    point = {4.0f, -6.0f};
    const OceanData&   built = *frame.ctx.ocean;
    check("an ocean is built where it stands", built.present && built.level == centre.y);
    check(
        "  inside a tick, at that tick's own time",
        built.surface.count == again.count && built.surface.heightAt(point) == again.heightAt(point)
    );
}

} // namespace

void runOceanTests() {
    testTheSeaIsAsHighAsTheWindMakesIt();
    testEveryWaveRollsAtItsDepthsSpeed();
    testTheSameSeaIsTheSameEverywhere();
    testTheSeaStaysSmoothForDays();
    testTheSurfaceNeverFolds();
    testAWorldPointFindsTheWaterOverIt();
    testASurgeRollsTheWayItPoints();
    testTheWavesDieAwayFromTheCentre();
    testTheRipplesAreTheSpectrumBelowTheWaves();
    testTheOceanSystemBuildsTheOneSea();
}

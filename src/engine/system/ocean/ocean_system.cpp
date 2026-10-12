#include "system/ocean/ocean_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "debug/profiler.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/ocean.h"
#include "ecs/scene.h"
#include "net/net_session.h"

namespace Vkm::Engine {

void OceanSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("OceanSystem");
    build(ctx);
}

void OceanSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("OceanSystem::fixed");
    build(ctx);
}

void OceanSystem::build(FrameContext& ctx) {
    ctx.ocean = &m_ocean;
    m_ocean.present = false;

    const EntityId id = findOcean(ctx.scene);
    if (!id) return;

    const double    seconds = ctx.net.worldTime(ctx.clock);
    const Ocean&    ocean   = ctx.scene.get<Ocean>(id);
    const float     repeat  = std::max(ocean.detailScale, 1e-3f);
    const glm::vec3 centre  = resolvedWorldPosition(ctx.scene, id, ctx.scene.get<Transform>(id));
    m_ocean.present     = true;
    m_ocean.surface     = OceanSurface::at(ocean, centre, seconds);
    m_ocean.ripples     = OceanRipples::at(ocean, seconds);
    m_ocean.level       = centre.y;
    m_ocean.foamColor   = ocean.foamColor;
    m_ocean.foam        = ocean.foam;
    m_ocean.waterline   = std::max(ocean.waterlineFoam, 0.0f);
    m_ocean.detailScale = repeat;

    // The maps drift downwind at the speed of a wave as long as one repeat, wrapped in double
    // so they stay smooth however long the sea has run.
    const double heading = glm::radians(double(ocean.windDirection));
    const double speed   = std::sqrt(OceanSurface::GRAVITY * repeat / glm::two_pi<double>());
    const double moved   = speed * seconds / repeat;
    const auto   wrap    = [](double repeats) { return float(repeats - std::floor(repeats)); };
    m_ocean.detailDrift = glm::vec2(wrap(std::sin(heading) * moved), wrap(std::cos(heading) * moved));
}

} // namespace Vkm::Engine

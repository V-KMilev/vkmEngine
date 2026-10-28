#include "system/water/water_data.h"

#include <cmath>

#include "system/ocean/ocean_data.h"

namespace Vkm::Engine {

bool WaterBody::sample(float x, float z, WaterCell& out) const {
    if (columns == 0 || rows == 0) return false;
    // Cell (i, j)'s middle is at corner + (i + 0.5) cells.
    const float u  = (x - corner.x) / cell - 0.5f;
    const float v  = (z - corner.y) / cell - 0.5f;
    const float fu = std::floor(u);
    const float fv = std::floor(v);
    const auto  at = [&](float i, float j) -> const WaterCell* {
        if (i < 0.0f || j < 0.0f || i >= float(columns) || j >= float(rows)) return nullptr;
        const WaterCell& c = cells[size_t(j) * columns + size_t(i)];
        return c.wet ? &c : nullptr;
    };

    // Between the four middles round the point where all four hold water; else the cell it is in.
    const WaterCell* c00 = at(fu, fv);
    const WaterCell* c10 = at(fu + 1.0f, fv);
    const WaterCell* c01 = at(fu, fv + 1.0f);
    const WaterCell* c11 = at(fu + 1.0f, fv + 1.0f);
    if (c00 && c10 && c01 && c11) {
        const float tu = u - fu;
        const float tv = v - fv;
        const float w00 = (1.0f - tu) * (1.0f - tv);
        const float w10 = tu * (1.0f - tv);
        const float w01 = (1.0f - tu) * tv;
        const float w11 = tu * tv;
        out.wet    = true;
        out.height = c00->height * w00 + c10->height * w10 + c01->height * w01 + c11->height * w11;
        const auto blend = [&](glm::vec3 WaterCell::* field) {
            return c00->*field * w00 + c10->*field * w10 + c01->*field * w01 + c11->*field * w11;
        };
        out.flow   = blend(&WaterCell::flow);
        out.normal = glm::normalize(blend(&WaterCell::normal));
        return true;
    }
    const WaterCell* in = at(std::floor(u + 0.5f), std::floor(v + 0.5f));
    if (!in) return false;
    out = *in;
    return true;
}

namespace {

// How strongly a surface claims a point: in a body's water, then in the sea's, then over either.
enum class Claim { None, Over, InSea, InBody };

} // namespace

bool WaterData::overSea(const glm::vec3& point) const {
    const float reach = m_ocean->surface.farthestReach().x;
    for (const glm::vec4& span : m_seaSpans) {
        const bool alongX = point.x >= span.x - reach && point.x <= span.z + reach;
        const bool alongZ = point.z >= span.y - reach && point.z <= span.w + reach;
        if (alongX && alongZ) return true;
    }
    return false;
}

WaterSample WaterData::at(const glm::vec3& point) const {
    WaterSample best;
    Claim       held    = Claim::None;
    float       nearest = 0.0f;
    const auto  offer = [&](Claim claim, const WaterCell& cell, EntityId body) {
        const float apart = std::abs(cell.height - point.y);
        if (claim < held || (claim == held && apart >= nearest)) return;
        held         = claim;
        nearest      = apart;
        best.found   = true;
        best.wet     = point.y < cell.height;
        best.surface = cell.height;
        best.normal  = cell.normal;
        best.flow    = cell.flow;
        best.body    = body;
    };

    for (const WaterBody& body : m_bodies) {
        WaterCell cell;
        if (!body.sample(point.x, point.z, cell)) continue;
        // Deeper than it reaches, the point is under the body - in the ground, below a deck - not in it.
        if (point.y >= cell.height)                  offer(Claim::Over, cell, body.entity);
        else if (point.y > cell.height - body.depth) offer(Claim::InBody, cell, body.entity);
    }
    // A body's water claimed the point: the sea, which can only lose to it, is not asked.
    if (held != Claim::InBody && m_ocean && m_ocean->present && overSea(point)) {
        const OceanSample sea = m_ocean->surface.sampleAt({point.x, point.z});
        WaterCell surface;
        surface.height = m_ocean->level + sea.height;
        surface.normal = sea.normal;
        offer(point.y < surface.height ? Claim::InSea : Claim::Over, surface, EntityId{});
    }
    return best;
}

} // namespace Vkm::Engine

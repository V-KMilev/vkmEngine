#include "ecs/water/water_data.h"

#include <cmath>


namespace Vkm::Engine {

bool WaterBody::sample(float x, float z, WaterCell& out) const {
    if (columns == 0 || rows == 0) return false;
    // Cell (i, j)'s middle is at origin + corner + (i + 0.5) cells.
    const float u  = (x - origin.x - corner.x) / cell - 0.5f;
    const float v  = (z - origin.z - corner.y) / cell - 0.5f;
    const float fu = std::floor(u);
    const float fv = std::floor(v);
    const auto at = [&](float i, float j) -> const WaterCell* {
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
        const float tu  = u - fu;
        const float tv  = v - fv;
        const float w00 = (1.0f - tu) * (1.0f - tv);
        const float w10 = tu * (1.0f - tv);
        const float w01 = (1.0f - tu) * tv;
        const float w11 = tu * tv;
        out.wet    = true;
        out.height = origin.y + c00->height * w00 + c10->height * w10 + c01->height * w01 + c11->height * w11;
        const auto blend = [&](glm::vec3 WaterCell::* field) {
            return c00->*field * w00 + c10->*field * w10 + c01->*field * w01 + c11->*field * w11;
        };
        out.along  = blend(&WaterCell::along);
        out.normal = glm::normalize(blend(&WaterCell::normal));
        return true;
    }
    const WaterCell* in = at(std::floor(u + 0.5f), std::floor(v + 0.5f));
    if (!in) return false;
    out = *in;
    out.height += origin.y;
    return true;
}

WaterSample WaterData::at(const glm::vec3& point) const {
    WaterSample best;
    bool bestIn = false;
    float bestBottom = 0.0f;
    float bestApart  = 0.0f;
    for (const WaterBody& body : m_bodies) {
        WaterCell cell;
        if (!body.sample(point.x, point.z, cell)) continue;
        float     surface = cell.height;
        glm::vec3 normal  = cell.normal;
        if (body.water.waves > 0.0f) {
            const WaveSample moved = m_waves.surface.sampleAt({point.x, point.z}, body.water.waves);
            surface += moved.height;
            normal = moved.normal;
        }
        const float bottom = body.water.depth > 0.0f ? cell.height - body.water.depth : -INFINITY;
        const bool in = point.y < surface && point.y > bottom;
        const float apart = std::abs(surface - point.y);
        if (in) {
            const bool higher = !bestIn || bottom > bestBottom;
            if (!higher && (bottom < bestBottom || apart >= bestApart)) continue;
        } else if (bestIn || point.y < surface || (best.found && apart >= bestApart)) {
            // Dry, it answers only over a surface below it, and only while the point is in none.
            continue;
        }
        bestIn       = in;
        bestBottom   = bottom;
        bestApart    = apart;
        best.found   = true;
        best.wet     = in;
        best.surface = surface;
        best.normal  = normal;
        best.flow    = cell.along * body.water.flow;
        best.body    = body.entity;
    }
    return best;
}

} // namespace Vkm::Engine

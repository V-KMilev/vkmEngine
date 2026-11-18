#pragma once

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Floats a dynamic Rigidbody on whatever water it is in: the sea, a lake, a river (WaterSystem).
 *
 * The body is taken as its Collider's box. Each tick it is lifted by how much of the box lies under
 * the water's surface, slowed toward the water's own speed - so a river carries it - and turned so
 * the face of its thinnest axis nearest up lies along the surface: a raft lies flat. Only where this
 * end simulates the body.
 */
struct Buoyancy {
    float buoyancy = 2.2f;  ///< Lift when wholly under, in multiples of gravity: above 1 it floats.
    float drag     = 1.6f;  ///< How fast its speed through the water decays, a second, when wholly under.
    float righting = 6.0f;  ///< How fast it turns to lie along the surface, a second.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Buoyancy)
    VKM_F(buoyancy, 0.0f, 20.0f, "Lift wholly under, in gravities: above 1 it floats")
    VKM_F(drag, 0.0f, 50.0f, "How fast its speed through the water decays, wholly under")
    VKM_F(righting, 0.0f, 100.0f, "How fast it turns to lie along the surface")
VKM_REFLECT_END()

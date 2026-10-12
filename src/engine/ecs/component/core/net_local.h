#pragma once

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Keeps an entity, and everything below it, off the wire: each end runs its own.
 *
 * For what every end works out for itself - scenery a behavior moves by the clock, the parts of
 * a table a screen animates - which a server would otherwise describe to every player who
 * joins, and a client then draw at the server's word. Nothing at or under it is sent, so
 * nothing there is drawn from a snapshot either - but for what NetSession::spawn built there,
 * whose NetSpawn still says what it is and where it was made. NetSession::simulates says yes for
 * it at every end, a client's outside a replay. A client's own entities need no such mark: the
 * server never names them.
 */
struct NetLocal {};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::NetLocal)
VKM_REFLECT_END()

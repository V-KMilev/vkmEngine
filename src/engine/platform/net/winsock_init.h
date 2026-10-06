#pragma once

namespace Vkm::Engine {

/**
 * @brief Make sure this process can call the Windows socket library.
 *
 * Before **any** entry point, resolving a host included, not just before a
 * socket exists. Started once and never stopped: an address may be parsed
 * after the last socket closes. A no-op returning true off Windows.
 *
 * @return False when the library would not start; nothing can reach a network.
 */
bool ensureWinsock();

} // namespace Vkm::Engine

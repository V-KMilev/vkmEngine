#pragma once

namespace Vkm::Engine {

/**
 * @brief Make sure this process can call the Windows socket library.
 *
 * Winsock has to be started before **any** of its entry points is called, not
 * just before a socket exists. Resolving a host is one of those entry points,
 * and an address is parsed off the command line long before anything is opened
 * - so an engine that started Winsock inside the socket constructor would find
 * every hostname unresolvable and every join silently declining to happen.
 *
 * Started once and never stopped. A process that closes its last socket may
 * still parse another address afterwards, so the library has to outlive any one
 * socket; and the operating system unloads it at exit, so there is nothing for
 * WSACleanup to buy.
 *
 * A no-op returning true everywhere else, so callers have no platform branch.
 *
 * A file that needs a winsock or windows header includes platform/windows_api.h
 * to get one; the defines and undefines that has to carry are its business.
 *
 * @return False when the library would not start, in which case nothing here
 *         can reach a network and the caller should say so rather than proceed.
 */
bool ensureWinsock();

} // namespace Vkm::Engine

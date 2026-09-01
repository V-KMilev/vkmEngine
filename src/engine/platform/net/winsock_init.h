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
 * Every file that includes a winsock header needs the same three defines before
 * it, and NOGDI is the one that is not optional: winsock2.h pulls in windows.h,
 * whose GDI half defines ERROR as 0, and vkmLog's LogLevel::ERROR then fails to
 * parse. WIN32_LEAN_AND_MEAN and NOMINMAX are guarded because the MinGW
 * libstdc++ headers already define the second. platform/library/dynamic_library.cpp
 * does the same dance for the same reason.
 *
 * @return False when the library would not start, in which case nothing here
 *         can reach a network and the caller should say so rather than proceed.
 */
bool ensureWinsock();

} // namespace Vkm::Engine

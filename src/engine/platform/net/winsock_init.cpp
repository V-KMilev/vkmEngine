#define VKM_LOG_CATEGORY "NET"

#include "platform/net/winsock_init.h"

#include "logger.h"

#include "platform/windows_api.h"

namespace Vkm::Engine {

bool ensureWinsock() {
#if defined(_WIN32)
    // A function-local static: started once, whichever thread arrives first.
    static const bool started = []() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) == 0) return true;
        LOG_ERROR("Winsock would not start, so nothing here can reach a network");
        return false;
    }();
    return started;
#else
    return true;
#endif
}

} // namespace Vkm::Engine

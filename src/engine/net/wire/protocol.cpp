#include "net/wire/protocol.h"

namespace Vkm::Engine {

const char* toString(NetRefusal reason) {
    switch (reason) {
        case NetRefusal::Full:     return "the server is full";
        case NetRefusal::Mismatch: return "a different build or world";
        case NetRefusal::Declined: return "the game declined the connection";
        default:                   return "no reason given";
    }
}

} // namespace Vkm::Engine

#pragma once

#include "core/system.h"

namespace Engine {

class VisibilitySystem : public System {
    public:
        void update(FrameContext& ctx) override;
};

} // namespace Engine

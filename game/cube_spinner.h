#pragma once

#include "system/script/reflected_behavior.h"

namespace Engine {

/**
 * @brief Example behavior: spins its entity about the world up axis.
 *
 * typeName / visitFields / clone come from ReflectedBehavior via the
 * VKM_REFLECT markup below - the only authored state is degreesPerSecond.
 */
class CubeSpinner : public ReflectedBehavior<CubeSpinner> {
    public:
        static constexpr const char* TYPE_NAME = "CubeSpinner";

        void onUpdate(float dt) override;

        /**
         * @brief Authored, reflected spin rate about world up, in degrees per second.
         */
        float degreesPerSecond = 90.0f;
};

VKM_REFLECT_BEGIN(CubeSpinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()

} // namespace Engine

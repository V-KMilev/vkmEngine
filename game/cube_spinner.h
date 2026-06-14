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

        /// Authored, reflected: spin rate about world up, in degrees/second.
        float degreesPerSecond = 90.0f;
};

} // namespace Engine

// Leading :: is required: this header is compiled alongside core/engine.h, so
// inside the macro's `namespace Engine::Reflect` the bare name `Engine` would
// bind to the class Engine::Engine, not the namespace. Fully-qualify it.
VKM_REFLECT_BEGIN(::Engine::CubeSpinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()

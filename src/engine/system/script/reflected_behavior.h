#pragma once

#include <memory>
#include <string_view>
#include <tuple>

#include "core/reflect.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"

namespace Engine {

/**
 * @brief CRTP base that derives a behavior's boilerplate from its reflected
 *        fields - the UPROPERTY-equivalent reuse.
 *
 * Declare the tunable fields once with VKM_REFLECT_BEGIN(Derived) / VKM_F /
 * VKM_REFLECT_END and `typeName()`, `visitFields()` (editor + serialization),
 * and `clone()` are all generated. Override the lifecycle hooks
 * (onStart/onUpdate/onDestroy) on the subclass as usual.
 *
 * Requirements on Derived:
 *   - `static constexpr const char* TYPE_NAME` (its BehaviorRegistry key), and
 *   - a `Reflect::Traits<Derived>` specialisation (the VKM_REFLECT markup).
 */
template<typename Derived>
class ReflectedBehavior : public Behavior {
    public:
        const char* typeName() const override { return Derived::TYPE_NAME; }

        void visitFields(BehaviorFieldVisitor& visitor) override {
            Reflect::forEachField(static_cast<Derived&>(*this),
                [&](std::string_view name, auto& value) {
                    // Field names come from string literals (VKM_F's #name), so
                    // data() is null-terminated for the const char* overloads.
                    visitor.field(name.data(), value);
                });
        }

        std::unique_ptr<Behavior> clone() const override {
            auto copy = std::make_unique<Derived>();
            const Derived& self = static_cast<const Derived&>(*this);
            // Copy only the reflected (authored) fields; the engine context and
            // started flag are rebound on the new instance by BehaviorSystem.
            std::apply([&](auto&&... f) {
                (((*copy).*(f.ptr) = self.*(f.ptr)), ...);
            }, Reflect::Traits<Derived>::fields());
            return copy;
        }
};

} // namespace Engine

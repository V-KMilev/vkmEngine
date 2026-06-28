#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Type-erased visitor over a behavior's reflected authoring fields.
 *
 * The bridge that lets code holding only a `Behavior*` (the editor inspector,
 * the serializer) read or write a concrete behavior's fields without knowing
 * its type: `ReflectedBehavior::visitFields` walks the reflected fields and
 * calls the matching `field()` overload for each. Implement one of these to
 * serialize, edit, or otherwise process every field uniformly.
 *
 * Add an overload here to support a new field type; a behavior that reflects an
 * unsupported type then fails to compile, which is the right nudge.
 */
class BehaviorFieldVisitor {
    public:
        virtual ~BehaviorFieldVisitor() = default;

        virtual void field(const char* name, float& value)     = 0;
        virtual void field(const char* name, int& value)       = 0;
        virtual void field(const char* name, bool& value)      = 0;
        virtual void field(const char* name, glm::vec3& value) = 0;
};

} // namespace Engine

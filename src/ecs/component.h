#pragma once

#include <cstdint>

/**
 * @brief Enumeration representing possible types of component.
 */
enum class ComponentType {
    NONE = 0,
    Mesh = 1,
    Material = 2,
};

/**
 * @brief Convert a ComponentType enum value to its string representation.
 *
 * @param type The ComponentType value to convert.
 * @return const char* String representation of the ComponentType.
 */
constexpr const char* toString(ComponentType type) {
    switch (type) {
        case ComponentType::NONE: return "NONE";
        case ComponentType::Mesh: return "Mesh";
        case ComponentType::Material: return "Material";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Abstract base class representing a generic Component.
 *
 * A Component has a unique ID and a type. Derived classes can represent more specific component data.
 */
class Component {
    public:
        Component() = delete;
        virtual ~Component();

        Component(const Component& other) = delete;
        Component& operator = (const Component& other) = delete;

        Component(Component && other) noexcept = default;
        Component& operator = (Component && other) noexcept = default;

        /**
         * @brief Construct a Component with the given ID and type.
         * @param id The unique identifier for the component.
         * @param type The ComponentType. Defaults to ComponentType::NONE.
         */
        Component(
            uint32_t id,
            ComponentType type = ComponentType::NONE
        );

    public:
        /**
         * @brief Get the unique ID of the component.
         * @return uint32_t The component's ID.
         */
        uint32_t getID() const;

        /**
         * @brief Get the type of the component.
         * @return ComponentType The type of the component.
         */
        ComponentType getType() const;

    protected:
        uint32_t m_id;
        ComponentType m_type;
};

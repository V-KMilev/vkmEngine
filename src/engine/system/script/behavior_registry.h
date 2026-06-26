#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "system/script/behavior.h"

namespace Engine {

/**
 * @brief Name -> factory registry for Behavior subclasses.
 *
 * The C++ analogue of Unreal's class registration. Game code registers each
 * behavior type at startup (registerBehavior<T>()); serialization recreates
 * instances by name (create("CubeSpinner")). Mirrors AssetFactories: a single
 * process-wide registry reached through get().
 */
class BehaviorRegistry {
    public:
        using Factory = std::function<std::unique_ptr<Behavior>()>;

        static BehaviorRegistry& get();

        /**
         * @brief Register @p name -> @p factory.
         *
         * A duplicate name logs a warning and overwrites the existing factory.
         *
         * @param name    Key the behavior type is registered and created under.
         * @param factory Callable that constructs a fresh instance of the type.
         */
        void registerBehavior(std::string name, Factory factory);

        /**
         * @brief Register T under its T::TYPE_NAME, the single source of truth it also
         * returns from Behavior::typeName().
         */
        template<typename T>
        void registerBehavior() {
            registerBehavior(T::TYPE_NAME, [] {
                return std::make_unique<T>();
            });
        }

        /**
         * @brief Create a fresh instance by name, or nullptr (and a logged error) if
         * the name is unknown.
         */
        std::unique_ptr<Behavior> create(const std::string& name) const;

        /**
         * @brief Report whether @p name has a registered factory.
         *
         * @param name Behavior type name to look up.
         * @return True if a factory is registered under @p name.
         */
        bool contains(const std::string& name) const;

        /**
         * @brief List every registered behavior name, sorted.
         *
         * Used to populate the editor's add-behavior menu.
         *
         * @return Alphabetically sorted copy of all registered type names.
         */
        std::vector<std::string> names() const;

        /**
         * @brief Drop every registered factory. Used before unloading the game module
         * on hot-reload, since the factories close over module code.
         */
        void clear();

    private:
        BehaviorRegistry() = default;

        std::unordered_map<std::string, Factory> m_factories;
};

} // namespace Engine

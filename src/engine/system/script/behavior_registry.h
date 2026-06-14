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

        /// Register @p name -> @p factory. A duplicate name logs and overwrites.
        void registerBehavior(std::string name, Factory factory);

        /// Register T under its T::TYPE_NAME, the single source of truth it also
        /// returns from Behavior::typeName().
        template<typename T>
        void registerBehavior() {
            registerBehavior(T::TYPE_NAME, [] {
                return std::unique_ptr<Behavior>(std::make_unique<T>());
            });
        }

        /// Create a fresh instance by name, or nullptr (and a logged error) if
        /// the name is unknown.
        std::unique_ptr<Behavior> create(const std::string& name) const;

        /// Whether @p name has a registered factory.
        bool contains(const std::string& name) const;

        /// All registered names, sorted - for the editor's add-behavior menu.
        std::vector<std::string> names() const;

        /// Drop every registered factory. Used before unloading the game module
        /// on hot-reload, since the factories close over module code.
        void clear();

    private:
        BehaviorRegistry() = default;

        std::unordered_map<std::string, Factory> m_factories;
};

} // namespace Engine

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/reflect.h"
#include "system/script/behavior.h"

namespace Vkm::Engine {

/**
 * @brief Name -> factory registry for Behavior subclasses.
 *
 * A gameplay module registers its types from vkmRegisterBehaviors. One per process,
 * since game-DLL code must reach it.
 */
class BehaviorRegistry {
    public:
        using Factory = std::function<std::unique_ptr<Behavior>()>;

    public:
        ~BehaviorRegistry() = default;

        BehaviorRegistry(const BehaviorRegistry& other) = delete;
        BehaviorRegistry& operator=(const BehaviorRegistry& other) = delete;

        BehaviorRegistry(BehaviorRegistry && other) = delete;
        BehaviorRegistry& operator=(BehaviorRegistry && other) = delete;

    public:
        static BehaviorRegistry& get();

        /**
         * @brief Register @p name -> @p factory.
         *
         * A duplicate name logs a warning and overwrites the existing factory.
         *
         * @param name    Key the type is registered and created under.
         * @param factory Constructs a fresh instance.
         */
        void registerBehavior(std::string name, Factory factory);

        /**
         * @brief Register T under the name its reflect block records.
         *
         * The same string typeName() returns, so the save and load keys cannot differ.
         *
         * @tparam T Behavior subclass with a VKM_REFLECT block, default-constructible.
         */
        template<typename T>
        void registerBehavior() {
            registerBehavior(Reflect::Traits<T>::NAME, [] { return std::make_unique<T>(); });
        }

        /**
         * @brief Register every type in @p Ts, in order, as registerBehavior<T>() does.
         *
         * @code
         * BehaviorRegistry::get().registerBehaviors<Spinner, Health, Door>();
         * @endcode
         *
         * @tparam Ts Behavior subclasses with VKM_REFLECT blocks.
         */
        template<typename... Ts>
        void registerBehaviors() {
            (registerBehavior<Ts>(), ...);
        }

        /**
         * @brief Create a fresh instance by name.
         *
         * @param name Registered type name.
         * @return The new instance, or nullptr, logged, when the name is unknown.
         */
        std::unique_ptr<Behavior> create(const std::string& name) const;

        /**
         * @brief Report whether @p name has a registered factory.
         *
         * @param name Behavior type name.
         * @return True if a factory is registered under @p name.
         */
        bool contains(const std::string& name) const;

        /**
         * @brief List every registered behavior name, sorted.
         *
         * @return Sorted copy of the names.
         */
        std::vector<std::string> names() const;

        /**
         * @brief Drop every registered factory; call before unloading the module whose code they hold.
         */
        void clear();

    private:
        BehaviorRegistry() = default;

    private:
        std::unordered_map<std::string, Factory> m_factories;
};

} // namespace Vkm::Engine

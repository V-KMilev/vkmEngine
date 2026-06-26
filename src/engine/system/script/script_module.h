#pragma once

#include <string>

#include "platform/library/dynamic_library.h"

namespace Engine {

class Scene;

/**
 * @brief Loads the hot-reloadable gameplay module and swaps it at runtime.
 *
 * The editor owns one of these. On load() it copies the built module (so the
 * original stays writable for rebuilds) and calls its vkmRegisterBehaviors entry
 * to populate the BehaviorRegistry. reload() swaps in a freshly built module
 * without restarting: it serializes each entity's behaviors, destroys them,
 * unloads the old module, loads the new one, and recreates the behaviors from
 * the serialized type + reflected fields. Entities and all other components are
 * untouched - only the behavior C++ objects are rebuilt.
 *
 * Reloaded behaviors start fresh (onStart runs again next tick); a behavior with
 * heavy onStart side effects should be written to tolerate that.
 */
class ScriptModule {
    public:
        ScriptModule() = default;
        ~ScriptModule();

        ScriptModule(const ScriptModule& other) = delete;
        ScriptModule& operator=(const ScriptModule& other) = delete;

        ScriptModule(ScriptModule && other) = delete;
        ScriptModule& operator=(ScriptModule && other) = delete;

    public:
        /**
         * @brief Load the gameplay module and register its behaviors.
         *
         * Copies the built module aside (keeping the original writable for
         * rebuilds), loads the copy, and calls its register entry to populate
         * the BehaviorRegistry.
         *
         * @param modulePath Path to the built module (.dll/.so) to load.
         * @return True if the module loaded and registered successfully.
         */
        bool load(const std::string& modulePath);

        /**
         * @brief Hot-reload from the same path: serialize -> swap module -> recreate.
         * Returns true on success. On failure the module is left unloaded and a
         * subsequent reload() retries the load (recovery after a fixed build).
         */
        bool reload(Scene& scene);

        bool isLoaded() const { return m_lib.isLoaded(); }

    private:
        /**
         * @brief Copy the built module to a fresh name, load it, and call its register
         * entry. The copy keeps the build free to overwrite the original.
         */
        bool loadCopyAndRegister();

        DynamicLibrary m_lib;
        std::string    m_modulePath;      ///< The built module (overwritten by rebuilds).
        std::string    m_loadedCopyPath;  ///< The copy actually loaded.
        int            m_reloadCounter = 0;
};

} // namespace Engine

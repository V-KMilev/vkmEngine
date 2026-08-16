#pragma once

#include <string>

#include "platform/library/dynamic_library.h"

namespace Engine {

class Scene;

/**
 * @brief Loads the hot-reloadable gameplay module and swaps it at runtime.
 *
 * Each host owns one of these: the editor to edit a project, the runtime to
 * play it. On load() it copies the built module (so the original stays writable
 * for rebuilds) and calls its vkmRegisterBehaviors entry to populate the
 * BehaviorRegistry. reload() swaps in a freshly built module without
 * restarting; entities and all other components are untouched - only the
 * behavior C++ objects are rebuilt.
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

        /**
         * @brief Let the module seed @p scene, if it wants to.
         *
         * Optional second entry, `vkmBuildScene`. A project whose world is
         * generated rather than authored - a procedural level, a profiling load -
         * has no scene file for project.json to point at, and the host has no
         * business carrying one game's content. This lets such a project say in
         * its own code what it starts as.
         *
         * A module without the entry is normal and silent: most projects author
         * a scene and name it in project.json instead.
         *
         * @param scene Scene to seed.
         * @return True if the module had the entry and it ran.
         */
        bool buildScene(Scene& scene);

        /**
         * @brief Drop the loaded module and the behavior types it registered.
         *
         * A host that moves to a project bringing no code of its own has to
         * unload rather than keep what it had: the old module would still answer
         * buildScene and its behavior types would stay in the registry, so the
         * previous project's world could be generated inside the new one.
         *
         * Safe to call when nothing is loaded. Entities are untouched - the
         * caller is expected to have cleared the scene first, since behaviors
         * outlive this call only as dangling objects.
         */
        void unload();

        bool isLoaded() const { return m_lib.isLoaded(); }

    private:
        /**
         * @brief Copy the built module to a fresh name, load it, and call its register
         * entry. The copy keeps the build free to overwrite the original.
         */
        bool loadCopyAndRegister();

    private:
        DynamicLibrary m_lib;
        std::string    m_modulePath;
        std::string    m_loadedCopyPath;
        int            m_reloadCounter = 0;
};

} // namespace Engine

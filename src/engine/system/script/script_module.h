#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "platform/library/dynamic_library.h"

namespace Vkm::Engine {

class BehaviorSystem;
class EventBus;
class NetSession;
class ResourceManager;
class Scene;

/**
 * @brief Loads the hot-reloadable gameplay module and swaps it at runtime.
 *
 * reload() rebuilds only the behavior objects; entities and other components are
 * untouched. Reloaded behaviors run onStart again next tick.
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
         * Loads a copy, keeping the original writable for rebuilds. A read-only directory
         * (an installed game) loads in place; any other copy failure refuses the load.
         *
         * @param modulePath Path to the built module (.dll/.so) to load.
         * @param events The host's bus, so a replaced module is released fully; null
         *        for a host with no Engine yet, which has nothing to replace.
         * @return True if the module loaded and registered successfully.
         */
        bool load(const std::filesystem::path& modulePath, EventBus* events);

        /**
         * @brief Hot-reload from the same path: serialize, swap module, recreate.
         *
         * The new build is checked before anything is torn down; a refused one leaves the
         * running module untouched. With none loaded, this is a plain load. @p scene must
         * hold no component set the outgoing module created (its vtable is module code);
         * see docs/reference/scripting.md, "Nothing may hold module code across the swap".
         *
         * @param scene     Scene whose behaviors are saved across the swap.
         * @param behaviors The system running them, whose session the swap ends.
         * @param events    Bus to clear the outgoing module's event types from.
         * @return True if the module reloaded and its behaviors were restored.
         */
        bool reload(Scene& scene, BehaviorSystem& behaviors, EventBus& events);

        /**
         * @brief Let the module seed @p scene through the optional `vkmBuildScene`, if it has one.
         *
         * @param scene Scene to seed.
         * @param resources Asset graph the built world's assets go into.
         * @return True if the module had the entry and it ran.
         */
        bool buildScene(Scene& scene, ResourceManager& resources);

        /**
         * @brief Let the module say what a player is, through the optional `vkmSetupNetwork`.
         *
         * Sets spawn callbacks and registers replicated components. Harmless offline:
         * an empty session behaves as no session.
         *
         * @param session Session the host will host or join with.
         * @return True if the module had the entry and it ran.
         */
        bool setupNetwork(NetSession& session);

        /**
         * @brief Drop the loaded module and the behavior types it registered.
         *
         * Safe when nothing is loaded. Clear the scene first: behaviors outlive this only
         * as dangling objects.
         *
         * @param events Bus to clear the outgoing module's event types from.
         */
        void unload(EventBus& events);

        /**
         * @brief Unload whatever is loaded and remember @p modulePath for a project not yet built.
         *
         * @param modulePath Where the project's build writes its module.
         * @param events Bus to clear the outgoing module's event types from.
         */
        void expect(const std::filesystem::path& modulePath, EventBus& events);

        bool isLoaded() const { return m_lib->isLoaded(); }

        /**
         * @brief The built module this loaded from, not the mapped copy: the file to watch for a build.
         *
         * @return The path given to load() or expect(), or empty when none was
         *         given or unload() has since dropped a loaded module.
         */
        const std::filesystem::path& modulePath() const { return m_modulePath; }

    private:
        /**
         * @brief A built module opened and checked, not yet registered.
         */
        struct OpenedModule {
            std::unique_ptr<DynamicLibrary> lib;   ///< Null when it did not open or was refused.
            std::filesystem::path           copy;  ///< Empty when loaded in place.
        };

    private:
        /**
         * @brief Copy the built module to a fresh name, open it, and check it.
         *
         * Touches nothing the running module owns. Refuses a library that will not open,
         * reports another or no engine version, or lacks vkmRegisterBehaviors.
         *
         * @return The opened module, or one holding no library.
         */
        OpenedModule openBuilt();

        /**
         * @brief Make @p opened the loaded module and call its register entry.
         *
         * The previous library must already be unloaded: its copy is deleted here.
         *
         * @param opened A module openBuilt() returned with a library.
         */
        void adopt(OpenedModule opened);

        /**
         * @brief Drop everything the loaded module registered, before it is unmapped.
         *
         * Factories, wire thunks, spawn callbacks and the module's event buses are all its
         * code, and must not outlive the dlclose.
         *
         * @param events The host's bus, or null when it has none yet.
         */
        void releaseRegistrations(EventBus* events);

    private:
        std::unique_ptr<DynamicLibrary> m_lib = std::make_unique<DynamicLibrary>();
        NetSession*           m_net = nullptr;  ///< Session this module's entry wrote into.
        std::filesystem::path m_modulePath;
        std::filesystem::path m_loadedCopyPath;
        int                   m_reloadCounter = 0;
};

} // namespace Vkm::Engine

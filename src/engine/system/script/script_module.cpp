#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/script_module.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/event/event_bus.h"
#include "debug/engine_error_log.h"
#include "ecs/scene.h"
#include "io/scene/component_serializer.h"
#include "net/wire/codecs.h"
#include "net/wire/schema.h"
#include "net/net_session.h"
#include "system/script/behavior_registry.h"
#include "system/script/behavior_system.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {

using RegisterFn     = void (*)();
using VersionFn      = const char* (*)();
using BuildSceneFn   = void (*)(Scene&, ResourceManager&);
using SetupNetworkFn = void (*)(NetSession&);

// Best-effort sweep of stale "<stem>.loaded.*.<ext>" copies a crash left; one still
// locked by a concurrent editor is skipped.
void removeStaleCopies(const std::filesystem::path& src) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string prefix = src.stem().string() + ".loaded.";
    const std::string ext = src.extension().string();
    for (fs::directory_iterator it(src.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(prefix, 0) == 0 && it->path().extension() == ext) {
            fs::remove(it->path(), ec);
        }
    }
}

} // namespace

void ScriptModule::releaseRegistrations(EventBus* events) {
    BehaviorRegistry::get().clear();
    NetSchema::get().clear();

    // endSession dropped the module's listeners, so its buses are idle; see EventBus::dropIdleBuses.
    if (events) events->dropIdleBuses();

    // Assigning over a std::function runs the old target's manager, which is module
    // code, so the session's lambdas go before the dlclose.
    if (m_net) m_net->onSpawn(nullptr, nullptr);
    m_net = nullptr;
}

ScriptModule::~ScriptModule() {
    // Registries only: the session and bus, declared after the module, are already gone.
    BehaviorRegistry::get().clear();
    NetSchema::get().clear();

    // Unloaded before the delete so the copy is no longer locked.
    m_lib->unload();
    if (!m_loadedCopyPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_loadedCopyPath, ec);
    }
}

bool ScriptModule::load(const std::filesystem::path& modulePath, EventBus* events) {
    // Released and unmapped first, so a failed load cannot leave registrations pointing
    // into an unmapped library.
    releaseRegistrations(events);
    m_lib->unload();

    m_modulePath = modulePath;
    removeStaleCopies(modulePath);
    if (OpenedModule opened = openBuilt(); opened.lib) {
        adopt(std::move(opened));
        return true;
    }

    // openBuilt logged the reason; this is the line for the editor's Errors tab.
    reportError(
        "Script",
        modulePath.string(),
        "Gameplay module failed to load; no behavior type is registered, so "
        "behaviors in the scene are held as text and do not run."
    );
    return false;
}

ScriptModule::OpenedModule ScriptModule::openBuilt() {
    namespace fs = std::filesystem;

    const fs::path& src = m_modulePath;
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        LOG_ERROR("Game module not found at '%s'", src.string().c_str());
        return {};
    }

    // A copy, since Windows locks a loaded DLL; step past names a second editor holds
    // open. Sixteen is a ceiling, not a dependency.
    fs::path copy;
    bool copied = false;
    for (int attempt = 0; attempt < 16 && !copied; ++attempt) {
        const std::string suffix = ".loaded." + std::to_string(m_reloadCounter++) + src.extension().string();
        copy = src.parent_path() / (src.stem().string() + suffix);
        fs::remove(copy, ec);
        ec.clear();
        fs::copy_file(src, copy, fs::copy_options::overwrite_existing, ec);
        copied = !ec;
    }
    if (!copied) {
        // In place only where the directory refuses writes (an installed game's bin/);
        // otherwise the build would be locked by the module it is meant to replace.
        const bool readOnly = ec == std::errc::permission_denied
            || ec == std::errc::read_only_file_system
            || ec == std::errc::operation_not_permitted;
        if (!readOnly) {
            LOG_ERROR("Cannot copy the game module aside (%s); not loading it", ec.message().c_str());
            return {};
        }
        LOG_INFO(
            "The game module's directory takes no copy (%s); loading '%s' in place",
            ec.message().c_str(),
            src.string().c_str()
        );
    }

    auto lib = std::make_unique<DynamicLibrary>();
    const fs::path& target = copied ? copy : src;
    const auto refuse = [&]() -> OpenedModule {
        lib->unload();
        if (copied) fs::remove(copy, ec);
        return {};
    };
    if (!lib->load(target)) return refuse();

    // A version mismatch is an ABI mismatch, so refuse it, and a missing version, with a
    // sentence rather than a later crash. See docs/reference/scripting.md.
    auto versionFn = reinterpret_cast<VersionFn>(lib->symbol("vkmModuleEngineVersion"));
    if (!versionFn) {
        LOG_ERROR(
            "Game module '%s' declares no engine version. Rebuild it against vkmEngine %s.",
            src.string().c_str(),
            VKM_ENGINE_VERSION
        );
        return refuse();
    }
    if (const char* built = versionFn(); std::strcmp(built, VKM_ENGINE_VERSION) != 0) {
        LOG_ERROR(
            "Game module '%s' was built against vkmEngine %s but this is %s. Rebuild the module (vkm build).",
            src.string().c_str(),
            built,
            VKM_ENGINE_VERSION
        );
        return refuse();
    }
    if (!lib->symbol("vkmRegisterBehaviors")) {
        LOG_ERROR("Game module '%s' has no vkmRegisterBehaviors entry", src.string().c_str());
        return refuse();
    }

    return {std::move(lib), copied ? copy : fs::path{}};
}

void ScriptModule::adopt(OpenedModule opened) {
    // The outgoing library is already unmapped, so its copy is no longer locked.
    std::error_code ec;
    if (!m_loadedCopyPath.empty()) std::filesystem::remove(m_loadedCopyPath, ec);
    m_loadedCopyPath = std::move(opened.copy);
    m_lib = std::move(opened.lib);

    auto registerFn = reinterpret_cast<RegisterFn>(m_lib->symbol("vkmRegisterBehaviors"));
    const size_t before = BehaviorRegistry::get().names().size();
    registerFn();
    const size_t registered = BehaviorRegistry::get().names().size() - before;
    LOG_INFO(
        "Loaded game module '%s' (%zu behavior type(s) registered)",
        m_modulePath.string().c_str(),
        registered
    );

    // Usually a behavior missing from vkmRegisterBehaviors, whose instances stay text.
    if (registered == 0) {
        LOG_WARNING(
            "Game module '%s' registered no behavior types. A behavior "
            "the scene names but vkmRegisterBehaviors does not is held "
            "as text and never runs.",
            m_modulePath.string().c_str()
        );
    }
}

bool ScriptModule::reload(Scene& scene, BehaviorSystem& behaviors, EventBus& events) {
    if (!m_lib->isLoaded()) {
        if (m_modulePath.empty()) {
            LOG_WARNING("ScriptModule::reload called but no module was ever configured");
            return false;
        }
        LOG_INFO(
            "ScriptModule::reload: no module loaded, retrying load of '%s'",
            m_modulePath.string().c_str()
        );
        OpenedModule opened = openBuilt();
        if (!opened.lib) return false;
        adopt(std::move(opened));

        // Behaviors kept as text by a load with no registry are re-read now their types exist.
        if (auto* storage = scene.storage<ScriptComponent>()) {
            storage->forEach([&](uint32_t, ScriptComponent& sc) {
                ComponentSerializer::load(ComponentSerializer::save(sc), sc);
            });
        }
        return true;
    }

    OpenedModule next = openBuilt();
    if (!next.lib) {
        reportError(
            "Script",
            m_modulePath.string(),
            "Reload refused: the new build did not load (the log says why). The "
            "module already running stays, with its behaviors."
        );
        return false;
    }

    // Saved while the module (visitFields, typeName) is loaded, and before onDestroy, so
    // the authored state is restored rather than a teardown's leftovers.
    std::vector<std::pair<EntityId, nlohmann::json>> saved;
    if (auto* storage = scene.storage<ScriptComponent>()) {
        storage->forEach([&](uint32_t entityIdx, ScriptComponent& sc) {
            saved.emplace_back(scene.entityAt(entityIdx), ComponentSerializer::save(sc));
        });
    }

    // onDestroy while the module is still mapped, so no listener or queued request
    // outlives its behavior.
    behaviors.endSession(scene);

    if (auto* storage = scene.storage<ScriptComponent>()) {
        storage->forEach([](uint32_t, ScriptComponent& sc) { sc.behaviors.clear(); });
    }

    releaseRegistrations(&events);
    m_lib->unload();
    adopt(std::move(next));

    for (auto& [id, data] : saved) {
        if (ScriptComponent* script = scene.tryGet<ScriptComponent>(id)) {
            ComponentSerializer::load(data, *script);
        }
    }

    LOG_INFO("Script reload complete (%zu entit(y/ies) restored)", saved.size());
    return true;
}

void ScriptModule::unload(EventBus& events) {
    if (!m_lib->isLoaded()) return;

    releaseRegistrations(&events);
    m_lib->unload();
    m_modulePath.clear();

    std::error_code ec;
    if (!m_loadedCopyPath.empty()) {
        std::filesystem::remove(m_loadedCopyPath, ec);
        m_loadedCopyPath.clear();
    }
    LOG_INFO("Game module unloaded");
}

void ScriptModule::expect(const std::filesystem::path& modulePath, EventBus& events) {
    unload(events);
    m_modulePath = modulePath;
}

bool ScriptModule::buildScene(Scene& scene, ResourceManager& resources) {
    if (!m_lib->isLoaded()) return false;

    auto buildFn = reinterpret_cast<BuildSceneFn>(m_lib->symbol("vkmBuildScene"));
    if (!buildFn) return false;  // optional: most projects author a scene instead

    buildFn(scene, resources);
    return true;
}

bool ScriptModule::setupNetwork(NetSession& session) {
    if (!m_lib->isLoaded()) return false;

    // Rebuilt from nothing, so no previous module's registrations survive.
    NetSchema::get().clear();
    m_net = &session;

    auto setupFn = reinterpret_cast<SetupNetworkFn>(m_lib->symbol("vkmSetupNetwork"));
    if (!setupFn) return false;  // optional: a single-player project needs none

    // Engine types first, from one place: the wire index is the identity and the
    // fingerprint is order-sensitive (NetSchema::fingerprint).
    registerEngineNetTypes();
    setupFn(session);
    return true;
}

} // namespace Vkm::Engine

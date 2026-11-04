#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/module/script_module.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "base/event/event_bus.h"
#include "debug/error/engine_error_log.h"
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
using SetupNetworkFn = void (*)(NetSession&);

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

    m_lib->unload();
}

bool ScriptModule::load(const std::filesystem::path& modulePath, EventBus* events) {
    // Released and unmapped first, so a failed load cannot leave registrations pointing
    // into an unmapped library.
    releaseRegistrations(events);
    m_lib->unload();

    m_modulePath = modulePath;
    Platform::DynamicLibrary::sweepCopies(modulePath);
    if (std::unique_ptr<Platform::DynamicLibrary> opened = openBuilt()) {
        adopt(std::move(opened));
        return true;
    }

    // openBuilt logged the reason; this is the line for the editor Console's Errors.
    reportError(
        "Script",
        modulePath.string(),
        "Gameplay module failed to load; no behavior type is registered, so "
        "behaviors in the scene are held as text and do not run."
    );
    return false;
}

std::unique_ptr<Platform::DynamicLibrary> ScriptModule::openBuilt() {
    const std::filesystem::path& src = m_modulePath;
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        LOG_ERROR("Game module not found at '%s'", src.string().c_str());
        return nullptr;
    }

    // A copy, so a rebuild can land while this one runs; in place only from a folder that takes none.
    auto lib = std::make_unique<Platform::DynamicLibrary>();
    const Platform::DynamicLibrary::Outcome outcome = lib->loadCopy(src);
    if (outcome.how == Platform::DynamicLibrary::Loaded::Refused) {
        LOG_ERROR("Cannot copy the game module aside (%s); not loading it", outcome.error.message().c_str());
        return nullptr;
    }
    if (outcome.how == Platform::DynamicLibrary::Loaded::InPlace) {
        LOG_INFO(
            "The game module's directory takes no copy (%s); loading '%s' in place",
            outcome.error.message().c_str(),
            src.string().c_str()
        );
    }
    if (!lib->isLoaded()) return nullptr;

    // A version mismatch is an ABI mismatch, so refuse it, and a missing version, with a
    // sentence rather than a later crash. See docs/reference/scripting.md.
    auto versionFn = reinterpret_cast<VersionFn>(lib->symbol("vkmModuleEngineVersion"));
    if (!versionFn) {
        LOG_ERROR(
            "Game module '%s' declares no engine version. Rebuild it against vkmEngine %s.",
            src.string().c_str(),
            VKM_ENGINE_VERSION
        );
        return nullptr;
    }
    if (const char* built = versionFn(); std::strcmp(built, VKM_ENGINE_VERSION) != 0) {
        LOG_ERROR(
            "Game module '%s' was built against vkmEngine %s but this is %s. Rebuild the module (vkm build).",
            src.string().c_str(),
            built,
            VKM_ENGINE_VERSION
        );
        return nullptr;
    }
    if (!lib->symbol("vkmRegisterBehaviors")) {
        LOG_ERROR("Game module '%s' has no vkmRegisterBehaviors entry", src.string().c_str());
        return nullptr;
    }

    return lib;
}

void ScriptModule::adopt(std::unique_ptr<Platform::DynamicLibrary> opened) {
    m_lib = std::move(opened);

    auto registerFn     = reinterpret_cast<RegisterFn>(m_lib->symbol("vkmRegisterBehaviors"));
    const size_t before = BehaviorRegistry::get().names().size();
    registerFn();
    const size_t registered = BehaviorRegistry::get().names().size() - before;
    LOG_INFO(
        "Loaded game module '%s' (%zu behavior type(s) registered)",
        m_modulePath.string().c_str(),
        registered
    );

    // Instances of a type no module registers stay text.
    if (registered == 0) {
        LOG_WARNING(
            "Game module '%s' registered no behavior types: a behavior registers through its "
            "VKM_BEHAVIOR block and the module's VKM_GAME_MODULE(). One the scene names that no "
            "module registers is held as text and never runs.",
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
        std::unique_ptr<Platform::DynamicLibrary> opened = openBuilt();
        if (!opened) {
            reportError("Script", m_modulePath.string(), "Reload failed: the module still does not load.");
            return false;
        }
        adopt(std::move(opened));

        // Behaviors kept as text by a load with no registry are re-read now their types exist.
        if (auto* storage = scene.storage<ScriptComponent>()) {
            storage->forEach([&](uint32_t, ScriptComponent& sc) {
                ComponentSerializer::load(ComponentSerializer::save(sc), sc);
            });
        }
        return true;
    }

    std::unique_ptr<Platform::DynamicLibrary> next = openBuilt();
    if (!next) {
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
    LOG_INFO("Game module unloaded");
}

void ScriptModule::expect(const std::filesystem::path& modulePath, EventBus& events) {
    unload(events);
    m_modulePath = modulePath;
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

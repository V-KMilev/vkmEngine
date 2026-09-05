#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/script_module.h"

#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

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
using BuildSceneFn   = void (*)(Scene&);
using SetupNetworkFn = void (*)(NetSession&);

// Best-effort sweep of stale "<stem>.loaded.*.<ext>" copies left by previous
// runs (a clean exit removes its own, but a crash can leave one). A copy still
// locked by a concurrent editor just fails to delete and is skipped.
void removeStaleCopies(const std::filesystem::path& src) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string prefix = src.stem().string() + ".loaded.";
    const std::string ext = src.extension().string();
    for (fs::directory_iterator it(src.parent_path(), ec), end; it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(prefix, 0) == 0 && it->path().extension() == ext) {
            fs::remove(it->path(), ec);
        }
    }
}
} // namespace

void ScriptModule::releaseRegistrations() {
    BehaviorRegistry::get().clear();
    NetSchema::get().clear();

    // The session holds two lambdas that are this module's code, and assigning
    // over a std::function runs the old target's manager - so they go before
    // the dlclose, not after. The destructor does not come here, and says why.
    if (m_net) m_net->onSpawn(nullptr, nullptr);
    m_net = nullptr;
}

ScriptModule::~ScriptModule() {
    // The registries only, never the session: every host declares its module
    // before its Engine, so the session is already destroyed here. The registries
    // outlive the module, so what it put in them is cleared before the dlclose.
    BehaviorRegistry::get().clear();
    NetSchema::get().clear();
    // Unload before deleting so the copy file is no longer locked.
    m_lib.unload();
    if (!m_loadedCopyPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_loadedCopyPath, ec);
    }
}

bool ScriptModule::load(const std::string& modulePath) {
    // DynamicLibrary::load unloads whatever is open before it opens the next,
    // so a failed load would otherwise leave the previous project's factories
    // and wire thunks pointing into a library that is no longer there.
    releaseRegistrations();

    m_modulePath = modulePath;
    removeStaleCopies(std::filesystem::path(modulePath));
    if (loadCopyAndRegister()) return true;

    // The log lines above name the reason; this is the one durable line for a
    // reader who has only the editor's Errors tab. reload() reports its own.
    reportError("Script", modulePath,
        "Gameplay module failed to load; no behavior type is registered, so "
        "behaviors in the scene are held as text and do not run.");
    return false;
}

bool ScriptModule::loadCopyAndRegister() {
    namespace fs = std::filesystem;

    const fs::path src(m_modulePath);
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        LOG_ERROR("Game module not found at '%s'", m_modulePath.c_str());
        return false;
    }

    // Best-effort: drop the previous (now-unloaded) copy so they don't pile up.
    if (!m_loadedCopyPath.empty()) {
        fs::remove(m_loadedCopyPath, ec);
        m_loadedCopyPath.clear();
    }

    // Load a copy so the original stays writable for rebuilds - Windows locks a
    // loaded DLL - and step past names a second editor still holds open, which
    // removeStaleCopies cannot sweep. Sixteen is a ceiling, not a dependency.
    fs::path copy;
    bool copied = false;
    for (int attempt = 0; attempt < 16 && !copied; ++attempt) {
        copy = src.parent_path() /
            (src.stem().string() + ".loaded." + std::to_string(m_reloadCounter++) + src.extension().string());
        fs::remove(copy, ec);
        ec.clear();
        fs::copy_file(src, copy, fs::copy_options::overwrite_existing, ec);
        copied = !ec;
    }
    // A read-only directory is an installed game, which nothing rebuilds into -
    // and guarding a rebuild is all the copy buys. Load in place rather than
    // refuse to start.
    if (!copied) {
        LOG_INFO("Cannot copy the game module aside (%s); loading '%s' in place",
            ec.message().c_str(), m_modulePath.c_str());
    }
    m_loadedCopyPath = copied ? copy.string() : std::string{};

    if (!m_lib.load(copied ? m_loadedCopyPath : m_modulePath)) return false;

    // A version mismatch is an ABI mismatch, whose symptom is a crash somewhere
    // unrelated; refusing here turns that into a sentence. No version at all is
    // refused too. See docs/reference/system/scripting.md.
    auto versionFn = reinterpret_cast<VersionFn>(m_lib.symbol("vkmModuleEngineVersion"));
    if (!versionFn) {
        LOG_ERROR("Game module '%s' declares no engine version. Rebuild it against "
                  "vkmEngine %s.", m_modulePath.c_str(), VKM_ENGINE_VERSION);
        m_lib.unload();
        return false;
    }
    if (const char* built = versionFn(); std::strcmp(built, VKM_ENGINE_VERSION) != 0) {
        LOG_ERROR("Game module '%s' was built against vkmEngine %s but this is %s. "
                  "Rebuild the module (vkm build).",
                  m_modulePath.c_str(), built, VKM_ENGINE_VERSION);
        m_lib.unload();
        return false;
    }

    auto registerFn = reinterpret_cast<RegisterFn>(m_lib.symbol("vkmRegisterBehaviors"));
    if (!registerFn) {
        LOG_ERROR("Game module '%s' has no vkmRegisterBehaviors entry", m_modulePath.c_str());
        m_lib.unload();
        return false;
    }
    registerFn();
    LOG_INFO("Loaded game module '%s' (%zu behavior type(s) registered)",
        m_modulePath.c_str(), BehaviorRegistry::get().names().size());
    return true;
}

bool ScriptModule::reload(Scene& scene) {
    if (!m_lib.isLoaded()) {
        // A failed reload leaves the module unloaded and no behaviors to
        // preserve, so this retries rather than refusing: it is the recovery
        // path after a fixed build.
        if (m_modulePath.empty()) {
            LOG_WARNING("ScriptModule::reload called but no module was ever configured");
            return false;
        }
        LOG_INFO("ScriptModule::reload: no module loaded, retrying load of '%s'", m_modulePath.c_str());
        if (!loadCopyAndRegister()) return false;

        // A load with no registry kept each behavior as text, and this is the
        // moment its type exists again - re-reading those documents through the
        // now-filled registry is what turns them back into behaviors.
        if (auto* storage = scene.storage<ScriptComponent>()) {
            storage->forEach([&](uint32_t, ScriptComponent& sc) {
                ComponentSerializer::load(ComponentSerializer::save(sc), sc);
            });
        }
        return true;
    }

    // Saved while the module is still loaded, because visitFields and typeName
    // are its code. Before onDestroy runs, so what is restored is the state the
    // author edited rather than what a teardown left behind.
    std::vector<std::pair<EntityId, nlohmann::json>> saved;
    if (auto* storage = scene.storage<ScriptComponent>()) {
        storage->forEach([&](uint32_t entityIdx, ScriptComponent& sc) {
            saved.emplace_back(scene.entityAt(entityIdx), ComponentSerializer::save(sc));
        });
    }

    // A reload ends these behaviors' lives, so they are told while the module
    // that wrote onDestroy is still mapped - otherwise a listener or a queued
    // request outlives the object that registered it.
    BehaviorSystem::endSession(scene);

    if (auto* storage = scene.storage<ScriptComponent>()) {
        storage->forEach([](uint32_t, ScriptComponent& sc) { sc.behaviors.clear(); });
    }

    releaseRegistrations();
    m_lib.unload();

    const bool loaded = loadCopyAndRegister();

    // Put back either way: with the new module loaded these become behaviors
    // again through its factories, and without one they come back as
    // UnknownBehavior, held as text until a reload that works.
    for (auto& [id, data] : saved) {
        if (scene.isAlive(id) && scene.has<ScriptComponent>(id)) {
            ComponentSerializer::load(data, scene.get<ScriptComponent>(id));
        }
    }

    if (!loaded) {
        // reportError, not the log alone: behaviors have stopped running, and
        // the editor has no log view for a reader to find that in afterwards.
        reportError("Script", m_modulePath,
            "Reload failed; behaviors are held as text and do not run. Fix the build "
            "and reload again - nothing is lost, including on save.");
        return false;
    }

    LOG_INFO("Script reload complete (%zu entit(y/ies) restored)", saved.size());
    return true;
}

void ScriptModule::unload() {
    if (!m_lib.isLoaded()) return;

    releaseRegistrations();
    m_lib.unload();
    m_modulePath.clear();

    std::error_code ec;
    if (!m_loadedCopyPath.empty()) {
        std::filesystem::remove(m_loadedCopyPath, ec);
        m_loadedCopyPath.clear();
    }
    LOG_INFO("Game module unloaded");
}

bool ScriptModule::buildScene(Scene& scene) {
    if (!m_lib.isLoaded()) return false;

    auto buildFn = reinterpret_cast<BuildSceneFn>(m_lib.symbol("vkmBuildScene"));
    if (!buildFn) return false;  // optional: most projects author a scene instead

    buildFn(scene);
    return true;
}

bool ScriptModule::setupNetwork(NetSession& session) {
    if (!m_lib.isLoaded()) return false;

    // Built from nothing every time, so nothing a previous module registered
    // survives into this one.
    NetSchema::get().clear();
    m_net = &session;

    auto setupFn = reinterpret_cast<SetupNetworkFn>(m_lib.symbol("vkmSetupNetwork"));
    if (!setupFn) return false;  // optional: a single-player project needs none

    // The engine's own types first, and from one place, because the wire index
    // is the identity and the fingerprint is order-sensitive - see
    // NetSchema::fingerprint.
    registerEngineNetTypes();
    setupFn(session);
    return true;
}

} // namespace Vkm::Engine

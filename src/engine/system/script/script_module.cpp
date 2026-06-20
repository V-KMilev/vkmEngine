#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/script_module.h"

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "io/component_serializer.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

namespace Engine {

namespace {
using RegisterFn = void (*)();

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

ScriptModule::~ScriptModule() {
    // Drop the registry's factories before unloading: they close over this
    // module's code, so the BehaviorRegistry singleton's own destruction at
    // process exit would otherwise tear down std::functions whose code has
    // already been dlclose'd here - a segfault in static teardown. Mirrors the
    // clear-before-unload ordering in reload().
    BehaviorRegistry::get().clear();
    // Unload before deleting so the copy file is no longer locked.
    m_lib.unload();
    if (!m_loadedCopyPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_loadedCopyPath, ec);
    }
}

bool ScriptModule::load(const std::string& modulePath) {
    m_modulePath = modulePath;
    removeStaleCopies(std::filesystem::path(modulePath));
    return loadCopyAndRegister();
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

    // Load a copy so the original stays writable for rebuilds (Windows locks a
    // loaded DLL). Try successive names so a leftover/locked prior copy doesn't
    // block us - clear the target first, then copy.
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
    if (!copied) {
        LOG_ERROR("Failed to copy game module '%s': %s",
            m_modulePath.c_str(), ec.message().c_str());
        return false;
    }
    m_loadedCopyPath = copy.string();

    if (!m_lib.load(m_loadedCopyPath)) return false;

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

void ScriptModule::reload(Scene& scene) {
    if (!m_lib.isLoaded()) {
        LOG_WARNING("ScriptModule::reload called but no module is loaded");
        return;
    }

    // 1. Serialize each entity's behaviors (type + reflected fields) while the
    //    current module is still loaded (visitFields/typeName are its code), and
    // 2. destroy the old behavior objects now, before unloading the module that
    //    owns their code and vtables.
    std::vector<std::pair<EntityId, nlohmann::json>> saved;
    if (auto* storage = scene.storage<ScriptComponent>()) {
        storage->forEach([&](uint32_t entityIdx, ScriptComponent& sc) {
            const EntityId id{entityIdx, scene.generationOf(entityIdx)};
            saved.emplace_back(id, ComponentSerializer::save(sc));
            sc.behaviors.clear();
        });
    }

    // 3. Drop the old module's factories, then unload it.
    BehaviorRegistry::get().clear();
    m_lib.unload();

    // 4. Load the rebuilt module + re-register.
    if (!loadCopyAndRegister()) {
        LOG_ERROR("Script reload failed; behaviors were cleared (entities kept). Rebuild and reload again.");
        return;
    }

    // 5. Recreate behaviors from the saved data via the new module's factories.
    //    Entities/other components were never touched; behaviors start fresh.
    for (auto& [id, data] : saved) {
        if (scene.isAlive(id) && scene.has<ScriptComponent>(id)) {
            ComponentSerializer::load(data, scene.get<ScriptComponent>(id));
        }
    }
    LOG_INFO("Script reload complete (%zu entit(y/ies) restored)", saved.size());
}

} // namespace Engine

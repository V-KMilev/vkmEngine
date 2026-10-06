#define VKM_LOG_CATEGORY "PLATFORM"

#include "platform/library/dynamic_library.h"

#include "logger.h"

#include "platform/windows_api.h"

#if !defined(_WIN32)
    #include <dlfcn.h>
#endif

namespace Vkm::Engine {

DynamicLibrary::~DynamicLibrary() {
    unload();
}

bool DynamicLibrary::load(const std::filesystem::path& path) {
    unload();
#if defined(_WIN32)
    // c_str() is already the wide native form; nothing is converted.
    m_handle = ::LoadLibraryW(path.c_str());
    if (!m_handle) {
        LOG_ERROR("LoadLibrary failed for '%s' (error %lu)", path.string().c_str(), ::GetLastError());
        return false;
    }
#else
    // RTLD_NOW: a missing engine symbol fails at load. RTLD_LOCAL: reloads stay isolated; a
    // gameplay module finds libvkm_core through its own DT_NEEDED.
    m_handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        const char* err = ::dlerror();
        LOG_ERROR("dlopen failed for '%s': %s", path.c_str(), err ? err : "unknown error");
        return false;
    }
#endif
    return true;
}

void DynamicLibrary::unload() {
    if (!m_handle) return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
    // Unmaps only a library with no STB_GNU_UNIQUE symbol, as built by
    // vkm_gameplay_module_options, and never under Tracy (docs/reference/scripting.md).
    // Nothing may keep a pointer into the old copy - see ScriptModule::releaseRegistrations.
    ::dlclose(m_handle);
#endif
    m_handle = nullptr;
}

void* DynamicLibrary::symbol(const char* name) const {
    if (!m_handle) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
    return ::dlsym(m_handle, name);
#endif
}

std::string DynamicLibrary::platformName(const std::string& baseName) {
#if defined(_WIN32)
    return baseName + ".dll";
#else
    return "lib" + baseName + ".so";
#endif
}

} // namespace Vkm::Engine

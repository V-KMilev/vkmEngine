#define VKM_LOG_CATEGORY "PLATFORM"

#include "platform/library/dynamic_library.h"

#include "logger.h"

#if defined(_WIN32)
    // Guard the lean-windows defines: the MinGW libstdc++ headers pulled in above
    // (via <string>) already define NOMINMAX, so an unguarded redefine warns.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #define NOGDI
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace Engine {

DynamicLibrary::~DynamicLibrary() {
    unload();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary && other) noexcept
    : m_handle(other.m_handle) {
    other.m_handle = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary && other) noexcept {
    if (this != &other) {
        unload();
        m_handle = other.m_handle;
        other.m_handle = nullptr;
    }
    return *this;
}

bool DynamicLibrary::load(const std::string& path) {
    unload();
#if defined(_WIN32)
    m_handle = ::LoadLibraryA(path.c_str());
    if (!m_handle) {
        LOG_ERROR("LoadLibrary failed for '%s' (error %lu)", path.c_str(), ::GetLastError());
        return false;
    }
#else
    // RTLD_NOW: resolve now so a missing engine symbol fails loudly at load,
    // not on first call. RTLD_LOCAL: keep the module's own symbols out of the
    // global scope so reloads stay isolated (its undefined engine symbols still
    // resolve from the rdynamic host exe).
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

} // namespace Engine

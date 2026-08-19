#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief Cross-platform handle to a loaded shared library (.dll / .so).
 *
 * Wraps LoadLibrary/GetProcAddress/FreeLibrary (Windows) and
 * dlopen/dlsym/dlclose (Linux). Move-only: it owns the OS handle and unloads on
 * destruction. Used by the editor to load the gameplay module for hot-reload.
 */
class DynamicLibrary {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();

        DynamicLibrary(const DynamicLibrary& other) = delete;
        DynamicLibrary& operator=(const DynamicLibrary& other) = delete;

        DynamicLibrary(DynamicLibrary && other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary && other) noexcept;

    public:
        /**
         * @brief Load the library at @p path. Returns false (and logs) on failure;
         * any previously loaded library is unloaded first.
         */
        bool load(const std::string& path);

        /**
         * @brief Unload the library if currently loaded.
         *
         * Idempotent: safe to call when no library is loaded and safe to call
         * more than once.
         */
        void unload();

        bool isLoaded() const { return m_handle != nullptr; }

        /**
         * @brief Resolve @p name to a symbol address, or nullptr if absent. Cast the
         * result to the expected function-pointer type.
         */
        void* symbol(const char* name) const;

        /**
         * @brief Map a base name to its platform filename: "game" -> "game.dll"
         * (Windows) or "libgame.so" (Linux).
         */
        static std::string platformName(const std::string& baseName);

    private:
        void* m_handle = nullptr;
};

} // namespace Vkm::Engine

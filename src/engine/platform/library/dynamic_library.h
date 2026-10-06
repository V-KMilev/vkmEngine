#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

/**
 * @brief Cross-platform handle to a loaded shared library (.dll / .so).
 *
 * Owns the OS handle and unloads on destruction.
 */
class DynamicLibrary {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();

        DynamicLibrary(const DynamicLibrary& other) = delete;
        DynamicLibrary& operator=(const DynamicLibrary& other) = delete;

        DynamicLibrary(DynamicLibrary && other) = delete;
        DynamicLibrary& operator=(DynamicLibrary && other) = delete;

    public:
        /**
         * @brief Load the library at @p path, unloading any loaded before it.
         *
         * A path, not a string: narrowing through the Windows code page cannot spell every
         * directory name, so the loader gets the native encoding.
         *
         * @param path Library file to open.
         * @return Whether it opened; a failure is logged.
         */
        [[nodiscard]] bool load(const std::filesystem::path& path);

        /**
         * @brief Unload the library if currently loaded; idempotent.
         */
        void unload();

        /**
         * @brief Resolve @p name to a symbol address.
         *
         * @param name The exported symbol's name.
         * @return Its address, to be cast to the expected function-pointer type;
         *         nullptr if absent or nothing is loaded.
         */
        void* symbol(const char* name) const;

        /**
         * @brief Map a base name to its platform filename: "game" -> "game.dll"
         *        (Windows) or "libgame.so" (elsewhere).
         *
         * @param baseName The library's name without prefix or extension.
         * @return The filename the platform's loader expects.
         */
        static std::string platformName(const std::string& baseName);

        bool isLoaded() const { return m_handle != nullptr; }

    private:
        void* m_handle = nullptr;
};

} // namespace Vkm::Engine

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief Process-wide ring buffer of shader compile/link errors.
 *
 * Hot-reloaded shaders failing to compile push entries here (otherwise the
 * previous LOG_ERROR-and-discard meant the editor had no way to inspect a
 * typo after the next keystroke restored a valid file).
 *
 * Consecutive duplicates (same name + same message) bump a count on the
 * existing entry instead of growing the buffer - rapid-fire editing in
 * an external editor produces many identical save events.
 */
class ShaderErrorLog {
    public:
        ShaderErrorLog(const ShaderErrorLog& other) = delete;
        ShaderErrorLog& operator=(const ShaderErrorLog& other) = delete;

        ShaderErrorLog(ShaderErrorLog && other) = delete;
        ShaderErrorLog& operator=(ShaderErrorLog && other) = delete;

    public:
        struct Entry {
            std::chrono::system_clock::time_point timestamp;
            std::string  shaderName;
            std::string  definesSummary;   ///< Joined feature defines (empty for the ubershader).
            std::string  message;
            unsigned int repeatCount = 1;
        };

        static ShaderErrorLog& get();

        void push(
            std::string shaderName,
            std::string definesSummary,
            std::string message
        );

        /// Drop all entries whose @p shaderName matches. Called after a
        /// successful recompile so stale errors don't linger in the UI.
        void clearFor(const std::string& shaderName);

        void clearAll();

        /// Returns a copy of the buffer (newest first).
        std::vector<Entry> snapshot() const;

        std::size_t size() const;

        static constexpr std::size_t CAPACITY = 64;

    private:
        ShaderErrorLog() = default;

        std::vector<Entry> m_entries;  ///< Ordered oldest-first.
};

} // namespace Engine

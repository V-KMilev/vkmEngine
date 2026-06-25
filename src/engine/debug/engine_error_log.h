#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief Ring buffer of recoverable engine errors worth surfacing in the editor.
 *
 * Engine code reports recoverable errors through the free reportError() seam
 * below. reportError() always logs (so the headless runtime still records the
 * failure) and, when an editor has installed a sink via setErrorSink(), also
 * appends here for display in the editor's Errors tab. The runtime installs no
 * sink, so it carries no buffer - errors only reach the log.
 *
 * Consecutive duplicates (same category + source + message) bump a count on the
 * existing entry instead of growing the buffer.
 */
class EngineErrorLog {
    public:
        EngineErrorLog() = default;

        EngineErrorLog(const EngineErrorLog& other) = delete;
        EngineErrorLog& operator=(const EngineErrorLog& other) = delete;

        EngineErrorLog(EngineErrorLog && other) = delete;
        EngineErrorLog& operator=(EngineErrorLog && other) = delete;

        struct Entry {
            std::chrono::system_clock::time_point timestamp;
            std::string  category;      ///< Subsystem that raised it ("Behavior", ...).
            std::string  source;        ///< What raised it (e.g. "MyBehavior / onUpdate").
            std::string  message;
            unsigned int repeatCount = 1;
        };

        /**
         * @brief Record an error. A consecutive duplicate (same category + source
         * + message) bumps the existing entry's count instead of appending.
         */
        void push(std::string category, std::string source, std::string message);

        /** @brief Drop every entry. */
        void clearAll();

        /** @brief Copy of the buffer, newest first. */
        std::vector<Entry> snapshot() const;
        std::size_t size() const;

        /**
         * @brief Monotonic count of distinct errors ever pushed. A deduped repeat
         * does NOT bump it, so the editor can diff it frame to frame and toast
         * only genuinely new errors instead of once per throwing frame.
         */
        unsigned long long totalPushed() const { return m_totalPushed; }

        static constexpr std::size_t CAPACITY = 64;

    private:
        std::vector<Entry> m_entries;        ///< Ordered oldest-first.
        unsigned long long m_totalPushed = 0;
};

/**
 * @brief Report a recoverable engine error. Always logs (LOG_ERROR); also records
 * into the installed sink, if any. Free-form category/source/message.
 */
void reportError(const char* category, std::string source, std::string message);

/** @brief Install (nullptr to clear) the sink reportError() records into. */
void setErrorSink(EngineErrorLog* sink);

} // namespace Engine

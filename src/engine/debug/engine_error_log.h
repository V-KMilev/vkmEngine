#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Bounded log of recoverable engine errors, for a host to surface.
 *
 * Filled by reportError() once a host installs it with setErrorSink().
 */
class EngineErrorLog {
    public:
        struct Entry {
            std::chrono::system_clock::time_point timestamp;
            std::string  category;      ///< Subsystem, e.g. "Behavior".
            std::string  source;        ///< e.g. "MyBehavior / onUpdate".
            std::string  message;
            unsigned int repeatCount = 1;
        };

        static constexpr std::size_t CAPACITY = 64;

    public:
        EngineErrorLog()  = default;
        ~EngineErrorLog() = default;

        EngineErrorLog(const EngineErrorLog& other) = delete;
        EngineErrorLog& operator=(const EngineErrorLog& other) = delete;

        EngineErrorLog(EngineErrorLog && other) = delete;
        EngineErrorLog& operator=(EngineErrorLog && other) = delete;

    public:
        /**
         * @brief Record an error, dropping the oldest entries past CAPACITY.
         *
         * A consecutive duplicate bumps the last entry's count instead.
         *
         * @param category Subsystem that raised it.
         * @param source   What raised it.
         * @param message  What went wrong.
         */
        void push(std::string category, std::string source, std::string message);

        /**
         * @brief Drop every buffered entry.
         */
        void clearAll();

        /**
         * @brief The buffered entries.
         *
         * @return The live entry list, oldest first.
         */
        const std::vector<Entry>& entries() const { return m_entries; }

        /**
         * @brief Monotonic count of entries ever appended.
         *
         * A deduped repeat does not bump it, so a frame-to-frame diff shows only new errors.
         *
         * @return The count, never reduced by eviction or clearAll().
         */
        unsigned long long totalPushed() const { return m_totalPushed; }

    private:
        std::vector<Entry> m_entries;        ///< Oldest first.
        unsigned long long m_totalPushed = 0;
};

/**
 * @brief Report a recoverable engine error.
 *
 * Always logs; also records into the installed sink, if any.
 *
 * @param category Subsystem that raised it; null is taken as empty.
 * @param source   What raised it, free-form.
 * @param message  What went wrong, free-form.
 */
void reportError(const char* category, std::string source, std::string message);

/**
 * @brief Install the sink that reportError() appends recorded errors into.
 *
 * @param sink Receives future reports; nullptr clears the sink.
 */
void setErrorSink(EngineErrorLog* sink);

} // namespace Vkm::Engine

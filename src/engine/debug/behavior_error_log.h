#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief Process-wide ring buffer of behavior (script) runtime errors.
 *
 * A behavior whose hook throws is caught by BehaviorSystem, recorded here, and
 * disabled so it never runs again. The editor surfaces these in the Bottom
 * panel and as a toast - the gameplay-script analogue of ShaderErrorLog.
 *
 * Consecutive duplicates (same behavior + hook + message) bump a count on the
 * existing entry rather than growing the buffer.
 */
class BehaviorErrorLog {
    public:
        BehaviorErrorLog(const BehaviorErrorLog& other) = delete;
        BehaviorErrorLog& operator=(const BehaviorErrorLog& other) = delete;

        BehaviorErrorLog(BehaviorErrorLog && other) = delete;
        BehaviorErrorLog& operator=(BehaviorErrorLog && other) = delete;

    public:
        struct Entry {
            std::chrono::system_clock::time_point timestamp;
            std::string  behaviorName;
            std::string  hook;          ///< Which hook threw ("onStart" / "onUpdate" / ...).
            std::string  message;
            unsigned int repeatCount = 1;
        };

        /** @brief Process-wide singleton accessor. */
        static BehaviorErrorLog& get();

        /**
         * @brief Record an error. A consecutive duplicate (same behavior + hook +
         * message) bumps the existing entry's count instead of appending.
         */
        void push(std::string behaviorName, std::string hook, std::string message);

        /** @brief Drop every entry. */
        void clearAll();

        /** @brief Copy of the buffer, newest first. */
        std::vector<Entry> snapshot() const;
        std::size_t size() const;

        /**
         * @brief Monotonic count of distinct errors ever pushed.
         *
         * A repeat (deduped onto an existing entry) does NOT bump it, so the
         * editor can compare it frame to frame and toast only genuinely new
         * errors instead of once per throwing frame.
         */
        unsigned long long totalPushed() const { return m_totalPushed; }

        static constexpr std::size_t CAPACITY = 64;

    private:
        BehaviorErrorLog() = default;

        std::vector<Entry> m_entries;        ///< Ordered oldest-first.
        unsigned long long m_totalPushed = 0;
};

} // namespace Engine

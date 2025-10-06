#pragma once

#include <fstream>
#include <string>
#include <mutex>

/**
 * @brief Log levels for the Logger.
 */
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

/**
 * @brief Thread-safe singleton logger class for logging messages to file and console.
 *
 * Usage:
 *   - Initialize once with Logger::init(filename, level)
 *   - Use LOG_* macros for logging
 */
class Logger {
    public:
        Logger() = delete;
        ~Logger();

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

    public:
        /**
         * @brief Initializes the logger singleton.
         * @param filename Path to the log file.
         * @param level Minimum log level to output.
         * @return true if initialization succeeded, false if already initialized.
         */
        static bool init(
            const std::string& filename,
            LogLevel level = LogLevel::INFO
        );

        /**
         * @brief Gets the singleton instance of the logger.
         * @return Reference to the Logger instance.
         */
        static Logger& getInstance();

        /**
         * @brief Logs a formatted message with the given log level.
         * @param level Log level for the message.
         * @param format printf-style format string.
         * @param ... Arguments for the format string.
         */
        void log(LogLevel level, const char* format, ...);

    private:
        /**
         * @brief Private constructor. Only accessible from getInstance/init.
         * @param filename Path to the log file.
         * @param level Minimum log level to output.
         */
        Logger(const std::string& filename, LogLevel level);

        /**
         * @brief Converts a log level to its string representation.
         * @param level Log level.
         * @return String representation of the log level.
         */
        std::string levelToString(LogLevel level);

        /**
         * @brief Gets the current timestamp as a string.
         * @return Timestamp string in "YYYY-MM-DD HH:MM:SS" format.
         */
        std::string getTimestamp();

    private:
        LogLevel m_level;
        std::string m_filename;
        std::ofstream m_file;
        std::mutex m_mutex;

        static bool s_isInitialized;
        static std::unique_ptr<Logger> s_instance;
};

// Macros for easy logging
#define LOG(level, format, ...)  Logger::getInstance().log(level, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)    LOG(LogLevel::INFO, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)   LOG(LogLevel::DEBUG, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG(LogLevel::WARNING, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)   LOG(LogLevel::ERROR, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...)   LOG(LogLevel::FATAL, format, ##__VA_ARGS__)

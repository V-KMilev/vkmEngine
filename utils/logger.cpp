#include "logger.h"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstdarg>
#include <vector>

// Static member initialization
bool Logger::s_isInitialized = false;
std::unique_ptr<Logger> Logger::s_instance = nullptr;

Logger::Logger(
    const std::string& filename,
    LogLevel level
) : m_level(level),
    m_filename(filename) {
    // Ensure the log directory exists before opening the log file
    size_t lastSlash = m_filename.find_last_of("/\\");

    if (lastSlash != std::string::npos) {
        std::string dir = m_filename.substr(0, lastSlash);
        std::filesystem::create_directories(dir);
    }

    m_file.open(m_filename, std::ios::app);
}

Logger::~Logger() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool Logger::init(const std::string& filename, LogLevel level) {
    if (s_isInitialized) {
        printf("WARNING: Logger already initialized! Call Logger::init() only once.\n");
        return false;
    }
    s_isInitialized = true;
    s_instance = std::unique_ptr<Logger>(new Logger(filename, level));
    return true;
}

Logger& Logger::getInstance() {
    if (!s_instance || !s_isInitialized) {
        printf("ERROR: Logger not initialized! Call Logger::init() before using.\n");
    }
    return *s_instance;
}

void Logger::log(LogLevel level, const char* format, ...) {
    if (!m_file.is_open()) {
        printf("WARNING: Failed to open log file: %s\n", m_filename.c_str());
        return;
    }

    if (level < m_level) {
        return;
    }

    va_list args;
    va_start(args, format);

    std::vector<char> buffer(1024);
    int len = std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    if (len < 0) {
        printf("Logger formatting error.\n");
        return;
    }

    if (static_cast<size_t>(len) >= buffer.size()) {
        buffer.resize(len + 1);
        va_start(args, format);
        std::vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);
    }

    std::string formattedMsg(buffer.data());

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::ostringstream logStream;
        logStream << "[" << getTimestamp() << "] [" << levelToString(level) << "] " << formattedMsg << "\n";

        // Output to console
        printf("%s", logStream.str().c_str());

        // Output to log file
        m_file << logStream.str();
        m_file.flush();
    }
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm timeInfo;
#ifdef _WIN32
    localtime_s(&timeInfo, &nowTime);
#else
    localtime_r(&nowTime, &timeInfo);
#endif
    std::ostringstream timestampStream;
    timestampStream << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
    return timestampStream.str();
}

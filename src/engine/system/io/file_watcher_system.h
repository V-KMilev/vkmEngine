#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/system.h"

namespace Engine {

/**
 * @brief Polling file watcher. Detects mtime changes in registered
 *        directories and fires a callback per change.
 *
 * Cheap and portable (just stat per file per poll), no platform-specific
 * filesystem-event API. Backend-agnostic: the callback decides what to do -
 * the typical use is "bump a resource version" so the backend reloads.
 *
 * Polling cadence is `intervalSeconds`; sub-cadence changes batch into a
 * single callback fire per entry per tick.
 */
class FileWatcherSystem : public System {
    public:
        using OnChange = std::function<void()>;

        explicit FileWatcherSystem(float intervalSeconds = 0.5f);
        ~FileWatcherSystem() override = default;

        FileWatcherSystem(const FileWatcherSystem& other) = delete;
        FileWatcherSystem& operator=(const FileWatcherSystem& other) = delete;

        FileWatcherSystem(FileWatcherSystem && other) = delete;
        FileWatcherSystem& operator=(FileWatcherSystem && other) = delete;

    public:
        /**
         * @brief Watch `dirPath` (non-recursive) for any file changes. `onChange`
         * runs the next time the watcher polls after a change is observed.
         */
        void watch(std::string dirPath, OnChange onChange);

        void update(FrameContext& ctx) override;

    private:
        struct Entry {
            std::string path;
            OnChange    onChange;
            std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;
        };

        void checkOne(Entry& entry);

        std::vector<Entry> m_entries;
        float m_interval;
        float m_accumulator = 0.0f;
};

} // namespace Engine

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/system.h"

namespace Engine {

/**
 * @brief Polling file watcher. Detects added, modified, and removed files in
 *        registered directories and fires a callback when any change is seen.
 *
 * Cheap and portable (a stat per file per poll), with no platform-specific
 * filesystem-event API. Backend-agnostic: the callback decides what to do -
 * the typical use is "bump a resource version" so the backend reloads.
 *
 * Polling cadence is `intervalSeconds`; multiple changes within one interval
 * batch into a single callback fire per directory per tick.
 */
class FileWatcherSystem : public System {
    public:
        using OnChange = std::function<void()>;  ///< Change callback (takes no arguments).

        explicit FileWatcherSystem(float intervalSeconds = 0.5f);
        ~FileWatcherSystem() override = default;

        FileWatcherSystem(const FileWatcherSystem& other) = delete;
        FileWatcherSystem& operator=(const FileWatcherSystem& other) = delete;

        FileWatcherSystem(FileWatcherSystem && other) = delete;
        FileWatcherSystem& operator=(FileWatcherSystem && other) = delete;

    public:
        /**
         * @brief Register a directory to watch (non-recursive) for file changes.
         *
         * Its current contents are captured immediately so the first poll reports
         * only genuine changes; @p onChange then fires on the next poll after any
         * watched file is added, modified, or removed.
         *
         * @param dirPath  Directory to watch; only its immediate files are tracked.
         * @param onChange Callback invoked at most once per poll that saw a change.
         */
        void watch(std::string dirPath, OnChange onChange);

        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief One watched directory plus the file mtimes seen on the last poll.
         */
        struct Entry {
            std::string path;      ///< Watched directory (non-recursive).
            OnChange    onChange;  ///< Fired once per poll that observed any change.
            std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;  ///< filename -> last-seen mtime.
        };

        /**
         * @brief Poll one directory, firing its callback if any file was added,
         *        modified, or removed since the last poll.
         * @param entry Watch entry to scan; its mtime baseline is updated in place.
         */
        void checkOne(Entry& entry);

    private:
        std::vector<Entry> m_entries;  ///< One Entry per watched directory.
        float m_interval;              ///< Seconds between polls.
        float m_accumulator = 0.0f;    ///< Elapsed time banked toward the next poll.
};

} // namespace Engine

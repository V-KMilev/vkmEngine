#pragma once

#include <string>
#include <vector>

#include "core/system.h"
#include "system/splash/splash_frame.h"

namespace Vkm::Engine {

/**
 * @brief Shows the vendor logo, then the project's, over black at startup.
 *
 * Lives on the system rather than in a boot scene, so it survives a world swap.
 * Steps on the frame clock, not the tick, so it keeps fading while paused. Runs
 * once per process.
 */
class SplashSystem : public System {
    public:
        SplashSystem() = default;
        ~SplashSystem() override = default;

        SplashSystem(const SplashSystem& other) = delete;
        SplashSystem& operator=(const SplashSystem& other) = delete;

        SplashSystem(SplashSystem && other) = delete;
        SplashSystem& operator=(SplashSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

        /**
         * @brief Append @p path to the sequence.
         *
         * A missing file is dropped with a log line rather than shown as a black pause.
         *
         * @param path Image to show, as an absolute path.
         * @param hold Seconds at full opacity, besides the two fades.
         */
        void add(const std::string& path, float hold);

        /**
         * @brief Set the fade at each end of every entry.
         *
         * @param seconds Fade length, in and out; zero cuts.
         */
        void setFade(float seconds) { m_fade = seconds; }

    private:
        struct Entry {
            std::string path;
            float       hold = 0.0f;
        };

    private:
        std::vector<Entry> m_entries;
        size_t             m_current = 0;      ///< Index into m_entries; == size() when done.
        float              m_elapsed = 0.0f;   ///< Seconds into the current entry.
        float              m_fade    = 0.5f;   ///< Seconds.
        SplashFrame        m_frame;            ///< Published on ctx.splash.
};

} // namespace Vkm::Engine

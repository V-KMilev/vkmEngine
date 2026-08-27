#pragma once

#include <string>
#include <vector>

#include "core/system.h"
#include "system/splash/splash_frame.h"

namespace Vkm::Engine {

/**
 * @brief Shows the vendor logo, then the project's, over black at startup.
 *
 * The sequence lives on the system rather than in a boot scene, so it survives
 * the editor's first frame replacing the world with the project's scene.
 *
 * Runs in the Simulation stage on update(), which is the frame clock rather
 * than the tick: a splash is presentation and must keep fading while the
 * simulation is paused, which is what the editor is doing behind it. Nothing
 * here reads a fixed step or writes anything a tick can see.
 *
 * The sequence runs once, over the first seconds of the process, and is then
 * over for good - there is no state to re-enter, which is why Play and Stop do
 * not show it again.
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
        /**
         * @brief Append whatever the project's project.json lists.
         *
         * Runs after the host has added its own, so the engine's mark comes
         * first and the game's chain behind it.
         */
        void init(FrameContext& ctx) override;

        void update(FrameContext& ctx) override;

        /**
         * @brief Append @p path to the sequence.
         *
         * An entry whose file is not there is dropped with a log line rather
         * than shown as a black pause. Whether the file is also decodable is
         * the backend's answer to give, and it gives it the same way.
         *
         * @param path Image to show, as an absolute path.
         * @param hold Seconds at full opacity, on top of the two fades.
         */
        void add(const std::string& path, float hold);

        /**
         * @brief Set the fade at each end of every entry, in seconds.
         *
         * Uniform across the whole sequence. Zero cuts.
         *
         * @param seconds Fade length; the same one is used in and out.
         */
        void setFade(float seconds) { m_fade = seconds; }

    private:
        /**
         * @brief One logo and how long it is up for.
         *
         * `hold` is the time at full opacity; the fades are on top of it, so an
         * entry occupies fade + hold + fade seconds in total.
         */
        struct Entry {
            std::string path;
            float       hold = 0.0f;
        };

    private:
        std::vector<Entry> m_entries;
        size_t             m_current = 0;      ///< Index into m_entries; == size() when done.
        float              m_elapsed = 0.0f;   ///< Seconds into the current entry.
        float              m_fade    = 0.5f;   ///< Fade at each end of an entry, in seconds.
        SplashFrame        m_frame;            ///< This frame's product, published on ctx.splash.
};

} // namespace Vkm::Engine

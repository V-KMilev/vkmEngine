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
         * @param path    Image to show, as an absolute path.
         * @param hold    Seconds at full opacity, besides the two fades.
         * @param caption An image of a line to show small at the foot of the screen with it, as an
         *                absolute path; empty for none, as a missing file is too.
         */
        void add(const std::string& path, float hold, const std::string& caption = {});

        /**
         * @brief Append an ident to the sequence: a logo in the dark, lit by a comet's light that
         *        strikes it and leaves it red hot, rather than faded in; its caption coming in with
         *        the strike (SplashFrame::reveal).
         *
         * The comet's path is laid along the engine's mark (assets/logo/vkm_engine_logo_mono.png,
         * shaders/splash): another logo is shown, but the light flies the mark's path over it. It
         * lasts IDENT_SECONDS, its fades included.
         *
         * @param path    As add's; the comet flies its fixed path over whatever image it is.
         * @param caption As add's.
         */
        void addIdent(const std::string& path, const std::string& caption = {});

        /**
         * @brief Set the fade at each end of every entry; an ident's is at most half IDENT_SECONDS.
         *
         * @param seconds Fade length, in and out; zero cuts.
         */
        void setFade(float seconds) { m_fade = seconds; }

    private:
        struct Entry {
            std::string path;
            float       hold = 0.0f;  ///< An image's; an ident holds what IDENT_SECONDS leaves its fades.
            std::string caption;
            bool        reveal = false;  ///< An ident's.
        };

    private:
        /// Seconds each of @p entry's fades lasts.
        float fadeOf(const Entry& entry) const;

        /// Seconds @p entry shows, its fades included.
        float spanOf(const Entry& entry) const;

        /**
         * @brief Append an image entry, or warn and skip a path that is not there.
         *
         * @param path    As add's.
         * @param hold    As add's.
         * @param caption As add's.
         * @param reveal  Whether it is an ident's (addIdent).
         */
        void addEntry(const std::string& path, float hold, const std::string& caption, bool reveal);

    private:
        std::vector<Entry> m_entries;
        size_t             m_current = 0;      ///< Index into m_entries; == size() when done.
        float              m_elapsed = 0.0f;   ///< Seconds into the current entry.
        float              m_fade    = 0.5f;   ///< Seconds.
        SplashFrame        m_frame;            ///< Published on ctx.splash.
};

} // namespace Vkm::Engine

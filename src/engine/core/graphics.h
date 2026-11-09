#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "base/reflect/enum_names.h"
#include "vkm_platform/window/window_manager.h"
#include "io/project/render_settings.h"

namespace Vkm::Engine {

/**
 * @brief A quality tier a game offers its players as one choice.
 */
enum class GraphicsPreset : uint8_t {
    Performance,
    Low,
    Medium,
    High,
    Max,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief The render settings the frame is drawn with: the project's, and what a game changes over
 *        them.
 *
 * A change never turns on an effect the project has off - ambient occlusion, reflections, bloom,
 * soft shadows - so the project's look is kept, and a tier is built from the project's effects
 * each time, so a higher one brings back what a lower one turned off. A game reads settings() and
 * writes through set() and setQuality(), and changes the window through setWindowMode(); the host
 * keeps the project's through setProject(), and a player's choices through keepChoicesIn().
 */
class Graphics {
    public:
        Graphics()  = default;
        ~Graphics() = default;

        Graphics(const Graphics& other) = default;
        Graphics& operator=(const Graphics& other) = default;

        Graphics(Graphics && other) = default;
        Graphics& operator=(Graphics && other) = default;

    public:
        /**
         * @brief Draw with @p settings from the next frame, but for an effect the project has off,
         *        which stays off.
         *
         * @param settings What a game or its player chose.
         */
        void set(const RenderSettings& settings);

        /**
         * @brief Draw at @p tier: what it sets, its effects only where the project has them on; the
         *        rest as it is.
         *
         * @param tier The tier; Count, which is none, asserts and reads as the last.
         */
        void setQuality(GraphicsPreset tier);

        /**
         * @brief The highest tier the frame is drawn at.
         *
         * @return The tier setQuality() would change nothing for; none once something it sets differs.
         */
        std::optional<GraphicsPreset> quality() const;

        /**
         * @brief Show the game fullscreen or in a window from the next frame (Engine::run asks the
         *        window), and keep the mode as the player's choice.
         *
         * @param mode The mode the player chose.
         */
        void setWindowMode(Platform::WindowMode mode);

        /**
         * @brief The window mode the player chose, kept with their other choices.
         *
         * @return None until they choose one.
         */
        std::optional<Platform::WindowMode> chosenWindowMode() const { return m_windowMode; }

        /**
         * @brief Hand over the mode setWindowMode asked for since the last call, leaving none.
         *
         * @return The mode for the window to take, or none when nothing was asked.
         */
        std::optional<Platform::WindowMode> takeWindowRequest() {
            return std::exchange(m_windowRequest, std::nullopt);
        }

        /**
         * @brief Write the next frame the game draws to @p path, once.
         *
         * A request RenderSystem takes after its next frame: the game's view at viewport size,
         * without a host's panels. Neither the frame nor any after it waits on the file: the pixels
         * are read back as the GPU finishes them and written on a worker, in the order asked, a frame
         * or more later, and at the latest as the host closes. A second request before that frame
         * replaces the first; a host drawing nothing writes nothing. A `.rgb` request every frame
         * into one pipe streams raw video.
         *
         * @param path Where it goes; the directory must exist. A `.png` gets a PNG and a `.rgb` the
         *             bare rows (writeScreenshotFile); any other is refused.
         */
        void saveScreenshot(std::string path) { m_screenshotRequest = std::move(path); }

        /**
         * @brief Hand over the pending screenshot request, leaving none.
         *
         * @return The path saveScreenshot named, or empty when none is pending.
         */
        std::string takeScreenshotRequest() { return std::exchange(m_screenshotRequest, std::string()); }

        /**
         * @brief Make @p project the project's settings and draw with them, what a game changed
         *        dropped: for the host, as a session starts and as the editor edits them.
         *
         * @param project project.json's render block.
         */
        void setProject(const RenderSettings& project);

        /// Draw with the project's settings again, what a game changed dropped; for the host.
        void reset() { m_settings = m_project; }

        /**
         * @brief Keep a player's choices in @p file: read and drawn with now, and written whenever
         *        set() or setQuality() changes one, so they survive a restart.
         *
         * The choices are what a tier sets, the occlusion cull and the window mode; the rest of the
         * settings are the project's look or a game's own. For a host a player plays on: not a
         * bench, an editor or a server.
         *
         * @param file The player's file, in Roots::playerRoot.
         */
        void keepChoicesIn(std::filesystem::path file);

        /// What the frame is drawn with.
        const RenderSettings& settings() const { return m_settings; }

        /**
         * @brief The project's settings, project.json's render block, which a quality tier is built
         *        over; for the host.
         *
         * @return The settings setProject last gave.
         */
        const RenderSettings& project() const { return m_project; }

    private:
        /// Write the choices to the player's file.
        void writeChoices() const;

    private:
        RenderSettings                      m_project;            ///< A tier's base, and the effects' bound.
        RenderSettings                      m_settings;           ///< What the frame is drawn with.
        std::optional<Platform::WindowMode> m_windowMode;         ///< The player's; none until chosen.
        std::optional<Platform::WindowMode> m_windowRequest;      ///< Asked of the window, not yet taken.
        std::string                         m_screenshotRequest;  ///< A path, until RenderSystem takes it.
        std::filesystem::path               m_choicesFile;        ///< The choices' file; empty for none.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::GraphicsPreset, "Performance", "Low", "Medium", "High", "Max")
VKM_ENUM_NAMES(::Vkm::Platform::WindowMode, "Fullscreen", "Windowed")

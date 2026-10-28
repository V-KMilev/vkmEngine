#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/event/event_bus.h"
#include "core/system.h"
#include "ecs/entity.h"
#include "system/loading/loading_frame.h"
#include "system/loading/loading_screen.h"

namespace Vkm::Engine {

/**
 * @brief Covers a world with a loading screen from the frame it arrives until it is ready.
 *
 * A world arrives as the scene's epoch moves (a load, a restart, a server's next scene). It is
 * ready once the renderer's startup programs are compiled, no asset of it is still decoding and the
 * renderer owes none of its first bakes (FrameContext::readiness): its light, reflections and
 * lightmaps - or, the programs compiled, once no step of those has been done for a while
 * (setPatience). The screen then fades. A bar fills by the steps done of those seen owed, never
 * going back; a line says what is waited on; both wait while a splash shows.
 * While a host authors the world it shows only over a world the host asked for
 * (HostChrome::coverNextWorld) - a project or scene it opens - or while the renderer's programs
 * still compile; an author sees the rest arrive. A loader asks for the cover ahead of a load with
 * LoadExpected. A project gives it tips, shown in turn under the bar, and a screen of its own
 * (setScreen). Runs at Simulation, after SplashSystem, whose splash it waits out, and ahead of
 * UISystem, which lays its LoadingFrame out.
 */
class LoadingSystem : public System {
    public:
        /// Frames published covered before LoadingFrame::covering: one to be drawn, one more presented.
        static constexpr uint32_t COVERED_FRAMES = 2;

        /// Seconds the screen takes to fade once the world is ready.
        static constexpr float FADE_SECONDS = 0.4f;

        /// Seconds the screen's content takes to come in once a splash has gone (LoadingFrame::content).
        static constexpr float CONTENT_SECONDS = 0.8f;

        /// Frames a world is waited on before readiness is believed: the renderer's is a frame behind.
        static constexpr uint32_t MIN_FRAMES = 3;

        /// The patience a world is waited on with, unless a host sets another.
        static constexpr float STALL_SECONDS = 10.0f;

        /// Seconds a tip shows, its fades included.
        static constexpr float TIP_SECONDS = 7.0f;

        /// Seconds a tip takes to fade in, and out.
        static constexpr float TIP_FADE = 0.5f;

    public:
        LoadingSystem();
        ~LoadingSystem() override;

        LoadingSystem(const LoadingSystem& other) = delete;
        LoadingSystem& operator=(const LoadingSystem& other) = delete;

        LoadingSystem(LoadingSystem && other) = delete;
        LoadingSystem& operator=(LoadingSystem && other) = delete;

    public:
        void init(FrameContext& ctx) override;
        void update(FrameContext& ctx) override;
        void shutdown() override;

        /**
         * @brief How long a world is waited on with no step of it done before it is shown as it is.
         *
         * @param seconds STALL_SECONDS unless set.
         */
        void setPatience(float seconds) { m_patience = seconds; }

        /**
         * @brief The project's tips, its own screen and whether the water shows under it.
         *
         * The tips start from one drawn at random each time the screen comes up. The screen's prefab
         * is built into a scene of this system's own as the screen next shows, and again once the
         * assets it was built against are swapped out (ResourceManager::epoch) or a picture of it is
         * removed.
         *
         * @param screen The project's (Project::loading); its empty tips are dropped.
         */
        void setScreen(const LoadingScreen& screen);

        /**
         * @brief Drop the screen built from the prefab, to be built again as it next shows.
         *
         * For a host about to swap out the code its behaviors came from: a script reload.
         */
        void forgetBuiltScreen();

        /// Whether a world has arrived that is not yet ready.
        bool loading() const { return m_loading; }

        /// The screen built from the project's prefab, whose assets the world's graph holds; null for none.
        const Scene* builtScreen() const { return m_screenScene.get(); }

        /**
         * @brief What the screen names, as AssetSerializer::namesIn reads it.
         *
         * Taken as it is built and kept while the graph it was built in lives - forgotten, or another
         * screen set, its assets stay there - so a host swapping worlds can tell them from ones
         * nothing names without touching a handle.
         *
         * @return Empty until a screen is built, and again once that graph is swapped out.
         */
        const std::vector<std::pair<std::string, std::string>>& screenNames() const { return m_screenNames; }

    private:
        /// A picture of the screen held clear while it decodes, and the alphas it goes back to.
        struct Waiting {
            EntityId picture;
            float    alpha       = 1.0f;  ///< Its tint's.
            float    bottomAlpha = 1.0f;  ///< Its gradient's bottom edge's, which has a color of its own.
        };

    private:
        /// The project's tip and screen into m_frame, for a frame it shows; @p fresh, its first.
        void showScreen(FrameContext& ctx, bool fresh);

        /// Whether the screen built no longer belongs to @p resources: swapped out, or a picture gone.
        bool screenStale(const ResourceManager& resources) const;

    private:
        EventBus*  m_events   = nullptr;
        ListenerId m_expected = 0;              ///< Its LoadExpected listener.
        float      m_patience = STALL_SECONDS;  ///< See setPatience.

        uint64_t m_epoch   = 0;
        bool     m_seen    = false;  ///< m_epoch holds a world's.
        bool     m_loading = false;

        bool     m_expecting  = false;  ///< A LoadExpected came, and the load it said has not.
        bool     m_asked      = false;  ///< One came since the last update.
        uint32_t m_covered    = 0;      ///< Frames published covered since.
        bool     m_authoring  = false;  ///< As the last update saw the host.
        bool     m_coverAsked = false;  ///< The host asked this world covered, or it waits on programs.
        uint32_t m_frames     = 0;      ///< Frames since the world arrived.

        uint32_t m_programsPeak = 0;     ///< The most startup programs seen compiling since it arrived.
        uint32_t m_assetsPeak   = 0;     ///< The most assets seen decoding at once.
        uint32_t m_bakesPeak    = 0;     ///< The most bake steps seen owed.
        uint32_t m_left         = 0;     ///< Steps left, as the last frame counted them.
        float    m_stalled      = 0.0f;  ///< Seconds m_left has held.

        float        m_content = 1.0f;   ///< LoadingFrame::content.
        float        m_opacity = 0.0f;
        bool         m_shown   = false;  ///< The last frame showed the screen.
        LoadingFrame m_frame;            ///< Published on ctx.loading.

        LoadingScreen          m_screen;               ///< See setScreen.
        std::unique_ptr<Scene> m_screenScene;          ///< Its prefab, built; null with none or none built.
        uint64_t               m_screenAssets = 0;     ///< The asset graph it was built against; 0 for none.
        std::vector<Waiting>   m_screenWaiting;        ///< Its pictures held clear while they decode.
        size_t                 m_tip          = 0;     ///< The tip showing.
        float                  m_tipSeconds   = 0.0f;  ///< Seconds it has shown.

        /// See screenNames; and the asset graph they were taken in.
        std::vector<std::pair<std::string, std::string>> m_screenNames;
        uint64_t                                         m_screenNamesGraph = 0;
};

} // namespace Vkm::Engine

#pragma once

// The editor with no window: what EditorSystem holds and hands its panels (EditorContext), over a
// frame a test builds or a game that runs. The commands, the play session and the files are the
// editor's own; only its ImGui shell is missing, and Pointer stands in for the mouse where an
// overlay reads ImGui's.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "debug/error/engine_error_log.h"
#include "system/audio/audio_system.h"
#include "system/loading/loading_system.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "system/script/behavior_system.h"

#include "editor_context.h"
#include "editor_state.h"
#include "preview/preview_cache.h"
#include "session/play_session.h"
#include "session/scene_session.h"
#include "view/camera_controller_system.h"

#include "harness/world.h"

namespace Vkm::Test {

/**
 * @brief The backend's editor half, drawing nothing: the editor needs one, and a test looks at no preview.
 */
class NoPreviews : public EditorRenderHooks {
    public:
        GpuTextureId renderPreview(const PreviewRequest&, const ResourceManager&) override { return 0; }
        uint64_t previewLook() const override { return 0; }
        GpuTextureId previewTexture(uint64_t) const override { return 0; }
        void releasePreview(uint64_t) override {}
        void releaseAllPreviews() override {}
        GpuTextureId textureId(const TextureHandle&) const override { return 0; }
        GpuTextureId ensureTexture(const TextureHandle&, const ResourceManager&) override { return 0; }
        GpuTextureId chromeImage(const std::string&) override { return 0; }
        uint32_t reloadChangedShaders() override { return 0; }
        bool reloadChangedGrading() override { return false; }
};

/**
 * @brief The editor's state and services over a frame, as EditorSystem builds them.
 *
 * The importers are installed as vkm_editor installs them, before any scene I/O.
 */
struct Desk {
    Desk(
        FrameContext& frame,
        CameraControllerSystem& view,
        RenderSystem& render,
        AudioSystem& audio,
        BehaviorSystem& behaviors
    );

    EditorState    state;
    NoPreviews     hooks;
    PreviewCache   previews{hooks};
    LoadingSystem  loading;
    EngineErrorLog errors;
    PlaySession    play;
    SceneSession   session;
    EditorContext  ec;

    Scene&           scene()     { return ec.frame.scene; }
    ResourceManager& resources() { return ec.frame.resources; }

    /// Play, as the playbar's button takes it: the snapshot. The clock stays the test's to step.
    bool startPlay() { return play.start(ec.frame, state); }

    /// Stop, as the playbar's button.
    void stop() { session.stopPlaySession(ec.frame, state); }

    /// Whether a toast still shown says @p words.
    bool said(const std::string& words) const;

    /// What the toasts still shown say, oldest first: unchanged by a step that says nothing.
    std::vector<std::string> sayings() const;

    /// The scene as its file would hold it.
    std::string sceneText();
};

/**
 * @brief Where two scene documents part, as a JSON patch from @p from to @p to, first change only.
 *
 * A root's Hierarchy says what having none says, and a reparent undone leaves its old parent one, so
 * it is no difference.
 *
 * @return Empty when they hold the same scene.
 */
std::string changes(const std::string& from, const std::string& to);

/// Whether @p file holds @p bytes exactly: a check of a file's contents that prints no file.
bool holds(const std::filesystem::path& file, const std::string& bytes);

/// Every live entity's id in @p scene, ascending by slot.
std::vector<EntityId> entityIds(const Scene& scene);

/**
 * @brief The editor over a Frame of its own: nothing runs but what the test calls.
 */
struct Bench {
    Frame                  frame;
    CameraControllerSystem view;
    RenderSystem           render;
    AudioSystem            audio;
    BehaviorSystem         behaviors;
    Desk                   desk{frame.ctx, view, render, audio, behaviors};
};

/**
 * @brief The editor over a Game that runs, registered as vkm_editor registers its own: the view in the
 *        Input stage, the rest in the Editor stage, before the game's frame ends.
 *
 * The desk stands up with the engine, so use it from Game::run's callback.
 */
class EditorHost : public System {
    public:
        EditorHost(Game& game, CameraControllerSystem& view)
            : m_game(game)
            , m_view(view)
        {}

        /// Stand the editor up on @p game, before it runs.
        static EditorHost& on(Game& game);

        void init(FrameContext& ctx) override;

        /// What EditorSystem does each frame with no window: keep the crash copy, and stop when asked.
        void update(FrameContext& ctx) override;

        Desk& desk() { return *m_desk; }

    private:
        Game&                   m_game;
        CameraControllerSystem& m_view;
        std::optional<Desk>     m_desk;
};

/**
 * @brief ImGui with no window and no renderer, for an overlay that reads the mouse through it.
 *
 * The viewport is the whole 1280 by 720 display; set EditorContext::viewportPos and viewportSize to it.
 */
class Pointer {
    public:
        Pointer();
        ~Pointer();

        Pointer(const Pointer& other) = delete;
        Pointer& operator=(const Pointer& other) = delete;

        Pointer(Pointer && other) = delete;
        Pointer& operator=(Pointer && other) = delete;

    public:
        static constexpr float WIDTH  = 1280.0f;
        static constexpr float HEIGHT = 720.0f;

        /// Move the pointer to @p at, pixels from the display's top left, for the next frame.
        void moveTo(const glm::vec2& at) { ImGui::GetIO().AddMousePosEvent(at.x, at.y); }

        /// Hold or let go the left button, for the next frame.
        void button(bool down) { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, down); }

        /// Hold or let go @p key, for the next frame.
        void key(ImGuiKey key, bool down) { ImGui::GetIO().AddKeyEvent(key, down); }

        /**
         * @brief One ImGui frame, a window over the whole display current, @p draw inside it.
         *
         * @param draw What the editor draws and reads this frame.
         */
        void frame(const std::function<void()>& draw);

        /// Put @p ec's viewport over the whole display, the pointer owned by the scene.
        static void aimAt(EditorContext& ec);
};

} // namespace Vkm::Test

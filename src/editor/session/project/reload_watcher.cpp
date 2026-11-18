#include "session/project/reload_watcher.h"

#include <string>
#include <system_error>

#include "core/clock.h"
#include "core/frame_context.h"
#include "core/graphics.h"
#include "editor_context.h"
#include "editor_state.h"
#include "session/play_session.h"
#include "session/scene_session.h"
#include "system/audio/audio_system.h"
#include "system/loading/loading_system.h"
#include "system/render/editor_render_hooks.h"
#include "system/script/module/script_module.h"

namespace Vkm::Engine {

ReloadWatcher::ReloadWatcher(ScriptModule& scriptModule, BehaviorSystem& behaviors)
    : m_scriptModule(scriptModule)
    , m_behaviors(behaviors)
{
}

void ReloadWatcher::update(EditorContext& ec) {
    pollShaders(ec);

    // Watched even if it did not load: that is the module whose next build should land.
    const std::filesystem::path& module = m_scriptModule.modulePath();
    if (!module.empty()) {
        using Stamp = std::optional<std::filesystem::file_time_type>;
        std::error_code missing;
        const auto written = std::filesystem::last_write_time(stampOf(module), missing);
        const Stamp built  = missing ? Stamp() : Stamp(written);
        if (rebuilt(m_last, module, built)) ec.state.requests.ask(Ask::ReloadScripts);
        if (m_last.module != module) m_last.module = module;
        m_last.built = built;
    }

    if (ec.state.requests.take(Ask::ReloadScripts)) reload(ec);
}

void ReloadWatcher::pollShaders(EditorContext& ec) {
    m_shaderPollTimer += ec.frame.clock.getDeltaTime();
    if (m_shaderPollTimer < SHADER_POLL_INTERVAL) return;
    m_shaderPollTimer = 0.0f;

    const uint32_t reloaded = ec.hooks.reloadChangedShaders();
    if (reloaded > 0) ec.state.toasts.push(
        ToastKind::Info,
        "Reloaded " + std::to_string(reloaded) + " shader(s)"
    );
    ec.hooks.reloadChangedGrading();
}

void ReloadWatcher::reload(EditorContext& ec) {
    FrameContext& ctx  = ec.frame;
    EditorState& state = ec.state;
    PlaySession& play  = ec.play;

    // A reload runs every onStart again, where a game builds its world, so a running session would
    // get a second world beside the first: it is stopped, and played again after.
    const bool wasPlaying = play.isPlaying();

    // Stop rebuilds the scene through its serializer, leaving no component holding the module's code.
    // An edit scene takes the same round trip; a refused snapshot keeps the old module.
    if (!wasPlaying && !play.capture(ctx, state, "Script reload")) return;
    ec.session.stopPlaySession(ctx, state);
    // A failed restore kept the old world; the module stays until a Stop succeeds.
    if (play.isPlaying()) return;
    // Stop drew with the project's settings again; a tier Render Settings previews in Edit mode stays.
    if (state.previewTier) ctx.graphics.setQuality(*state.previewTier);

    // AudioSystem would see Stop's new world only after the reload.
    ec.audioSystem.stopEverything();
    // The loading screen's scene may hold the old module's behaviors: built again when next shown.
    ec.loadingSystem.forgetBuiltScreen();
    if (!m_scriptModule.reload(ctx.scene, m_behaviors, ctx.events)) {
        const char* failed = "Script reload failed; fix the build and reload again";
        state.toasts.push(ToastKind::Error, failed, ConsoleView::Errors);
        return;
    }

    // A reload drops the wire schema the module registered, which Project Settings shows.
    m_scriptModule.setupNetwork(ctx.net);

    // A snapshot that will not serialize leaves the editor stopped rather than running with no way back.
    const bool restarted = wasPlaying && play.start(ctx, state);
    if (restarted) ctx.clock.setPaused(false);

    state.toasts.push(ToastKind::Info, restarted ? "Reloaded scripts - play restarted" : "Reloaded scripts");
}

} // namespace Vkm::Engine

#include "editor/desk.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "io/scene/scene_serializer.h"

#include "import/recipe_registration.h"

namespace Vkm::Test {

Desk::Desk(
    FrameContext& frame,
    CameraControllerSystem& view,
    RenderSystem& render,
    AudioSystem& audio,
    BehaviorSystem& behaviors
)
    : session(view, previews, behaviors, loading, play)
    , ec{frame, state, view, render, hooks, previews, audio, loading, errors, session, play}
{
    registerRecipeAssetFactories();
}

bool Desk::said(const std::string& words) const {
    for (const Toast& toast : state.toasts.shown()) {
        if (toast.message.find(words) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> Desk::sayings() const {
    std::vector<std::string> words;
    for (const Toast& toast : state.toasts.shown()) words.push_back(toast.message);
    return words;
}

std::string Desk::sceneText() { return SceneSerializer::saveToString(scene(), resources()); }

namespace {

nlohmann::json withoutRootLinks(const std::string& text) {
    nlohmann::json doc = nlohmann::json::parse(text);
    for (nlohmann::json& entity : doc["entities"]) {
        nlohmann::json& components = entity["components"];
        const auto link = components.find("Hierarchy");
        if (link != components.end() && link->value("parent", 0) == 0) components.erase(link);
    }
    return doc;
}

} // namespace

std::string changes(const std::string& from, const std::string& to) {
    const nlohmann::json patch = nlohmann::json::diff(withoutRootLinks(from), withoutRootLinks(to));
    return patch.empty() ? std::string() : patch.front().dump();
}

bool holds(const std::filesystem::path& file, const std::string& bytes) { return readText(file) == bytes; }

std::vector<EntityId> entityIds(const Scene& scene) {
    std::vector<EntityId> ids;
    scene.forEachEntity([&](EntityId id) { ids.push_back(id); });
    return ids;
}

EditorHost& EditorHost::on(Game& game) {
    auto& view = game.engine.addSystem<CameraControllerSystem>(SystemStage::Input);
    return game.engine.addSystem<EditorHost>(SystemStage::Editor, game, view);
}

void EditorHost::init(FrameContext& ctx) {
    m_desk.emplace(ctx, m_view, m_game.systems.render, m_game.systems.audio, m_game.systems.behaviors);
}

void EditorHost::update(FrameContext& ctx) {
    m_desk->session.serviceRecovery(ctx, m_desk->state);
    if (m_desk->state.requests.take(Ask::StopPlay)) m_desk->stop();
}

Pointer::Pointer() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(WIDTH, HEIGHT);
    io.DeltaTime   = 1.0f / 60.0f;
    // The textures a frame asks for are never made: nothing is drawn.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

Pointer::~Pointer() { ImGui::DestroyContext(); }

void Pointer::frame(const std::function<void()>& draw) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    draw();
    ImGui::End();
    ImGui::Render();
}

void Pointer::aimAt(EditorContext& ec) {
    ec.viewportPos   = ImVec2(0.0f, 0.0f);
    ec.viewportSize  = ImVec2(WIDTH, HEIGHT);
    ec.input.pointer = PointerOwner::Scene;
}

} // namespace Vkm::Test

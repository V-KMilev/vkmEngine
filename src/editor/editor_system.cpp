#include "editor_system.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>

#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"
#include "ecs/hierarchy_utils.h"
#include "editor/camera_controller.h"
#include "visibility/visibility_system.h"
#include "render/render_system.h"
#include "render/render_pipeline.h"
#include "resource/resource_manager.h"
#include "resource/mesh_asset.h"
#include "resource/texture_asset.h"
#include "resource/material_asset.h"
#include "debug/statistics.h"
#include "visibility/visibility.h"
#include "platform/threading/thread_pool.h"

#include "generator/light_generators.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"

namespace Engine {

// Axis colors
static const ImVec4 kAxisRed    = ImVec4(0.80f, 0.18f, 0.18f, 1.00f);
static const ImVec4 kAxisGreen  = ImVec4(0.30f, 0.70f, 0.20f, 1.00f);
static const ImVec4 kAxisBlue   = ImVec4(0.20f, 0.35f, 0.85f, 1.00f);
static const ImVec4 kAxisRedHov   = ImVec4(0.90f, 0.28f, 0.28f, 1.00f);
static const ImVec4 kAxisGreenHov = ImVec4(0.40f, 0.80f, 0.30f, 1.00f);
static const ImVec4 kAxisBlueHov  = ImVec4(0.30f, 0.45f, 0.95f, 1.00f);

static constexpr float kLabelWidth = 100.0f;

// ────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ────────────────────────────────────────────────────────────────────────────

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraController* cameraController,
    VisibilitySystem* visibilitySystem,
    RenderSystem* renderSystem
)
    : m_cameraController(cameraController)
    , m_visibilitySystem(visibilitySystem)
    , m_renderSystem(renderSystem)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 0.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(8, 6);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.FrameRounding     = 2.0f;
    style.FramePadding      = ImVec2(6, 3);
    style.FrameBorderSize   = 0.0f;
    style.GrabRounding      = 1.0f;
    style.GrabMinSize       = 8.0f;
    style.ItemSpacing       = ImVec2(6, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 14.0f;
    style.ScrollbarSize     = 11.0f;
    style.ScrollbarRounding = 2.0f;
    style.TabRounding       = 2.0f;
    style.PopupRounding     = 3.0f;
    style.ChildRounding     = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.CellPadding       = ImVec2(4, 2);
    style.SeparatorTextBorderSize = 2.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.86f, 0.87f, 0.88f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.46f, 0.47f, 0.50f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.13f, 0.13f, 0.14f, 0.97f);
    c[ImGuiCol_Border]                = ImVec4(0.22f, 0.22f, 0.24f, 0.80f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.28f, 0.31f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.36f, 0.36f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.44f, 0.44f, 0.48f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.40f, 0.68f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.33f, 0.56f, 0.88f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.42f, 0.66f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.28f, 0.48f, 0.76f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.23f, 0.42f, 0.68f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.28f, 0.48f, 0.76f, 0.55f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.28f, 0.48f, 0.76f, 0.75f);
    c[ImGuiCol_Separator]             = ImVec4(0.22f, 0.22f, 0.24f, 0.60f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.33f, 0.56f, 0.88f, 0.60f);
    c[ImGuiCol_Tab]                   = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.28f, 0.48f, 0.76f, 0.75f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.22f, 0.38f, 0.60f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.40f, 0.68f, 1.00f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.40f, 0.68f, 1.00f, 0.80f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_TableBorderStrong]     = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

EditorSystem::~EditorSystem() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

// ────────────────────────────────────────────────────────────────────────────
// Widget Helpers
// ────────────────────────────────────────────────────────────────────────────

bool EditorSystem::drawVec3Control(const char* label, float* values,
                                    float resetValue, float speed) {
    bool changed = false;
    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize(lineHeight + 2.0f, lineHeight);
    float inputWidth = (ImGui::GetContentRegionAvail().x - kLabelWidth
                        - buttonSize.x * 3 - ImGui::GetStyle().ItemSpacing.x * 5) / 3.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);

    ImGui::PushStyleColor(ImGuiCol_Button, kAxisRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisRedHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisRed);
    if (ImGui::Button("X", buttonSize)) { values[0] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##X", &values[0], speed, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_Button, kAxisGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisGreenHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisGreen);
    if (ImGui::Button("Y", buttonSize)) { values[1] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##Y", &values[1], speed, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_Button, kAxisBlue);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisBlueHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisBlue);
    if (ImGui::Button("Z", buttonSize)) { values[2] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##Z", &values[2], speed, 0.0f, 0.0f, "%.2f");

    ImGui::PopID();
    return changed;
}

void EditorSystem::drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1);
}

const char* EditorSystem::getEntityDisplayName(const Scene& scene, EntityId id) const {
    if (scene.has<Name>(id)) {
        const auto& name = scene.get<Name>(id);
        if (name.value[0] != '\0') return name.value;
    }
    // Fallback to type-based name
    static thread_local char buf[64];
    const char* typeName = "Entity";
    if (scene.has<Camera>(id))    typeName = "Camera";
    else if (scene.has<Light>(id)) {
        auto& l = scene.get<Light>(id);
        typeName = l.type == LightType::Directional ? "Dir Light" :
                   l.type == LightType::Point ? "Point Light" : "Spot Light";
    }
    else if (scene.has<Mesh>(id)) typeName = scene.has<Animation>(id) ? "Animated Mesh" : "Mesh";
    else if (scene.has<Animation>(id)) typeName = "Animation";
    snprintf(buf, sizeof(buf), "%s %u", typeName, id.index);
    return buf;
}

const char* EditorSystem::getEntityIcon(const Scene& scene, EntityId id) const {
    if (scene.has<Camera>(id))    return "[C]";
    if (scene.has<Light>(id)) {
        auto& l = scene.get<Light>(id);
        if (l.type == LightType::Directional) return "[D]";
        if (l.type == LightType::Point)       return "[P]";
        return "[S]";
    }
    if (scene.has<Mesh>(id))      return "[M]";
    if (scene.has<Animation>(id)) return "[A]";
    return "[ ]";
}

// ────────────────────────────────────────────────────────────────────────────
// Entity Operations
// ────────────────────────────────────────────────────────────────────────────

EntityId EditorSystem::createEntity(Scene& scene, ResourceManager& resources, const char* type) {
    auto entity = scene.createEntity();
    EntityId id = entity.getID();

    scene.add(entity, Transform{});
    scene.add(entity, Name(type));

    if (std::strcmp(type, "Point Light") == 0) {
        scene.add(entity, generatePointLight());
    } else if (std::strcmp(type, "Spot Light") == 0) {
        scene.add(entity, generateSpotLight());
    } else if (std::strcmp(type, "Directional Light") == 0) {
        scene.add(entity, generateDirectionalLight());
    } else if (std::strcmp(type, "Cube") == 0) {
        auto meshHandle = resources.add(generateCube());
        auto matHandle = generateDefaultMaterial(resources);
        scene.add(entity, Mesh{meshHandle, matHandle});
    } else if (std::strcmp(type, "Sphere") == 0) {
        auto meshHandle = resources.add(generateSphere());
        auto matHandle = generateDefaultMaterial(resources);
        scene.add(entity, Mesh{meshHandle, matHandle});
    } else if (std::strcmp(type, "Camera") == 0) {
        Camera cam;
        cam.active = false;
        scene.add(entity, cam);
    }

    return id;
}

void EditorSystem::duplicateEntity(Scene& scene, EntityId source) {
    auto entity = scene.createEntity();
    EntityId newId = entity.getID();

    if (scene.has<Transform>(source)) {
        auto t = scene.get<Transform>(source);
        t.position += glm::vec3(1.0f, 0.0f, 0.0f);
        scene.add(entity, std::move(t));
    }
    if (scene.has<Name>(source)) {
        auto n = scene.get<Name>(source);
        scene.add(entity, std::move(n));
    }
    if (scene.has<Mesh>(source)) {
        scene.add(entity, scene.get<Mesh>(source));
    }
    if (scene.has<Light>(source)) {
        scene.add(entity, scene.get<Light>(source));
    }
    if (scene.has<Camera>(source)) {
        auto cam = scene.get<Camera>(source);
        cam.active = false;
        scene.add(entity, cam);
    }
    if (scene.has<Animation>(source)) {
        // Animation tracks are move-only; duplicate without tracks
        Animation anim;
        anim.duration = scene.get<Animation>(source).duration;
        anim.speed = scene.get<Animation>(source).speed;
        anim.looping = scene.get<Animation>(source).looping;
        anim.playing = false;
        scene.add(entity, std::move(anim));
    }

    m_selectedEntity = newId;
}

void EditorSystem::deleteEntity(Scene& scene, EntityId entity) {
    if (m_selectedEntity == entity) m_selectedEntity = {};

    if (scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).firstChild) {
        HierarchyUtils::destroyHierarchy(scene, entity);
    } else {
        if (scene.has<Hierarchy>(entity)) {
            HierarchyUtils::removeFromParent(scene, entity);
        }
        scene.destroyEntity(Entity{entity});
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Main Update — Static Layout
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::update(FrameContext& ctx) {
    if (m_cameraController) {
        // Don't block mouse if camera is already in right-click look mode (prevents
        // cursor escape when mouse leaves viewport bounds during drag).
        bool blockMouse = !m_viewportHovered && !m_cameraController->isLooking();
        m_cameraController->setEditorInputCapture(blockMouse, ImGui::GetIO().WantTextInput);
    }

    // Always reset to fill mode for ImGui rendering (wireframe is re-applied at end for next 3D frame)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_frameTimeHistory[m_frameTimeOffset] = ctx.deltaTime * 1000.0f;
    m_frameTimeOffset = (m_frameTimeOffset + 1) % FRAME_HISTORY_SIZE;

    // Keyboard shortcuts (allow when no text input is active)
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F1)) m_showStats     = !m_showStats;
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) m_showHierarchy = !m_showHierarchy;
        if (ImGui::IsKeyPressed(ImGuiKey_F3)) m_showInspector = !m_showInspector;
        if (ImGui::IsKeyPressed(ImGuiKey_F4)) m_showBottom    = !m_showBottom;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            deleteEntity(ctx.scene, m_selectedEntity);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_selectedEntity = {};
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)
            && m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            duplicateEntity(ctx.scene, m_selectedEntity);
        }
    }

    // ── Fullscreen editor window ──
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration
                               | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoBringToFrontOnFocus
                               | ImGuiWindowFlags_NoSavedSettings
                               | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##Editor", nullptr, rootFlags)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        drawMenuBar(ctx);

        // Zero spacing between ALL layout regions to prevent scene bleeding through gaps
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        float toolbarH = ImGui::GetCursorPosY();
        float statusBarH = ImGui::GetFrameHeight() + 4;
        float bottomH = m_showBottom ? m_bottomPanelHeight : 0.0f;
        float mainH = viewport->WorkSize.y - toolbarH - statusBarH - bottomH;

        // ── Main region (Hierarchy | Viewport | Inspector) ──
        float leftW  = m_showHierarchy ? m_leftPanelWidth : 0.0f;
        float rightW = m_showInspector ? m_rightPanelWidth : 0.0f;

        // Left: Hierarchy
        if (m_showHierarchy) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
            if (ImGui::BeginChild("##Hierarchy", ImVec2(leftW, mainH), ImGuiChildFlags_Borders)) {
                drawHierarchyPanel(ctx);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 0);
        }

        // Center: Viewport (transparent — scene renders behind)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            float centerW = viewport->WorkSize.x - leftW - rightW;
            ImVec2 vpMin = ImGui::GetCursorScreenPos();
            if (ImGui::BeginChild("##Viewport", ImVec2(centerW, mainH), ImGuiChildFlags_None)) {
                ImVec2 vpMax = ImVec2(vpMin.x + centerW, vpMin.y + mainH);
                m_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
                if (m_showStats) drawViewportOverlay(ctx);
                drawNavigationGizmo(ctx, vpMin, vpMax);
            } else {
                m_viewportHovered = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
        }

        // Right: Inspector
        if (m_showInspector) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Inspector", ImVec2(rightW, mainH), ImGuiChildFlags_Borders)) {
                drawInspectorPanel(ctx);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        // ── Bottom panel (tabbed) ──
        if (m_showBottom) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Bottom", ImVec2(0, bottomH), ImGuiChildFlags_Borders)) {
                drawBottomPanel(ctx);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        ImGui::PopStyleVar(); // ItemSpacing

        // ── Status bar ──
        drawStatusBar(ctx);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Set wireframe for next frame's 3D rendering (RenderSystem runs before EditorSystem)
    if (m_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}

// ────────────────────────────────────────────────────────────────────────────
// Menu Bar
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawMenuBar(FrameContext& ctx) {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Stats Overlay", "F1", &m_showStats);
        ImGui::MenuItem("Hierarchy",     "F2", &m_showHierarchy);
        ImGui::MenuItem("Inspector",     "F3", &m_showInspector);
        ImGui::MenuItem("Bottom Panel",  "F4", &m_showBottom);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene")) {
        drawCreateEntityMenu(ctx.scene, ctx.resources);
        ImGui::Separator();
        if (ImGui::MenuItem("Pause All Animations")) {
            ctx.scene.forEach<Animation>([](EntityId, Animation& a) { a.playing = false; });
        }
        if (ImGui::MenuItem("Resume All Animations")) {
            ctx.scene.forEach<Animation>([](EntityId, Animation& a) { a.playing = true; });
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Deselect", "Escape")) {
            m_selectedEntity = {};
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) ImGui::OpenPopup("##About");
        ImGui::EndMenu();
    }

    // About popup (must be at menu bar level to stay open after menu closes)
    if (ImGui::BeginPopup("##About")) {
        ImGui::Text("%s  v%s", APP_NAME, APP_VERSION);
        ImGui::Separator();
        ImGui::TextDisabled("Branch:");   ImGui::SameLine(); ImGui::Text("%s", APP_BRANCH);
        ImGui::TextDisabled("Commit:");   ImGui::SameLine(); ImGui::Text("%.8s", APP_COMMIT_HASH);
        ImGui::TextDisabled("Built:");    ImGui::SameLine(); ImGui::Text("%s", APP_BUILD_DATE);
        ImGui::TextDisabled("OpenGL:");   ImGui::SameLine(); ImGui::Text("%s", (const char*)glGetString(GL_VERSION));
        ImGui::TextDisabled("Renderer:"); ImGui::SameLine(); ImGui::Text("%s", (const char*)glGetString(GL_RENDERER));
        ImGui::TextDisabled("ImGui:");    ImGui::SameLine(); ImGui::Text("%s", IMGUI_VERSION);
        ImGui::EndPopup();
    }

    // FPS (right-aligned, color-coded)
    const auto& info = Engine::get().getStatistics().getFrameInfo();
    char fps[32];
    snprintf(fps, sizeof(fps), "%.0f FPS", info.frameRateInfo.frameRate);
    float fpsW = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - fpsW - 16.0f);
    float rate = info.frameRateInfo.frameRate;
    ImVec4 fpsColor = rate >= 60 ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f) :
                      rate >= 30 ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                                   ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, fpsColor);
    ImGui::TextUnformatted(fps);
    ImGui::PopStyleColor();

    ImGui::EndMenuBar();
}

// ────────────────────────────────────────────────────────────────────────────
// Create Entity Menu
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawCreateEntityMenu(Scene& scene, ResourceManager& resources) {
    if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Empty Entity")) {
            m_selectedEntity = createEntity(scene, resources, "Empty");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cube"))    m_selectedEntity = createEntity(scene, resources, "Cube");
        if (ImGui::MenuItem("Sphere"))  m_selectedEntity = createEntity(scene, resources, "Sphere");
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light"))       m_selectedEntity = createEntity(scene, resources, "Point Light");
        if (ImGui::MenuItem("Spot Light"))        m_selectedEntity = createEntity(scene, resources, "Spot Light");
        if (ImGui::MenuItem("Directional Light")) m_selectedEntity = createEntity(scene, resources, "Directional Light");
        ImGui::Separator();
        if (ImGui::MenuItem("Camera"))  m_selectedEntity = createEntity(scene, resources, "Camera");
        ImGui::EndMenu();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Viewport Overlay (Stats)
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawViewportOverlay(const FrameContext& ctx) {
    ImVec2 regionSize = ImGui::GetContentRegionAvail();
    ImVec2 overlayPos(regionSize.x - 276, 4);

    ImGui::SetCursorPos(overlayPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.11f, 0.12f, 0.72f));
    if (ImGui::BeginChild("##StatsOverlay", ImVec2(272, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders)) {
        const auto& info = Engine::get().getStatistics().getFrameInfo();

        float avgMs = 0.0f, maxMs = 0.0f, minMs = 1000.0f;
        for (int i = 0; i < FRAME_HISTORY_SIZE; ++i) {
            avgMs += m_frameTimeHistory[i];
            if (m_frameTimeHistory[i] > maxMs) maxMs = m_frameTimeHistory[i];
            if (m_frameTimeHistory[i] > 0.0f && m_frameTimeHistory[i] < minMs)
                minMs = m_frameTimeHistory[i];
        }
        avgMs /= FRAME_HISTORY_SIZE;
        m_frameTimeMax = m_frameTimeMax * 0.95f + maxMs * 0.05f;

        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%.1f FPS | %.2f ms", info.frameRateInfo.frameRate, avgMs);
        ImGui::PlotLines("##FT", m_frameTimeHistory, FRAME_HISTORY_SIZE,
                         m_frameTimeOffset, overlay, 0.0f,
                         std::max(m_frameTimeMax * 1.2f, 1.0f), ImVec2(256, 36));

        ImGui::TextDisabled("Min %.2f  Avg %.2f  Max %.2f", minMs, avgMs, maxMs);

        ImGui::Spacing();

        size_t total = ctx.scene.entityCount();
        size_t vis = ctx.visibility ? ctx.visibility->entities.size() : 0;
        float pct = total > 0 ? (static_cast<float>(vis) / static_cast<float>(total)) * 100.0f : 0.0f;
        ImGui::Text("Entities: %zu  Visible: %zu (%.1f%%)", total, vis, pct);
        ImGui::Text("Draws: %u  Passes: %u", info.renderSystemInfo.drawCalls, info.renderSystemInfo.renderPasses);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ────────────────────────────────────────────────────────────────────────────
// Navigation Gizmo (ImGui DrawList)
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawNavigationGizmo(const FrameContext& ctx, ImVec2 regionMin, ImVec2 regionMax) {
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float gizmoSize = 60.0f;
    float padding = 16.0f;
    ImVec2 center(regionMax.x - gizmoSize - padding, regionMax.y - gizmoSize - padding);

    // Transform world axes by camera view rotation
    glm::mat3 viewRot = glm::mat3(ctx.visibility->view);
    glm::vec3 axisX = viewRot * WORLD_AXIS_X_RIGHT;
    glm::vec3 axisY = viewRot * WORLD_AXIS_Y_UP;
    glm::vec3 axisZ = viewRot * WORLD_AXIS_Z_FORWARD;

    float axisLen = gizmoSize * 0.8f;

    struct AxisDraw { glm::vec3 dir; ImU32 col; const char* label; };
    AxisDraw axes[] = {
        { axisX, IM_COL32(220, 60, 60, 255),  "X" },
        { axisY, IM_COL32(80, 190, 60, 255),   "Y" },
        { axisZ, IM_COL32(60, 100, 220, 255),  "Z" },
    };

    // Sort by depth (draw back-to-front)
    std::sort(std::begin(axes), std::end(axes),
        [](const AxisDraw& a, const AxisDraw& b) { return a.dir.z < b.dir.z; });

    // Background circle
    drawList->AddCircleFilled(center, gizmoSize * 0.5f, IM_COL32(20, 20, 22, 160), 32);
    drawList->AddCircle(center, gizmoSize * 0.5f, IM_COL32(50, 50, 55, 200), 32, 1.0f);

    for (const auto& axis : axes) {
        ImVec2 endPt(center.x + axis.dir.x * axisLen,
                     center.y - axis.dir.y * axisLen);  // Y flipped for screen coords

        drawList->AddLine(center, endPt, axis.col, 2.0f);

        // Arrow tip circle
        drawList->AddCircleFilled(endPt, 5.0f, axis.col, 8);

        // Label
        ImVec2 labelPos(endPt.x - 3.0f, endPt.y - 6.0f);
        drawList->AddText(labelPos, IM_COL32(255, 255, 255, 220), axis.label);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Hierarchy Panel
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawHierarchyPanel(FrameContext& ctx) {
    auto& scene = ctx.scene;

    ImGui::Text("Hierarchy");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
    if (ImGui::SmallButton("+")) {
        ImGui::OpenPopup("##CreatePopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create Entity");
    if (ImGui::BeginPopup("##CreatePopup")) {
        drawCreateEntityMenu(scene, ctx.resources);
        ImGui::EndPopup();
    }

    ImGui::Separator();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Filter", "Search...", m_hierarchyFilter, sizeof(m_hierarchyFilter));
    ImGui::Spacing();

    bool hasFilter = m_hierarchyFilter[0] != '\0';

    if (ImGui::BeginChild("##Tree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        if (hasFilter) {
            scene.forEach<Transform>([&](EntityId id, const Transform&) {
                const char* name = getEntityDisplayName(scene, id);
                bool match = false;
                for (const char* p = name; *p; ++p) {
                    const char* s = m_hierarchyFilter;
                    const char* t = p;
                    while (*s && *t && tolower(static_cast<unsigned char>(*s)) ==
                                        tolower(static_cast<unsigned char>(*t))) { ++s; ++t; }
                    if (!*s) { match = true; break; }
                }
                if (match) {
                    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                         | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (m_selectedEntity == id) f |= ImGuiTreeNodeFlags_Selected;
                    ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id.index)),
                                     f, "%s  %s", getEntityIcon(scene, id), name);
                    if (ImGui::IsItemClicked()) m_selectedEntity = id;
                    drawEntityContextMenu(scene, id);
                }
            });
        } else {
            scene.forEach<Transform>([&](EntityId id, const Transform&) {
                bool isRoot = !scene.has<Hierarchy>(id) || !scene.get<Hierarchy>(id).parent;
                if (isRoot) drawEntityNode(scene, id);
            });
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            m_selectedEntity = {};
        }

        // Right-click on empty space
        if (ImGui::BeginPopupContextWindow("##HierarchyCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            drawCreateEntityMenu(scene, ctx.resources);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%zu entities", scene.entityCount());
}

void EditorSystem::drawEntityNode(Scene& scene, EntityId entity) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_FramePadding;

    bool hasChildren = scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).firstChild;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;

    const char* name = getEntityDisplayName(scene, entity);
    bool nodeOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity.index)),
        flags, "%s  %s", getEntityIcon(scene, entity), name);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_selectedEntity = entity;

    drawEntityContextMenu(scene, entity);

    if (nodeOpen && hasChildren) {
        HierarchyUtils::forEachChild(scene, entity, [&](EntityId child) {
            drawEntityNode(scene, child);
        });
        ImGui::TreePop();
    }
}

void EditorSystem::drawEntityContextMenu(Scene& scene, EntityId entity) {
    if (!ImGui::BeginPopupContextItem()) return;

    ImGui::TextDisabled("%s", getEntityDisplayName(scene, entity));
    ImGui::Separator();

    if (ImGui::MenuItem("Select")) m_selectedEntity = entity;
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateEntity(scene, entity);
    if (ImGui::MenuItem("Delete", "Del")) deleteEntity(scene, entity);

    if (scene.has<Transform>(entity)) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Transform")) {
            auto& t = scene.get<Transform>(entity);
            t.position = glm::vec3(0.0f);
            t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            t.scale    = glm::vec3(1.0f);
        }
    }

    if (scene.has<Light>(entity)) {
        auto& light = scene.get<Light>(entity);
        if (ImGui::MenuItem(light.enabled ? "Disable Light" : "Enable Light")) {
            light.enabled = !light.enabled;
        }
    }

    if (scene.has<Mesh>(entity)) {
        auto& mesh = scene.get<Mesh>(entity);
        if (ImGui::MenuItem(mesh.visible ? "Hide" : "Show")) {
            mesh.visible = !mesh.visible;
        }
    }

    ImGui::EndPopup();
}

// ────────────────────────────────────────────────────────────────────────────
// Inspector Panel
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawInspectorPanel(FrameContext& ctx) {
    ImGui::Text("Inspector");
    ImGui::Separator();

    if (!m_selectedEntity || !ctx.scene.isAlive(m_selectedEntity)) {
        ImGui::TextDisabled("No entity selected");
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = m_selectedEntity;

    // Name editing
    {
        if (!scene.has<Name>(id)) {
            scene.add(Entity{id}, Name(getEntityDisplayName(scene, id)));
        }
        auto& name = scene.get<Name>(id);
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##Name", name.value, sizeof(name.value));
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (scene.has<Transform>(id))  drawTransformSection(scene, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, id);
    if (scene.has<Light>(id))      drawLightSection(scene, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, id);
}

void EditorSystem::drawAddComponentMenu(Scene& scene, EntityId id) {
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("##AddComp");
    }

    if (ImGui::BeginPopup("##AddComp")) {
        if (!scene.has<Mesh>(id) && ImGui::MenuItem("Mesh")) {
            scene.add(Entity{id}, Mesh{});
        }
        if (!scene.has<Light>(id) && ImGui::MenuItem("Light")) {
            scene.add(Entity{id}, generatePointLight());
        }
        if (!scene.has<Camera>(id) && ImGui::MenuItem("Camera")) {
            Camera cam;
            cam.active = false;
            scene.add(Entity{id}, cam);
        }
        if (!scene.has<Animation>(id) && ImGui::MenuItem("Animation")) {
            scene.add(Entity{id}, Animation{});
        }
        ImGui::EndPopup();
    }
}

// ── Component Sections ──

void EditorSystem::drawTransformSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    auto& t = scene.get<Transform>(id);
    drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (drawVec3Control("Rotation", glm::value_ptr(euler), 0.0f, 0.5f)) {
        t.rotation = glm::quat(glm::radians(euler));
    }

    drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

    if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
        glm::mat4 worldMat = HierarchyUtils::computeWorldMatrix(scene, id);
        glm::vec3 worldPos(worldMat[3]);
        ImGui::TextDisabled("  World: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y, worldPos.z);
    }
    ImGui::Spacing();
}

void EditorSystem::drawMeshSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Mesh##Sec", ImGuiTreeNodeFlags_DefaultOpen);
    // Remove button
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    char removeId[32]; snprintf(removeId, sizeof(removeId), "x##RemMesh%u", id.index);
    if (ImGui::SmallButton(removeId)) { scene.remove<Mesh>(Entity{id}); ImGui::PopStyleColor(); return; }
    ImGui::PopStyleColor();

    if (!open) return;
    auto& mesh = scene.get<Mesh>(id);
    drawPropertyLabel("Mesh ID");    ImGui::Text("#%u", mesh.mesh.id());
    drawPropertyLabel("Material ID"); ImGui::Text("#%u", mesh.material.id());
    drawPropertyLabel("Visible");    ImGui::Checkbox("##MeshVis", &mesh.visible);
    ImGui::Spacing();
}

void EditorSystem::drawLightSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Light##Sec", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    char removeId[32]; snprintf(removeId, sizeof(removeId), "x##RemLight%u", id.index);
    if (ImGui::SmallButton(removeId)) { scene.remove<Light>(Entity{id}); ImGui::PopStyleColor(); return; }
    ImGui::PopStyleColor();

    if (!open) return;
    auto& light = scene.get<Light>(id);

    drawPropertyLabel("Type");
    const char* typeNames[] = {"Directional", "Point", "Spot"};
    int typeIdx = static_cast<int>(light.type);
    if (ImGui::Combo("##LType", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
        light.type = static_cast<LightType>(typeIdx);
    }

    drawPropertyLabel("Color");
    ImGui::ColorEdit3("##LColor", glm::value_ptr(light.color), ImGuiColorEditFlags_Float);

    drawPropertyLabel("Intensity");
    ImGui::DragFloat("##LIntensity", &light.intensity, 0.1f, 0.0f, 100.0f, "%.1f");

    if (light.type != LightType::Directional) {
        drawPropertyLabel("Radius");
        ImGui::DragFloat("##LRadius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");
    }

    if (light.type == LightType::Spot) {
        float innerDeg = glm::degrees(light.innerConeAngle);
        float outerDeg = glm::degrees(light.outerConeAngle);
        drawPropertyLabel("Inner Cone");
        if (ImGui::DragFloat("##InnerC", &innerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
            light.innerConeAngle = glm::radians(innerDeg);
        drawPropertyLabel("Outer Cone");
        if (ImGui::DragFloat("##OuterC", &outerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
            light.outerConeAngle = glm::radians(outerDeg);
    }

    drawPropertyLabel("Shadows");  ImGui::Checkbox("##Shad", &light.castShadows);
    drawPropertyLabel("Enabled");  ImGui::Checkbox("##LEn", &light.enabled);
    ImGui::Spacing();
}

void EditorSystem::drawCameraSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Camera##Sec", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    char removeId[32]; snprintf(removeId, sizeof(removeId), "x##RemCam%u", id.index);
    if (ImGui::SmallButton(removeId)) { scene.remove<Camera>(Entity{id}); ImGui::PopStyleColor(); return; }
    ImGui::PopStyleColor();

    if (!open) return;
    auto& cam = scene.get<Camera>(id);

    drawPropertyLabel("Projection");
    const char* projNames[] = {"Perspective", "Orthographic"};
    int projIdx = static_cast<int>(cam.projection);
    if (ImGui::Combo("##CProj", &projIdx, projNames, IM_ARRAYSIZE(projNames)))
        cam.projection = static_cast<ProjectionType>(projIdx);

    if (cam.projection == ProjectionType::Perspective) {
        float fovDeg = glm::degrees(cam.fovY);
        drawPropertyLabel("FOV");
        if (ImGui::SliderFloat("##CFOV", &fovDeg, 10.0f, 170.0f, "%.0f deg"))
            cam.fovY = glm::radians(fovDeg);
    } else {
        drawPropertyLabel("Ortho Height");
        ImGui::DragFloat("##COrthoH", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
    }

    drawPropertyLabel("Near Clip"); ImGui::DragFloat("##CNear", &cam.zNear, 0.01f, 0.001f, cam.zFar, "%.3f");
    drawPropertyLabel("Far Clip");  ImGui::DragFloat("##CFar", &cam.zFar, 1.0f, cam.zNear, 100000.0f, "%.0f");
    drawPropertyLabel("Exposure");  ImGui::DragFloat("##CExp", &cam.exposure, 0.01f, 0.0f, 10.0f, "%.2f");
    drawPropertyLabel("Active");    ImGui::Checkbox("##CAct", &cam.active);
    ImGui::Spacing();
}

void EditorSystem::drawAnimationSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Animation##Sec", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    char removeId[32]; snprintf(removeId, sizeof(removeId), "x##RemAnim%u", id.index);
    if (ImGui::SmallButton(removeId)) { scene.remove<Animation>(Entity{id}); ImGui::PopStyleColor(); return; }
    ImGui::PopStyleColor();

    if (!open) return;
    auto& anim = scene.get<Animation>(id);

    float btnW = ImGui::GetFrameHeight();
    bool playing = anim.playing;
    if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.48f, 0.76f, 1.0f));
    if (ImGui::Button(playing ? "||" : ">", ImVec2(btnW * 1.5f, 0))) anim.playing = !anim.playing;
    if (playing) ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("|<", ImVec2(btnW * 1.5f, 0))) anim.time = 0.0f;
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &anim.looping);

    if (anim.duration > 0.0f) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##ATime", &anim.time, 0.0f, anim.duration, "%.2f / %.2f s");
        ImVec4 barCol = anim.playing ? ImVec4(0.33f, 0.56f, 0.88f, 1.0f) : ImVec4(0.40f, 0.40f, 0.44f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
        ImGui::ProgressBar(anim.time / anim.duration, ImVec2(-1, 3), "");
        ImGui::PopStyleColor();
    }

    drawPropertyLabel("Speed"); ImGui::DragFloat("##ASpeed", &anim.speed, 0.01f, 0.0f, 10.0f, "%.2fx");
    ImGui::TextDisabled("Tracks: %s%s%s  Dur: %.2fs",
        anim.positionTrack.isEmpty() ? "" : "P ",
        anim.rotationTrack.isEmpty() ? "" : "R ",
        anim.scaleTrack.isEmpty() ? "" : "S ",
        anim.duration);
    ImGui::Spacing();
}

void EditorSystem::drawHierarchySection(const Scene& scene, EntityId id) {
    if (!ImGui::CollapsingHeader("Hierarchy##Sec")) return;
    const auto& h = scene.get<Hierarchy>(id);

    if (h.parent) {
        ImGui::TextDisabled("Parent:");
        ImGui::SameLine();
        if (ImGui::SmallButton(getEntityDisplayName(scene, h.parent))) {
            const_cast<EditorSystem*>(this)->m_selectedEntity = h.parent;
        }
    } else {
        ImGui::TextDisabled("Root (no parent)");
    }

    if (h.firstChild) {
        ImGui::TextDisabled("Children:");
        HierarchyUtils::forEachChild(scene, id, [&](EntityId child) {
            char lbl[96];
            snprintf(lbl, sizeof(lbl), "  %s %s", getEntityIcon(scene, child),
                     getEntityDisplayName(scene, child));
            if (ImGui::Selectable(lbl, m_selectedEntity == child)) {
                const_cast<EditorSystem*>(this)->m_selectedEntity = child;
            }
        });
    }
    ImGui::Spacing();
}

// ────────────────────────────────────────────────────────────────────────────
// Bottom Panel (Tabbed: Settings | Resources)
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawBottomPanel(FrameContext& ctx) {
    if (ImGui::BeginTabBar("##BottomTabs")) {
        if (ImGui::BeginTabItem("Settings")) {
            drawSettingsTab(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Resources")) {
            drawResourcesTab(ctx);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void EditorSystem::drawSettingsTab(FrameContext& ctx) {
    auto& window = Engine::get().getWindow();
    float colW = ImGui::GetContentRegionAvail().x / 3.0f;

    ImGui::Columns(3, "##SettCols", true);
    ImGui::SetColumnWidth(0, colW);
    ImGui::SetColumnWidth(1, colW);

    // Column 1: Visibility
    ImGui::TextDisabled("Visibility");
    ImGui::Separator();
    if (m_visibilitySystem) {
        float minPx = m_visibilitySystem->getMinPixels();
        drawPropertyLabel("Min Pixels");
        if (ImGui::DragFloat("##MinPx", &minPx, 0.1f, 0.0f, 100.0f, "%.1f"))
            m_visibilitySystem->setMinPixels(minPx);

        float maxDist = m_visibilitySystem->getMaxDistance();
        drawPropertyLabel("Max Distance");
        if (ImGui::DragFloat("##MaxD", &maxDist, 1.0f, 10.0f, 10000.0f, "%.0f"))
            m_visibilitySystem->setMaxDistance(maxDist);

        if (ctx.visibility) {
            size_t vis = ctx.visibility->entities.size();
            size_t tot = ctx.scene.entityCount();
            ImGui::TextDisabled("Culled: %zu / %zu", tot > vis ? tot - vis : 0, tot);
        }
    }

    ImGui::NextColumn();

    // Column 2: Camera
    ImGui::TextDisabled("Camera Controls");
    ImGui::Separator();
    if (m_cameraController) {
        auto& s = m_cameraController->getSettings();
        drawPropertyLabel("Move Speed");   ImGui::DragFloat("##MS", &s.moveSpeed, 0.5f, 0.1f, 200.0f);
        drawPropertyLabel("Speed Boost");  ImGui::DragFloat("##SB", &s.speedBoost, 0.1f, 1.0f, 20.0f, "%.1fx");
        drawPropertyLabel("Look Sens.");   ImGui::DragFloat("##LS", &s.lookSensitivity, 0.0001f, 0.0001f, 0.01f, "%.4f");
        if (ImGui::SmallButton("Reset##Cam")) s = CameraControllerSettings{};
    }

    ImGui::NextColumn();

    // Column 3: Display
    ImGui::TextDisabled("Display");
    ImGui::Separator();
    ImGui::Text("Res: %zux%zu", window.getWidth(), window.getHeight());

    drawPropertyLabel("VSync");
    if (ImGui::Button("On##VS", ImVec2(40, 0))) window.setVSync(true);
    ImGui::SameLine();
    if (ImGui::Button("Off##VS", ImVec2(40, 0))) window.setVSync(false);

    static int fpsLimit = 0;
    drawPropertyLabel("FPS Limit");
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("##FPSLim", &fpsLimit, 30);
    fpsLimit = std::max(0, fpsLimit);
    ImGui::SameLine();
    if (ImGui::SmallButton("Set##fps")) window.setFramerate(fpsLimit);

    ImGui::Text("Threads: %zu", ThreadPool::get().size());

    ImGui::Spacing();
    ImGui::TextDisabled("Rendering");
    ImGui::Separator();
    ImGui::Checkbox("Wireframe", &m_wireframe);

    ImGui::Spacing();
    ImGui::TextDisabled("Render Passes");
    ImGui::Separator();
    if (m_renderSystem) {
        auto& pipeline = m_renderSystem->getPipeline();
        for (size_t i = 0; i < pipeline.passCount(); ++i) {
            auto& pass = pipeline.getPass(i);
            bool enabled = pass.isEnabled();
            if (ImGui::Checkbox(pass.getName().c_str(), &enabled))
                pass.setEnabled(enabled);
        }
    }

    ImGui::Columns(1);
}

void EditorSystem::drawResourcesTab(const FrameContext& ctx) {
    const auto& scene = ctx.scene;
    float colW = ImGui::GetContentRegionAvail().x / 3.0f;

    ImGui::Columns(3, "##ResCols", true);
    ImGui::SetColumnWidth(0, colW);
    ImGui::SetColumnWidth(1, colW);

    // Column 1: Components
    ImGui::TextDisabled("Component Counts");
    ImGui::Separator();
    struct CI { const char* n; size_t c; };
    CI comps[] = {
        {"Transform", scene.count<Transform>()}, {"Mesh", scene.count<Mesh>()},
        {"Light", scene.count<Light>()}, {"Camera", scene.count<Camera>()},
        {"Animation", scene.count<Animation>()}, {"Hierarchy", scene.count<Hierarchy>()},
        {"Name", scene.count<Name>()},
    };
    for (const auto& co : comps) ImGui::Text("%-12s %zu", co.n, co.c);

    ImGui::NextColumn();

    // Column 2: Animations
    ImGui::TextDisabled("Animations");
    ImGui::Separator();
    uint32_t playing = 0, paused = 0;
    scene.forEach<Animation>([&](EntityId, const Animation& a) {
        if (a.playing) ++playing; else ++paused;
    });
    ImGui::Text("Playing: %u  Paused: %u", playing, paused);
    if (ImGui::SmallButton("Pause All")) {
        const_cast<Scene&>(scene).forEach<Animation>([](EntityId, Animation& a) { a.playing = false; });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Resume All")) {
        const_cast<Scene&>(scene).forEach<Animation>([](EntityId, Animation& a) { a.playing = true; });
    }

    ImGui::NextColumn();

    // Column 3: Lights
    ImGui::TextDisabled("Lights");
    ImGui::Separator();
    uint32_t dir = 0, pt = 0, sp = 0, dis = 0;
    scene.forEach<Light>([&](EntityId, const Light& l) {
        if (!l.enabled) { ++dis; return; }
        switch (l.type) {
            case LightType::Directional: ++dir; break;
            case LightType::Point: ++pt; break;
            case LightType::Spot: ++sp; break;
        }
    });
    ImGui::Text("Dir: %u  Point: %u  Spot: %u", dir, pt, sp);
    if (dis > 0) ImGui::Text("Disabled: %u", dis);

    ImGui::Columns(1);
}

// ────────────────────────────────────────────────────────────────────────────
// Status Bar
// ────────────────────────────────────────────────────────────────────────────

void EditorSystem::drawStatusBar(const FrameContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(8);
        ImGui::AlignTextToFramePadding();

        if (m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            ImGui::TextDisabled("Selected:");
            ImGui::SameLine(0, 4);
            ImGui::Text("%s", getEntityDisplayName(ctx.scene, m_selectedEntity));

            if (ctx.scene.has<Transform>(m_selectedEntity)) {
                const auto& pos = ctx.scene.get<Transform>(m_selectedEntity).position;
                ImGui::SameLine(0, 16);
                ImGui::TextDisabled("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
            }
        } else {
            ImGui::TextDisabled("No selection");
        }

        char right[128];
        snprintf(right, sizeof(right), "%s v%s | %s | %.8s",
                 APP_NAME, APP_VERSION, APP_BRANCH, APP_COMMIT_HASH);
        float rw = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - rw - 16);
        ImGui::TextDisabled("%s", right);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace Engine

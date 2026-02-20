#include "editor_system.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <cstdio>

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
#include "render/render_system.h"
#include "resource/resource_manager.h"
#include "debug/statistics.h"

#include "generator/light_generators.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"

namespace Engine {

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraController* cameraController,
    VisibilitySystem* visibilitySystem,
    RenderSystem* renderSystem
)
    : m_window(window)
    , m_cameraController(cameraController)
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

    m_hierarchyDirty = true;
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
        Animation anim;
        anim.duration = scene.get<Animation>(source).duration;
        anim.speed = scene.get<Animation>(source).speed;
        anim.looping = scene.get<Animation>(source).looping;
        anim.playing = false;
        scene.add(entity, std::move(anim));
    }

    m_hierarchyDirty = true;
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
    m_hierarchyDirty = true;
}

void EditorSystem::update(FrameContext& ctx) {
    if (!m_editorVisible) {
        int f5 = glfwGetKey(m_window, GLFW_KEY_F5);
        if (f5 == GLFW_PRESS && !m_f5WasDown) m_editorVisible = true;
        m_f5WasDown = (f5 == GLFW_PRESS);
        if (m_cameraController) m_cameraController->setEditorInputCapture(false, false);
        return;
    }

    if (m_cameraController) {
        bool blockMouse = !m_viewportHovered && !m_cameraController->isLooking();
        m_cameraController->setEditorInputCapture(blockMouse, ImGui::GetIO().WantTextInput);
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_frameTimeHistory[m_frameTimeOffset] = ctx.deltaTime * 1000.0f;
    m_frameTimeOffset = (m_frameTimeOffset + 1) % FRAME_HISTORY_SIZE;
    m_metrics.update(ctx.deltaTime);

    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F1)) m_showStats     = !m_showStats;
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) m_showHierarchy = !m_showHierarchy;
        if (ImGui::IsKeyPressed(ImGuiKey_F3)) m_showInspector = !m_showInspector;
        if (ImGui::IsKeyPressed(ImGuiKey_F4)) m_showBottom    = !m_showBottom;
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) m_editorVisible = false;
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

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        float toolbarH = ImGui::GetCursorPosY();
        float statusBarH = ImGui::GetFrameHeight() + 4;
        float bottomH = m_showBottom ? m_bottomPanelHeight : 0.0f;
        float mainH = viewport->WorkSize.y - toolbarH - statusBarH - bottomH;

        float leftW  = m_showHierarchy ? m_leftPanelWidth : 0.0f;
        float rightW = m_showInspector ? m_rightPanelWidth : 0.0f;

        if (m_showHierarchy) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
            if (ImGui::BeginChild("##Hierarchy", ImVec2(leftW, mainH), ImGuiChildFlags_Borders)) {
                drawHierarchyPanel(ctx);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 0);
        }

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

        if (m_showInspector) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Inspector", ImVec2(rightW, mainH), ImGuiChildFlags_Borders)) {
                drawInspectorPanel(ctx);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        if (m_showBottom) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Bottom", ImVec2(0, bottomH), ImGuiChildFlags_Borders)) {
                drawBottomPanel(ctx);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        ImGui::PopStyleVar(); // ItemSpacing

        drawStatusBar(ctx);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (m_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}

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

void EditorSystem::drawStatusBar(const FrameContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(8);
        ImGui::AlignTextToFramePadding();

        if (m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            ImGui::TextDisabled("Selected:");
            ImGui::SameLine(0, 4);
            char selName[64];
            getEntityDisplayName(ctx.scene, m_selectedEntity, selName, sizeof(selName));
            ImGui::Text("%s", selName);

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

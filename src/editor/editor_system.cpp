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

#include "debug/statistics.h"
#include "platform/window/window_manager.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"
#include "ecs/hierarchy_utils.h"
#include "camera_controller.h"
#include "system/render/render_system.h"
#include "resource/resource_manager.h"
#include "system/visibility/bounds_utils.h"
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

    style.WindowRounding    = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(8, 6);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.FrameRounding     = 3.0f;
    style.FramePadding      = ImVec2(6, 4);
    style.FrameBorderSize   = 0.0f;
    style.GrabRounding      = 2.0f;
    style.GrabMinSize       = 10.0f;
    style.ItemSpacing       = ImVec2(6, 5);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 14.0f;
    style.ScrollbarSize     = 12.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ChildRounding     = 4.0f;
    style.ChildBorderSize   = 0.5f;
    style.CellPadding       = ImVec2(4, 3);
    style.SeparatorTextBorderSize = 2.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.51f, 0.54f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.15f, 0.15f, 0.16f, 0.98f);
    c[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.22f, 0.50f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.08f, 0.09f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.44f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.36f, 0.60f, 0.92f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.46f, 0.70f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.50f, 0.78f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.25f, 0.44f, 0.70f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.26f, 0.44f, 0.70f, 0.50f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.26f, 0.44f, 0.70f, 0.70f);
    c[ImGuiCol_Separator]             = ImVec4(0.22f, 0.22f, 0.24f, 0.40f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.36f, 0.60f, 0.92f, 0.60f);
    c[ImGuiCol_Tab]                   = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.30f, 0.50f, 0.78f, 0.75f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.22f, 0.36f, 0.56f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.45f, 0.72f, 1.00f, 0.80f);
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
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

void EditorSystem::focusOnSelected(FrameContext& ctx) {
    if (!m_selectedEntity || !ctx.scene.isAlive(m_selectedEntity)) return;
    if (!ctx.scene.has<Transform>(m_selectedEntity)) return;
    if (!m_cameraController) return;

    bool hasParent = ctx.scene.has<Hierarchy>(m_selectedEntity)
                  && ctx.scene.get<Hierarchy>(m_selectedEntity).parent;

    glm::vec3 targetPos;
    float focusDistance = 5.0f;

    if (ctx.scene.has<Mesh>(m_selectedEntity)) {
        const auto& mesh = ctx.scene.get<Mesh>(m_selectedEntity);
        const auto& asset = ctx.resources.get(mesh.mesh);

        glm::mat4 model = hasParent
            ? HierarchyUtils::computeWorldMatrix(ctx.scene, m_selectedEntity)
            : Transform::computeModelMatrix(ctx.scene.get<Transform>(m_selectedEntity));

        if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
            glm::vec3 localCenter = (asset.boundsMin + asset.boundsMax) * 0.5f;
            targetPos = glm::vec3(model * glm::vec4(localCenter, 1.0f));

            glm::vec3 extent = asset.boundsMax - asset.boundsMin;
            float maxExtent = glm::max(extent.x, glm::max(extent.y, extent.z));
            const auto& t = ctx.scene.get<Transform>(m_selectedEntity);
            float maxScale = glm::max(t.scale.x, glm::max(t.scale.y, t.scale.z));
            focusDistance = glm::max(maxExtent * maxScale * 1.5f, 2.0f);
        } else {
            targetPos = glm::vec3(model[3]);
        }
    } else {
        if (hasParent) {
            glm::mat4 wm = HierarchyUtils::computeWorldMatrix(ctx.scene, m_selectedEntity);
            targetPos = glm::vec3(wm[3]);
        } else {
            targetPos = ctx.scene.get<Transform>(m_selectedEntity).position;
        }
    }

    m_cameraController->focusOn(ctx.scene, targetPos, focusDistance);
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
        bool blockMouse = (!m_viewportHovered && !m_cameraController->isLooking())
                       || m_gizmo.isOver();
        m_cameraController->setEditorInputCapture(blockMouse, ImGui::GetIO().WantTextInput);
    }

    if (m_renderSystem) m_renderSystem->setWireframe(false);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_frameTimeHistory[m_frameTimeOffset] = ctx.deltaTime * 1000.0f;
    m_frameTimeOffset = (m_frameTimeOffset + 1) % FRAME_HISTORY_SIZE;
    m_metrics.update(ctx.deltaTime);

    if (!ImGui::GetIO().WantTextInput) {
        const auto& kb = m_keybinds;

        if (isPressed(kb.toggleStats))     m_showStats     = !m_showStats;
        if (isPressed(kb.toggleHierarchy)) m_showHierarchy = !m_showHierarchy;
        if (isPressed(kb.toggleInspector)) m_showInspector = !m_showInspector;
        if (isPressed(kb.toggleBottom))    m_showBottom    = !m_showBottom;
        if (isPressed(kb.toggleEditor))    m_editorVisible = false;

        if (isPressed(kb.deleteEntity) && m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            deleteEntity(ctx.scene, m_selectedEntity);
        }
        if (isPressed(kb.deselect)) {
            m_selectedEntity = {};
        }
        if (isPressed(kb.duplicate) && m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            duplicateEntity(ctx.scene, m_selectedEntity);
        }
        if (isPressed(kb.focusSelected) && m_selectedEntity && ctx.scene.isAlive(m_selectedEntity)) {
            focusOnSelected(ctx);
        }

        // Gizmo mode shortcuts (only when camera NOT in fly mode)
        if (!m_cameraController->isLooking()) {
            if (isPressed(kb.gizmoTranslate))   m_gizmoOperation = GizmoOperation::Translate;
            if (isPressed(kb.gizmoRotate))      m_gizmoOperation = GizmoOperation::Rotate;
            if (isPressed(kb.gizmoScale))       m_gizmoOperation = GizmoOperation::Scale;
            if (isPressed(kb.gizmoToggleSpace)) {
                m_gizmoMode = (m_gizmoMode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
            }
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

        // Track panel edge positions for border-less resize detection
        ImVec2 panelAreaStart = ImGui::GetCursorScreenPos();

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
                drawTransformGizmo(ctx, vpMin, centerW, mainH);
                handleViewportPick(ctx, vpMin, centerW, mainH);
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

        // Panel border resize detection (no extra layout space needed)
        // Only allow starting a resize when nothing else is being dragged.
        {
            constexpr float RESIZE_ZONE = 4.0f;
            ImVec2 mpos = ImGui::GetMousePos();
            bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            bool alreadyResizing = m_resizingLeft || m_resizingRight || m_resizingBottom;
            // Block new resize if an ImGui widget or gizmo is active
            bool canStartNew = !ImGui::IsAnyItemActive() && !m_gizmo.isUsing() && !alreadyResizing;

            auto handleEdge = [&](bool show, float edgePos, bool horizontal,
                                  bool& resizingFlag, float& panelSize, float sign,
                                  float minSize, float maxSize) {
                if (!show) return;
                bool nearEdge;
                if (horizontal) {
                    nearEdge = std::abs(mpos.y - edgePos) <= RESIZE_ZONE
                            && mpos.x >= panelAreaStart.x
                            && mpos.x <= panelAreaStart.x + viewport->WorkSize.x;
                } else {
                    nearEdge = std::abs(mpos.x - edgePos) <= RESIZE_ZONE
                            && mpos.y >= panelAreaStart.y
                            && mpos.y <= panelAreaStart.y + mainH;
                }

                // Continue an existing resize drag
                if (resizingFlag) {
                    ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
                    float d = horizontal ? delta.y : delta.x;
                    panelSize += d * sign;
                    panelSize = std::clamp(panelSize, minSize, maxSize);
                    return;
                }

                // Show cursor hint when hovering (even if can't start)
                if (nearEdge) {
                    ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
                }

                // Only start a new resize on click when nothing else is active
                if (nearEdge && canStartNew && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    resizingFlag = true;
                }
            };

            handleEdge(m_showHierarchy, panelAreaStart.x + leftW, false,
                       m_resizingLeft, m_leftPanelWidth, 1.0f, 180.0f, 500.0f);
            handleEdge(m_showInspector, panelAreaStart.x + viewport->WorkSize.x - rightW, false,
                       m_resizingRight, m_rightPanelWidth, -1.0f, 240.0f, 600.0f);
            handleEdge(m_showBottom, panelAreaStart.y + mainH, true,
                       m_resizingBottom, m_bottomPanelHeight, -1.0f, 100.0f, 500.0f);

            if (!mouseDown) {
                m_resizingLeft = m_resizingRight = m_resizingBottom = false;
            }
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

    if (m_renderSystem && m_wireframe) m_renderSystem->setWireframe(true);
}

void EditorSystem::drawMenuBar(FrameContext& ctx) {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("View")) {
        char lbl[48];
        ImGui::MenuItem("Stats Overlay", getKeyBindLabel(m_keybinds.toggleStats, lbl, sizeof(lbl)), &m_showStats);
        ImGui::MenuItem("Hierarchy",     getKeyBindLabel(m_keybinds.toggleHierarchy, lbl, sizeof(lbl)), &m_showHierarchy);
        ImGui::MenuItem("Inspector",     getKeyBindLabel(m_keybinds.toggleInspector, lbl, sizeof(lbl)), &m_showInspector);
        ImGui::MenuItem("Bottom Panel",  getKeyBindLabel(m_keybinds.toggleBottom, lbl, sizeof(lbl)), &m_showBottom);
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
        char deselectLbl[48];
        if (ImGui::MenuItem("Deselect", getKeyBindLabel(m_keybinds.deselect, deselectLbl, sizeof(deselectLbl)))) {
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

    const auto& info = ctx.statistics.getFrameInfo();
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

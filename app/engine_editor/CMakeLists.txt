# ============================================================================
# engine_editor
# Engine + in-process editor (ImGui panels, gizmos, scene I/O dialogs).
# Built only when VKM_WITH_EDITOR=ON.
# ============================================================================
add_executable(engine_editor main.cpp)

target_link_libraries(engine_editor PRIVATE
    EngineApp
    EngineEditor
    BuildInfo
)

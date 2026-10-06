# The vkm_core test suite. Covers engine logic that is pure computation over a
# Scene, so the binary runs on a build machine with no GPU and no display. Built
# with the engine, because a suite that is opt-in is a suite nobody runs.
#
# A suite is tests/<area>/<suite>_tests.cpp, the area being the src folder it
# tests, each ending in the one entry point main calls. Globbed rather than
# listed, so adding one is a file and a row in suites.h - never an edit here
# that only the link step would notice was missing. tests/render/ is the GPU
# binary below, not part of this one.
file(GLOB VKM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/*_tests.cpp)
list(FILTER VKM_TEST_SOURCES EXCLUDE REGEX "/tests/render/[^/]+$")

# The src/editor files below depend on nothing else from it. PlaySnapshot is a
# scene, a resource manager and the serializers; the undo machinery is a scene
# and a CommandHost, the seam it asks the editor for rather than the editor's
# whole state; the editor's view is a System over an
# InputMap of its own. Compiled in here so each is tested without a window.
#
# Nothing else from src/editor belongs here. A file that needs ImGui is a file
# this binary cannot take, and that is the line: this one runs on a build
# machine with no GPU, no display and no graphics libraries.
#
# The cooker is the same case from src/tools: asset_cooker.cpp decides what a
# cook re-bakes and reaches the importers only through the AssetFactory table,
# so the suite compiles it alone and installs a stand-in importer, rather than
# linking the whole of vkm_cook. The cook key, the mesh processing, the LOD
# generation and the texture bake come with it, and with them meshoptimizer,
# the mip filter and the block encoders; so does the model parse, and with it
# cgltf, ufbx and MikkTSpace - plain CPU code with no dependencies of its own,
# so the binary still needs nothing installed.
add_executable(vkm_engine_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${VKM_TEST_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/editor/input/camera_controller_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/session/play_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/command/command_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/command/editor_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/command/prefab_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/cook/asset_cooker.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/cook/bc4_encoder.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/cook/cook_key.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/cook/lod_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/cook/mesh_processing.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/cook/texture_bake.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/import/model_source.cpp
)

# ImGui's headers only, for EditorState; nothing here calls into ImGui.
target_include_directories(vkm_engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/editor
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)

# vkm_tools as well as the engine, for bakeFontSDF: the UI suite needs real
# glyph metrics to assert what a line of text lays out as, and baking is how
# they are got without a GL context. It is the runtime-safe half of the tools -
# stb_truetype and the image decoders - so it pulls in no importer and this
# binary still runs on a machine with no graphics libraries at all.
#
# vkm_render for the same reason: it is the backend-neutral half of rendering -
# the view, the settings and the probe-grid repair a bake finishes on the CPU -
# and names no graphics library either. Bare, not vkmEngine::, because it is not
# an exported target: a project never links it (cmake/install.cmake says why).
# The backend is what this binary must not take, and that is a separate target.
target_link_libraries(vkm_engine_tests PRIVATE
    vkmEngine::vkm_core
    vkm_render
    vkm_tools
    meshoptimizer
    stb_image_resize
    bc7e
    cgltf
    ufbx
    mikktspace
    vkmEngine::vkm_warnings
)

# The transport suite opens a socket of its own, to send the zero-length
# datagram UdpSocket refuses to emit. vkm_core links ws2_32 PRIVATE - it is an
# implementation detail of its own sockets, not part of what it offers - so a
# caller that calls socket() itself asks for the library itself.
if(WIN32)
    target_link_libraries(vkm_engine_tests PRIVATE ws2_32)
    # Linux's 8 MB main-thread stack, not Windows' 1 MB: std::regex recurses once per character it
    # matches, and docs_tests.cpp matches whole source files.
    if(MSVC)
        target_link_options(vkm_engine_tests PRIVATE /STACK:8388608)
    else()
        target_link_options(vkm_engine_tests PRIVATE -Wl,--stack,8388608)
    endif()
endif()

# Where the shipped example projects are, so a test can open the scene of record
# and check the format still reads it. A packaged SDK has no examples/ - the
# test that uses this says so and passes rather than failing on a missing file.
target_compile_definitions(vkm_engine_tests PRIVATE
    VKM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    VKM_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs"
    VKM_ENGINE_DIR="${CMAKE_SOURCE_DIR}"
)

# No arguments runs every suite, which is what CI wants. Naming one or more
# runs just those, for the person working on it:
#     ./build/bin/vkm_engine_tests solver wire
#     ./build/bin/vkm_engine_tests --list
add_test(NAME vkm_engine COMMAND vkm_engine_tests)

# A real gameplay module for the script suite. ScriptModule loads a shared
# library, checks what it exports and drops what it registered before unmapping
# it - none of which can be exercised against anything but an actual module.
#
# Declared here rather than through vkm_add_gameplay_module: that helper names
# its output `game` and puts it in a project's bin/, which is the one shape this
# must not take. How it compiles and links is the helper's own
# vkm_gameplay_module_options, so the reload test unloads a module built the way
# a game's is. Its name is copied deliberately - MODULE, no debug postfix, and
# the empty prefix under Windows - so the file the loader is pointed at is named
# the way a shipped game's is: a libtest_module.dll where a real module is
# game.dll would make the suite skip itself on Windows.
#
# Built outside bin/, which is the engine's own: `vkm package` carries every
# library there into a game, and a test fixture is no part of one.
set(VKM_TEST_FIXTURE_DIR ${CMAKE_BINARY_DIR}/test_fixtures)
add_library(vkm_test_module MODULE ${CMAKE_CURRENT_SOURCE_DIR}/script/test_module.cpp)
vkm_gameplay_module_options(vkm_test_module)
target_include_directories(vkm_test_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(vkm_test_module PROPERTIES
    OUTPUT_NAME              test_module
    DEBUG_POSTFIX            ""
    LIBRARY_OUTPUT_DIRECTORY ${VKM_TEST_FIXTURE_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${VKM_TEST_FIXTURE_DIR}
)
if(WIN32)
    set_target_properties(vkm_test_module PROPERTIES PREFIX "")
endif()
add_dependencies(vkm_engine_tests vkm_test_module)
target_compile_definitions(vkm_engine_tests PRIVATE VKM_TEST_MODULE_DIR="${VKM_TEST_FIXTURE_DIR}")

# The project tool, run as a user runs it (tests/tools/vkm_tool.cmake). Only for
# the build tree at build/, the one the tool finds the engine in.
find_package(Python3 3.8 COMPONENTS Interpreter QUIET)
file(REAL_PATH "${CMAKE_SOURCE_DIR}/build" _vkm_tool_build)
file(REAL_PATH "${CMAKE_BINARY_DIR}" _vkm_this_build)
if(Python3_Interpreter_FOUND AND _vkm_this_build STREQUAL _vkm_tool_build)
    add_test(NAME vkm_tool
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${Python3_EXECUTABLE}
            -DVKM=${CMAKE_SOURCE_DIR}/tools/vkm
            -DWORK=${VKM_TEST_FIXTURE_DIR}/vkm_tool
            -DEXE=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/vkm_tool.cmake
    )
endif()

# The GPU suite, built only where EGL can be found.
#
# Separate from vkm_engine_tests on purpose: that binary must stay runnable on a
# machine with no GPU and no graphics libraries at all, and linking EGL into it
# would end that. This one links EGL, opens the device directly, and skips at
# runtime when there is no device to open - so it is green on a build machine and
# useful on a workstation.
find_package(OpenGL QUIET COMPONENTS EGL)
if(TARGET OpenGL::EGL)
    add_executable(vkm_render_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/render/render_tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/render/golden_tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/render/egl_context.cpp
    )
    target_include_directories(vkm_render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/render)

    # vkm_backend_gl as well as vkm_gl: the suite compiles the shipped shaders
    # with the backend's own prelude, which is GLBackend's to build. Nothing here
    # opens a window - the EGL context above is the whole GL surface.
    target_link_libraries(vkm_render_tests PRIVATE vkm_backend_gl vkm_gl vkm_log OpenGL::EGL)

    # Where the shaders are. The loader resolves their paths against the working
    # directory, which the suite sets to this root before it loads any.
    target_compile_definitions(vkm_render_tests PRIVATE VKM_ENGINE_ROOT="${CMAKE_SOURCE_DIR}")
    set_target_properties(vkm_render_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME render COMMAND vkm_render_tests)
    message(STATUS "EGL found - the GPU test suite (vkm_render_tests) is built")
else()
    message(STATUS "EGL not found - the GPU test suite is skipped")
endif()

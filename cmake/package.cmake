# CPack - turning the install tree into an archive somebody can download.
#
#     cmake --build build --target package
#
# Two components, and the split is the useful part: Runtime is what a player
# needs to run a game, Development is what a developer needs to build one.
# A packaged game ships Runtime only, which is why the split exists at all.

set(CPACK_PACKAGE_NAME              "vkmEngine")
set(CPACK_PACKAGE_VENDOR            "vkm")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A C++17 OpenGL game engine: editor, runtime and asset cooker")
set(CPACK_PACKAGE_FILE_NAME
    "vkmEngine-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# One directory inside the archive, so unpacking never scatters files into
# whatever the user happened to be standing in.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "vkmEngine-${PROJECT_VERSION}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TXZ)
endif()

# The engine is not ABI-stable across compilers, so the archive name says which
# one built it. Without this, two builds of the same version are indistinguishable
# and the toolchain pin's error message is the first anyone learns of the
# difference - after downloading the wrong one.
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_FILE_NAME}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

set(CPACK_COMPONENTS_ALL Runtime Development)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME     "Engine and hosts")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION
    "The three hosts, the shared engine libraries and the shaders they load.")
set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "SDK")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION
    "Headers, CMake package and project template for building a game.")
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)

# One archive containing both components: an SDK is not useful in halves.
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

set(CPACK_RESOURCE_FILE_README  "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/" "/build.*/" "/logs/" "/cooked/" "/library/" "/scenes/"
    "\\\\.user$" "/dist/")

include(CPack)

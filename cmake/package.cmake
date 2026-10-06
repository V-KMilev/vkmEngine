# CPack: the whole install as one archive (`cmake --build build --target package`).

set(CPACK_PACKAGE_NAME                "vkmEngine")
set(CPACK_PACKAGE_VENDOR              "vkm")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A C++17 OpenGL game engine: editor, runtime and asset cooker")

# Named as `vkm package` names a game's (platform_tag in tools/vkmcli/shell.py), e.g.
# vkmEngine-1.0.0-linux-x64; unpacks into one folder of that name. The compiler is
# not in it: an SDK builds games with the one it pins.
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _vkm_system)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _vkm_machine)
if(_vkm_machine MATCHES "^(x86_64|amd64)$")
    set(_vkm_machine x64)
elseif(_vkm_machine STREQUAL "aarch64")
    set(_vkm_machine arm64)
endif()
set(CPACK_PACKAGE_FILE_NAME "vkmEngine-${PROJECT_VERSION}-${_vkm_system}-${_vkm_machine}")

if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TXZ)
endif()

# `package_source`: the tree less what the tools write into it.
set(CPACK_SOURCE_GENERATOR TXZ)
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git"
    "/build[^/]*/"
    "/examples/[^/]+/(bin|cooked|logs|dist)/"
    "/__pycache__/"
)

include(CPack)

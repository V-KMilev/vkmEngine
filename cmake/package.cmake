# CPack: the whole install as one archive (`cmake --build build --target package`).

set(CPACK_PACKAGE_NAME                "vkmEngine")
set(CPACK_PACKAGE_VENDOR              "vkm")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A C++17 OpenGL game engine: editor, runtime and asset cooker")

# Named for what it runs on, as `vkm package` names a game's (platform_tag in
# tools/vkmcli/shell.py), and for the compiler a game builds its code with against it:
# vkmEngine-1.0.0-linux-x64-gcc. Unpacks into one folder of that name.
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _vkm_system)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _vkm_machine)
if(_vkm_machine MATCHES "^(x86_64|amd64)$")
    set(_vkm_machine x64)
elseif(_vkm_machine STREQUAL "aarch64")
    set(_vkm_machine arm64)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_vkm_compiler gcc)
else()
    string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" _vkm_compiler)
endif()
set(CPACK_PACKAGE_FILE_NAME "vkmEngine-${PROJECT_VERSION}-${_vkm_system}-${_vkm_machine}-${_vkm_compiler}")

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

# CPack: the whole install as one archive (`cmake --build build --target package`).

set(CPACK_PACKAGE_NAME                "vkmEngine")
set(CPACK_PACKAGE_VENDOR              "vkm")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A C++17 OpenGL game engine: editor, runtime and asset cooker")

# Named for the compiler too, which a project must match; unpacks into one folder.
string(JOIN "-" CPACK_PACKAGE_FILE_NAME
    vkmEngine
    ${PROJECT_VERSION}
    ${CMAKE_SYSTEM_NAME}
    ${CMAKE_SYSTEM_PROCESSOR}
    ${CMAKE_CXX_COMPILER_ID}
    ${CMAKE_CXX_COMPILER_VERSION}
)

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

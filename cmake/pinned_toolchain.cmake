# The toolchain a configure takes when it names no compiler (CMakeLists.txt): the GCC or
# Clang tools/toolchain.json pins, as VKM_COMPILER says, fetched by `vkm toolchain`, so
# the engine builds here with what builds a release.

# Read again for every try_compile, in a project that has none of this configure's
# variables but these; with the folder known, vkm is not asked again.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES VKM_COMPILER VKM_PINNED_BIN)

if(VKM_COMPILER STREQUAL "gcc")
    set(_vkm_drivers gcc g++)
elseif(VKM_COMPILER STREQUAL "clang")
    set(_vkm_drivers clang clang++)
else()
    message(FATAL_ERROR "VKM_COMPILER is gcc or clang, not '${VKM_COMPILER}'")
endif()

if(NOT VKM_PINNED_BIN)
    # The first Python that runs vkm; Windows' Store stub answers to the name and runs nothing.
    set(_vkm_python "")
    foreach(_vkm_name python3 python)
        find_program(_vkm_found NAMES ${_vkm_name} NO_CACHE)
        if(_vkm_found)
            execute_process(
                COMMAND "${_vkm_found}" -c "import sys; sys.exit(sys.version_info < (3, 8))"
                RESULT_VARIABLE _vkm_too_old
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(_vkm_too_old EQUAL 0)
                set(_vkm_python "${_vkm_found}")
                break()
            endif()
        endif()
        unset(_vkm_found)
    endforeach()
    if(NOT _vkm_python)
        message(FATAL_ERROR
            "Building with a pinned compiler needs Python 3.8 or newer, to fetch it. Install one, "
            "or name a compiler: -DCMAKE_CXX_COMPILER=..., or -DVKM_PINNED_TOOLCHAIN=OFF."
        )
    endif()

    execute_process(
        COMMAND "${_vkm_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/vkm.py" toolchain ${VKM_COMPILER} --path
        OUTPUT_VARIABLE VKM_PINNED_BIN
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _vkm_failed
    )
    if(_vkm_failed)
        message(FATAL_ERROR "`vkm toolchain ${VKM_COMPILER}` fetched no compiler; its output above says why")
    endif()
    file(TO_CMAKE_PATH "${VKM_PINNED_BIN}" VKM_PINNED_BIN)

    # Recorded in the engine's package, so vkm builds modules with the same pinned compiler.
    set(VKM_PINNED_TOOLCHAIN_USED ON CACHE INTERNAL "The engine is built with a pinned compiler")
endif()

set(_vkm_exe "")
if(CMAKE_HOST_WIN32)
    set(_vkm_exe ".exe")
endif()
list(GET _vkm_drivers 0 _vkm_c)
list(GET _vkm_drivers 1 _vkm_cxx)
set(CMAKE_C_COMPILER "${VKM_PINNED_BIN}/${_vkm_c}${_vkm_exe}")
set(CMAKE_CXX_COMPILER "${VKM_PINNED_BIN}/${_vkm_cxx}${_vkm_exe}")

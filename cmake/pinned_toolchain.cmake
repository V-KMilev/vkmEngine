# The toolchain a configure takes when it names no compiler (CMakeLists.txt): the GCC
# tools/toolchain.json pins, fetched by `vkm toolchain`, so the engine builds here with
# what builds a release. Read again for every try_compile, whose compilers are set.
if(CMAKE_CXX_COMPILER)
    return()
endif()

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
        "Building with the pinned GCC needs Python 3.8 or newer, to fetch it. Install one, "
        "or name a compiler: -DCMAKE_CXX_COMPILER=..., or -DVKM_PINNED_TOOLCHAIN=OFF."
    )
endif()

execute_process(
    COMMAND "${_vkm_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/vkm.py" toolchain gcc --path
    OUTPUT_VARIABLE _vkm_gcc_bin
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _vkm_failed
)
if(_vkm_failed)
    message(FATAL_ERROR "`vkm toolchain gcc` did not fetch the pinned GCC; its output above says why")
endif()
file(TO_CMAKE_PATH "${_vkm_gcc_bin}" _vkm_gcc_bin)

set(_vkm_exe "")
if(CMAKE_HOST_WIN32)
    set(_vkm_exe ".exe")
endif()
set(CMAKE_C_COMPILER "${_vkm_gcc_bin}/gcc${_vkm_exe}")
set(CMAKE_CXX_COMPILER "${_vkm_gcc_bin}/g++${_vkm_exe}")

# Recorded in the engine's package, so vkm builds modules with the same pinned GCC.
set(VKM_PINNED_TOOLCHAIN_USED ON CACHE INTERNAL "The engine is built with the pinned GCC")

# No object calls into Tracy (cmake -P), for a tree that asked for no profiler:
#
#   -DOBJECTS=<a|b|...>  the objects to read
#   -DNM=<nm>            the toolchain's nm

string(REPLACE "|" ";" _objects "${OBJECTS}")
set(_calling "")
foreach(_object IN LISTS _objects)
    execute_process(COMMAND "${NM}" -C -u "${_object}" OUTPUT_VARIABLE _undefined RESULT_VARIABLE _status)
    if(NOT _status EQUAL 0)
        message(FATAL_ERROR "${NM} -C -u ${_object} failed")
    endif()
    if(_undefined MATCHES "tracy::")
        list(APPEND _calling "${_object}")
    endif()
endforeach()

if(_calling)
    string(REPLACE ";" "\n  " _calling "${_calling}")
    message(FATAL_ERROR "No profiler was asked for, yet these call into Tracy:\n  ${_calling}")
endif()

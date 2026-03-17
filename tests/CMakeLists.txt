add_executable(test_thread_pool
    test_thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/platform/threading/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/platform/threading/steal_thread.cpp
)

target_include_directories(test_thread_pool PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)

target_link_libraries(test_thread_pool PRIVATE pthread glm::glm)

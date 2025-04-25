cmake_minimum_required(VERSION 3.16)
project(RDMA_lib C CXX)

# Global compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-pointer-arith -Wno-conversion -O3")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
  ${PROJECT_SOURCE_DIR}/include
)

# Dependencies
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCONFIG REQUIRED libconfig)
pkg_check_modules(IBVERBS REQUIRED libibverbs)
# pkg_check_modules(DPDK libdpdk) # Optional

# include_directories(${LIBCONFIG_INCLUDE_DIRS} ${IBVERBS_INCLUDE_DIRS})
# link_directories(${LIBCONFIG_LIBRARY_DIRS} ${IBVERBS_LIBRARY_DIRS})

# Source files
set(RDMA_C_SRC
  src/c/ib.c
  src/c/mr.c
  src/c/qp.c
  utils/log.c
  utils/sock.c
  utils/utils.c
)

add_library(RDMA_lib STATIC ${RDMA_C_SRC})
target_link_libraries(RDMA_lib PUBLIC ${IBVERBS_LIBRARIES} ${LIBCONFIG_LIBRARIES})
target_include_directories(RDMA_lib PRIVATE ${LIBCONFIG_LIBRARY_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/utils)
target_include_directories(RDMA_lib PUBLIC ${IBVERBS_LIBRARY_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include/c)

set_target_properties(RDMA_lib PROPERTIES OUTPUT_NAME "RDMA_lib")

add_custom_command(TARGET RDMA_lib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:RDMA_lib>
        ${PROJECT_SOURCE_DIR}/libRDMA_lib.a
    COMMENT "Copying RDMA_lib.a to project root"
)

file(GLOB RDMA_CPP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/*.cpp)
add_library(RDMA_lib_cpp STATIC ${RDMA_CPP_SRC} )
target_link_libraries(RDMA_lib_cpp PUBLIC RDMA_lib)
target_include_directories(RDMA_lib_cpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/cpp)

add_custom_command(TARGET RDMA_lib_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:RDMA_lib_cpp>
        ${PROJECT_SOURCE_DIR}/libRDMA_lib_cpp.a
    COMMENT "Copying RDMA_lib_cpp.a to project root"
)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)

add_executable(${CMAKE_PROJECT_NAME}_unit_tests ${TEST_SOURCES})
# gtest_main have the main function so no need for main function in tests
# use the fmt-header-only target to avoid link error
target_link_libraries(${CMAKE_PROJECT_NAME}_unit_tests PRIVATE GTest::gtest_main RDMA_lib_cpp )
# target_include_directories(${CMAKE_PROJECT_NAME}_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/cpp ${CMAKE_SOURCE_DIR}/include/c)

include(GoogleTest)
gtest_discover_tests(${CMAKE_PROJECT_NAME}_unit_tests)


add_executable(ping_pong ${CMAKE_CURRENT_SOURCE_DIR}/examples/ping_pong.c)
target_link_libraries(ping_pong PRIVATE RDMA_lib)
target_include_directories(ping_pong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/utils/)


add_executable(ping_pong_cmplt_cnl ${CMAKE_CURRENT_SOURCE_DIR}/examples/ping_pong_cmplt_cnl.c)
target_link_libraries(ping_pong_cmplt_cnl PRIVATE RDMA_lib)
target_include_directories(ping_pong_cmplt_cnl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/utils/)
# Binaries
# function(add_rdma_executable name)
#   add_executable(${name} ${ARGN} ${RDMA_SRC})
#   target_link_libraries(${name}
#     RDMA_lib
#     ${LIBCONFIG_LIBRARIES}
#     ${DPDK_LIBRARIES}
#     Threads::Threads
#     ${IBVERBS_LIBRARIES}
#   )
# endfunction()
#
# add_rdma_executable(rdma-bench
#   perf/rdma-bench.c
#   perf/rdma-bench_cfg.c
#   perf/client.c
#   perf/server.c
#   perf/setup_ib.c
# )
#
# add_rdma_executable(rc_connection
#   examples/rc_connection.c
#   examples/bitmap.c
#   examples/memory_management.c
# )
#
# add_rdma_executable(sg_list examples/sg_list.c)
# add_rdma_executable(sg_list_perf perf/sg_list_perf.c)
#
# add_rdma_executable(test_bitmap
#   examples/test_bitmap.c
#   examples/bitmap.c
# )
#
# add_rdma_executable(ping_pong examples/ping_pong.c)
# add_rdma_executable(ping_pong_cmplt_cnl examples/ping_pong_cmplt_cnl.c)
# add_rdma_executable(test_socket examples/test_socket.c)
# add_rdma_executable(test_config test/unity/unity.c test/test_config.c)
#
# # Install targets
# install(TARGETS RDMA_lib
#   ARCHIVE DESTINATION lib
#   LIBRARY DESTINATION lib
#   RUNTIME DESTINATION bin
# )
#
# install(DIRECTORY include/ DESTINATION include)
#
# # Optional: custom copy target (if you still want to copy lib to root dir)
# add_custom_command(TARGET RDMA_lib POST_BUILD
#   COMMAND ${CMAKE_COMMAND} -E copy
#   $<TARGET_FILE:RDMA_lib> ${CMAKE_SOURCE_DIR}/libRDMA_lib.a
# )
#

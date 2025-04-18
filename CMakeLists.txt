cmake_minimum_required(VERSION 3.16)
project(RDMA_lib C)

# Global compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-pointer-arith -Wno-conversion -O3")

# Include directories
include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/test/unity
  ${PROJECT_SOURCE_DIR}/perf
  ${PROJECT_SOURCE_DIR}/utils
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
set(RDMA_SRC
  src/ib.c
  src/mr.c
  src/qp.c
  utils/log.c
  utils/sock.c
  utils/utils.c
)

# Static library
add_library(RDMA_lib STATIC ${RDMA_SRC})
target_link_libraries(RDMA_lib PUBLIC ${IBVERBS_LIBRARIES} ${LIBCONFIG_LIBRARIES})
target_include_directories(RDMA_lib PUBLIC ${LIBCONFIG_LIBRARY_DIRS} ${IBVERBS_LIBRARY_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(RDMA_lib PROPERTIES OUTPUT_NAME "RDMA_lib")

add_custom_command(TARGET RDMA_lib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:RDMA_lib>
        ${PROJECT_SOURCE_DIR}/libRDMA_lib.a
    COMMENT "Copying RDMA_lib.a to project root"
)

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

project('RDMA_lib', 'c',
  default_options: ['warning_level=3', 'optimization=3']
)
#
add_project_arguments(['-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-pointer-arith',  '-Wno-conversion'], language: 'c')
# Detect dependencies
libconfig_dep = dependency('libconfig', required: true)
libdpdk_dep = dependency('libdpdk', required: false)
ibverbs_dep = dependency('libibverbs', required: true)
thread_dep = dependency('threads', required: true)

root_dir = meson.project_source_root()

# Compiler and linker flags
#cflags = ['-Wall', '-Werror', '-O3', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-pointer-arith',  '-Wno-conversion']
cflags = []
if libdpdk_dep.found()
  #cflags += libdpdk_dep.get_compile_args()
  #ldflags = libdpdk_dep.get_link_args()
endif

inc_dirs = include_directories('.', 'test/unity', 'perf', 'include', 'utils')


# Directories
test_dir = 'test'
perf_dir = 'perf'
unity_dir = 'test/unity'
example_dir = 'examples'
utils_dir = 'utils'
bin_dir = 'bin'

# Source files
src_files = files('ib.c', 
  'mr.c',
  'qp.c',
  'utils/log.c',
  'utils/sock.c',
  'utils/utils.c')
unity_files = files(join_paths(unity_dir, 'unity.c'))

# Programs
bin_programs = [
  ['rdma-bench', files(join_paths(perf_dir, 'rdma-bench.c'), join_paths(perf_dir, 'rdma-bench_cfg.c'),
                      join_paths(perf_dir, 'client.c'), join_paths(perf_dir, 'server.c'), join_paths(perf_dir, 'setup_ib.c'))],
  ['rc_connection', files(join_paths(example_dir, 'rc_connection.c'), join_paths(example_dir, 'bitmap.c'), 
                          join_paths(example_dir, 'memory_management.c'))],
  ['sg_list', files(join_paths(example_dir, 'sg_list.c'))],
  ['sg_list_perf', files(join_paths(perf_dir, 'sg_list_perf.c'))],
  ['test_bitmap', files(join_paths(example_dir, 'test_bitmap.c'), join_paths(example_dir, 'bitmap.c'))],
  ['ping_pong', files(join_paths(example_dir, 'ping_pong.c'))],
  ['ping_pong_cmplt_cnl', files(join_paths(example_dir, 'ping_pong_cmplt_cnl.c'))],
  ['test_socket', files(join_paths(example_dir, 'test_socket.c'))],
  ['test_config', files('test/unity/unity.c', 'test/test_config.c')]
]

libRDMA_lib = static_library(
  'RDMA_lib',  # Name of the library
  src_files,     # Source files
  include_directories: inc_dirs,  # Include root directory
  c_args: ['-Wall', '-Werror'],       # Additional compile flags
  dependencies: [ibverbs_dep],
  install: true                       # Whether to install the library
)

copy_lib = custom_target(
    'copy_static_library',
    output: 'libRDMA_lib',        # Output filename in the root directory
    input: libRDMA_lib,                # The built static library
    command: ['cp', '@INPUT@', root_dir],
    depends: [libRDMA_lib],              # Ensure this depends on the static library
    build_by_default: true           # Run during the default build process
)


foreach prog : bin_programs
  executable(prog[0],
    sources: prog[1] + src_files,
    include_directories: inc_dirs,
    c_args: cflags,
    dependencies: [libconfig_dep, libdpdk_dep, thread_dep, ibverbs_dep],
    install: true
  )
endforeach




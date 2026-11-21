cmake_minimum_required(VERSION 3.18)

project(df-explorer LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)

# Aztec S2 Poseidon2 explorer (default). Classic MiMC sources remain in tree unused.
# Prefer local cooperative_groups shim so CUDA 13 libcu++ is not pulled into CGBN
# (that breaks nvcc stub generation with ::cuda::std::iter_swap).
include_directories(${CMAKE_CURRENT_LIST_DIR}/thirdparty/cg_shim)
include_directories(${CMAKE_CURRENT_LIST_DIR}/thirdparty)
include_directories(${CMAKE_CURRENT_LIST_DIR}/thirdparty/CGBN/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)

find_package(workflow CONFIG HINTS thirdparty/workflow REQUIRED)
include_directories(${WORKFLOW_INCLUDE_DIR})
link_directories(${WORKFLOW_LIB_DIR})

# Prefer conda / explicit GMP if provided
if(DEFINED ENV{CONDA_PREFIX})
  include_directories($ENV{CONDA_PREFIX}/include)
  link_directories($ENV{CONDA_PREFIX}/lib)
endif()

set(SRC_FILE
        src/server.cc
        src/darkforest.cc
        src/poseidon2_cpu.cc
        src/poseidon2.cu)

add_executable(df-explorer ${SRC_FILE})

set_target_properties(df-explorer PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF
        CUDA_ARCHITECTURES "86")

# Ubuntu 24.04 often has no libpthread.so symlink; prefer -pthread and system libdir.
link_directories(/usr/lib/x86_64-linux-gnu)
target_link_libraries(df-explorer
        gmp
        gmpxx
        cudart
        workflow
        pthread
        ssl
        crypto
        )
target_link_options(df-explorer PRIVATE -pthread)

add_subdirectory(samples)

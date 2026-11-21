cmake_minimum_required(VERSION 3.18)

project(df-explorer-standalone LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/../thirdparty/cg_shim)
include_directories(${PROJECT_SOURCE_DIR}/../src)
include_directories(${PROJECT_SOURCE_DIR}/../thirdparty)
include_directories(${PROJECT_SOURCE_DIR}/../thirdparty/CGBN/include)

if(DEFINED ENV{CONDA_PREFIX})
  include_directories($ENV{CONDA_PREFIX}/include)
  link_directories($ENV{CONDA_PREFIX}/lib)
endif()

set(TEST_SRC_FILE
        standalone.cc
        ${PROJECT_SOURCE_DIR}/../src/darkforest.cc
        ${PROJECT_SOURCE_DIR}/../src/poseidon2_cpu.cc
        ${PROJECT_SOURCE_DIR}/../src/poseidon2.cu)

add_executable(standalone ${TEST_SRC_FILE})

set_target_properties(standalone PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF
        CUDA_ARCHITECTURES "86")

target_link_libraries(standalone gmp gmpxx cudart)

set(EQ_SRC_FILE
        cpu_gpu_eq.cc
        ${PROJECT_SOURCE_DIR}/../src/darkforest.cc
        ${PROJECT_SOURCE_DIR}/../src/poseidon2_cpu.cc
        ${PROJECT_SOURCE_DIR}/../src/poseidon2.cu)

add_executable(cpu_gpu_eq ${EQ_SRC_FILE})

set_target_properties(cpu_gpu_eq PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF
        CUDA_ARCHITECTURES "86")

target_link_libraries(cpu_gpu_eq gmp gmpxx cudart)

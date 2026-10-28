cmake_minimum_required(VERSION 3.22)
project(nextqt VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NQ_BUILD_TESTS "Build tests" ON)
option(NQ_BUILD_EXAMPLES "Build examples" ON)
option(NQ_WERROR "Treat warnings as errors" OFF)
option(NQ_GOLDEN "Deterministic raster build (golden)" OFF)
# 端口选择（M0 只实现 headless；其余在后续里程碑接入）
set(NQ_PLATFORM "none" CACHE STRING "Platform backend: none|win32|cocoa|x11|wayland")
set(NQ_GPU "none" CACHE STRING "GPU backend: none|vk|mtl|d3d12|gles|webgpu")

message(STATUS "NextQt ${PROJECT_VERSION}: platform=${NQ_PLATFORM} gpu=${NQ_GPU} "
               "golden=${NQ_GOLDEN} tests=${NQ_BUILD_TESTS}")

add_library(nq_base STATIC
  src/base/arena.cpp
  src/base/diag.cpp
)
target_include_directories(nq_base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
nq_engine_flags(nq_base)

add_library(nq_dl STATIC src/dl/display_list.cpp)
target_link_libraries(nq_dl PUBLIC nq_base)
nq_engine_flags(nq_dl)

add_library(nq_raster STATIC
  src/raster/surface.cpp
  src/raster/raster.cpp
)
target_link_libraries(nq_raster PUBLIC nq_dl)
nq_engine_flags(nq_raster)

if(NQ_GOLDEN AND NOT MSVC)
  # 确定性光栅：禁止浮点收缩，保证跨机器 golden 可比
  target_compile_options(nq_raster PRIVATE -ffp-contract=off)
endif()

if(NQ_BUILD_EXAMPLES)
  add_executable(nq_hello examples/hello_card.cpp)
  target_link_libraries(nq_hello PRIVATE nq_raster)
  nq_engine_flags(nq_hello)
endif()

if(NQ_BUILD_TESTS)
  add_library(nqtest STATIC tools/nqtest/nqtest.cpp)
  target_include_directories(nqtest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tools/nqtest")
  nq_engine_flags(nqtest)

  enable_testing()
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")

  foreach(test_name base dl raster golden)
    add_executable(test_${test_name} tests/test_${test_name}.cpp)
    target_link_libraries(test_${test_name} PRIVATE nq_raster nqtest)
    target_include_directories(test_${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_definitions(test_${test_name} PRIVATE
      NQ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
      NQ_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
    nq_engine_flags(test_${test_name})
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
endif()

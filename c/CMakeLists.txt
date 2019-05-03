cmake_minimum_required(VERSION 2.8.5)
project(clox CXX)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set(CMAKE_VERBOSE_MAKEFILE ON)

find_library(CB_LIB cb)
find_path(CB_INCLUDE "cb.h")
find_path(XXHASH_INCLUDE "external/xxhash.h")

option(COVERAGE "Build with test coverage" OFF)

set(CLOX_SOURCES
  "${CMAKE_SOURCE_DIR}/cb_integration.cpp"
  "${CMAKE_SOURCE_DIR}/chunk.cpp"
  "${CMAKE_SOURCE_DIR}/compiler.cpp"
  "${CMAKE_SOURCE_DIR}/debug.cpp"
  "${CMAKE_SOURCE_DIR}/main.cpp"
  "${CMAKE_SOURCE_DIR}/memory.cpp"
  "${CMAKE_SOURCE_DIR}/object.cpp"
  "${CMAKE_SOURCE_DIR}/scanner.cpp"
  "${CMAKE_SOURCE_DIR}/table.cpp"
  "${CMAKE_SOURCE_DIR}/value.cpp"
  "${CMAKE_SOURCE_DIR}/vm.cpp"
)

include_directories(
  "${CMAKE_SOURCE_DIR}"
  "${CB_INCLUDE}"
  "${XXHASH_INCLUDE}"
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNAN_TAGGING -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -DDEBUG_PRINT_CODE -DDEBUG_STRESS_GC -DDEBUG_TRACE_EXECUTION -DDEBUG_TRACE_GC")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=native")

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -mtune=native")

set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -mtune=native")

set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_COVERAGE} -O0 -g -fprofile-arcs -ftest-coverage")
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE "${CMAKE_EXE_LINKER_FLAGS_COVERAGE} -fprofile-arcs -ftest-coverage")

add_executable(clox ${CLOX_SOURCES})
target_link_libraries(clox "${CB_LIB}")


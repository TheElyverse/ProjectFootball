cmake_minimum_required(VERSION 3.24)

project(ProjectFootball
  VERSION 0.1.0
  DESCRIPTION "Elyverse: Football -- headless simulation core"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(PF_BUILD_TESTS "Build unit tests" ON)
option(PF_ENABLE_SANITIZERS "Build with address/UB sanitizers (GCC/Clang only)" OFF)
option(PF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake")

if(PF_ENABLE_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(libs/sim-core)
add_subdirectory(apps/sim-cli)

if(PF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()

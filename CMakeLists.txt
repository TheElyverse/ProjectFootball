cmake_minimum_required(VERSION 3.25)

project(ProjectFootball
        VERSION 0.1.0
        DESCRIPTION "Elyverse: Football -- headless simulation core"
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif ()

option(PF_BUILD_TESTS "Build unit tests" ON)
option(PF_ENABLE_SANITIZERS "Build with address/UB sanitizers (GCC/Clang only)" OFF)
option(PF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake")

# Deterministic floating point: every multiply and add rounds on its own.
# Compilers may otherwise fuse a * b + c into one multiply-add where the
# target has the instruction -- Clang does by default, GCC outside ISO C --
# which changes the last bit and with it replays between platforms. MSVC
# fuses only with /fp:contract; /fp:precise is its default and stated here.
if (MSVC)
    add_compile_options(/fp:precise)
else ()
    add_compile_options(-ffp-contract=off)
endif ()

if (PF_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif ()

# JSON for data files (tactics) and replays; shared by several libraries, so
# it is declared once here.
CPMAddPackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY nlohmann/json
        GIT_TAG v3.12.0
        OPTIONS
        "JSON_BuildTests OFF"
        "JSON_Install OFF"
)

add_subdirectory(libs/sim-core)
add_subdirectory(libs/sim-tactics)
add_subdirectory(libs/sim-match)
add_subdirectory(libs/sim-replay)
add_subdirectory(apps/sim-cli)

if (PF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

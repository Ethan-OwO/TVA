cmake_minimum_required(VERSION 3.20)
project(tva VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(TVA_SANITIZE "Build everything with ASan + UBSan" OFF)
option(TVA_BUILD_TESTS "Build unit and integration tests" ON)

# Sanitizers apply to every target (including fetched dependencies) so that
# instrumented and uninstrumented STL code never mix. -fno-sanitize-recover
# turns any UBSan report into a hard failure instead of a log line.
if(TVA_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

include(cmake/dependencies.cmake)
find_package(ZLIB REQUIRED)

# Warnings only for our own code, not for fetched dependencies.
add_library(tva_warnings INTERFACE)
target_compile_options(tva_warnings INTERFACE
  -Wall -Wextra -Wpedantic $<$<CONFIG:Debug>:-Werror>)

add_library(tva_core STATIC
  src/core/checkout.cpp
  src/core/commit.cpp
  src/core/config.cpp
  src/core/ignore.cpp
  src/core/materialize.cpp
  src/core/object.cpp
  src/core/object_database.cpp
  src/core/object_id.cpp
  src/core/pr.cpp
  src/core/refs.cpp
  src/core/repository.cpp
  src/core/rev_parse.cpp
  src/core/sha1.cpp
  src/core/snapshot.cpp
  src/core/trailers.cpp
  src/core/tree.cpp
  src/core/tree_diff.cpp
  src/core/zlib_codec.cpp
  src/platform/clock_posix.cpp
  src/platform/fs_posix.cpp
)
target_include_directories(tva_core PUBLIC src)
target_link_libraries(tva_core PUBLIC ZLIB::ZLIB PRIVATE tva_warnings)

add_executable(tva
  src/main.cpp
  src/cli/cli.cpp
  src/cli/cmd_cat_file.cpp
  src/cli/cmd_commit_tree.cpp
  src/cli/cmd_hash_object.cpp
  src/cli/cmd_init.cpp
  src/cli/cmd_log.cpp
  src/cli/cmd_pr.cpp
  src/cli/cmd_restore.cpp
  src/cli/cmd_rev_parse.cpp
  src/cli/cmd_snapshot.cpp
  src/cli/cmd_status.cpp
  src/cli/cmd_write_tree.cpp
)
target_compile_definitions(tva PRIVATE TVA_VERSION="${PROJECT_VERSION}")
target_link_libraries(tva PRIVATE tva_core CLI11::CLI11 tva_warnings)

if(TVA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

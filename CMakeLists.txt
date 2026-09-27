cmake_minimum_required(VERSION 3.20)
project(fuse_vault_v2 LANGUAGES C)
set(FV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
# CTest must be enabled at the build root to discover host subdirectory tests.
option(FV_BUILD_TESTS "Build desktop tests and benchmark" ON)
if(FV_BUILD_TESTS)
  enable_testing()
endif()
add_subdirectory(src/host)

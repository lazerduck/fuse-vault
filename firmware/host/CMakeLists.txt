cmake_minimum_required(VERSION 3.15)

project(fuse_vault_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(fuse_vault_core STATIC
    ../src/app.c
)

target_include_directories(fuse_vault_core PUBLIC
    ../include
)

target_compile_options(fuse_vault_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

add_executable(fuse_vault_simulator
    simulator.c
)
target_link_libraries(fuse_vault_simulator PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_simulator PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

enable_testing()
add_executable(fuse_vault_tests
    tests.c
)
target_link_libraries(fuse_vault_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_state_machine COMMAND fuse_vault_tests)


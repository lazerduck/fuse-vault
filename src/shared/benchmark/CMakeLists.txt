add_library(fv_benchmark STATIC protocol.c engine.c)
target_include_directories(fv_benchmark PUBLIC include ../storage/include)
target_link_libraries(fv_benchmark PUBLIC fv_storage)
target_compile_options(fv_benchmark PRIVATE -Wall -Wextra -Werror)

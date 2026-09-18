add_library(fv_storage STATIC auth_store.c volume_format.c)
target_include_directories(fv_storage PUBLIC include)
target_link_libraries(fv_storage PUBLIC fv_crypto)
target_compile_options(fv_storage PRIVATE -Wall -Wextra -Werror)

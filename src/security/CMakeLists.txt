add_library(fv_security STATIC policy.c key_derivation.c key_wrap.c envelope.c vault.c random.c journal.c enrollment.c
  ../../third_party/nettle_keywrap/keywrap.c)
target_include_directories(fv_security PUBLIC include PRIVATE ../../third_party/nettle_keywrap)
target_link_libraries(fv_security PUBLIC fv_storage)
target_compile_options(fv_security PRIVATE -Wall -Wextra -Werror)

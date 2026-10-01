add_library(fv_ui STATIC
  device_ui.c startup.c scene.c drawing.c credential_entry.c button_input.c
  widgets/menu.c
  scenes/dashboard.c scenes/home.c scenes/settings.c scenes/credential.c scenes/setup.c
  scenes/erase.c scenes/fido.c scenes/passkeys.c scenes/status.c)
target_include_directories(fv_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(fv_ui PRIVATE -Wall -Wextra -Werror)

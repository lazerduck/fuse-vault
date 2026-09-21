# The SDK's no-OS tud_task_ext drains the queue until empty. MSC callbacks
# returning zero enqueue their own retry, so the task may never return while
# FIDO waits for application UI/polling. Bound each invocation to eight events.
# Generate a pinned local copy; never modify the installed Pico SDK.
set(usbd_source "${PICO_SDK_PATH}/lib/tinyusb/src/device/usbd.c")
file(SHA256 "${usbd_source}" usbd_hash)
if(NOT usbd_hash STREQUAL "f15a6da11eca127b792bd04d3270e6ea3901b0a29c24f462d5687d63da1df6cb")
  message(FATAL_ERROR "TinyUSB USBD source changed: review the event-budget patch")
endif()
file(READ "${usbd_source}" usbd_code)
set(task_loop "  // Loop until there is no more events in the queue\n  while (1) {")
string(FIND "${usbd_code}" "${task_loop}" task_loop_at)
if(task_loop_at EQUAL -1)
  message(FATAL_ERROR "TinyUSB task loop not found")
endif()
string(REPLACE "${task_loop}"
  "  // Bound self-requeuing MSC retries so UI, CDC and FIDO polling can run.\n  unsigned fv_events = 0;\n  while (fv_events++ < 8) {" usbd_code "${usbd_code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/usbd_budget.c" "${usbd_code}")
get_target_property(tinyusb_sources tinyusb_device_base INTERFACE_SOURCES)
list(FILTER tinyusb_sources EXCLUDE REGEX "/device/usbd\\.c$")
set_property(TARGET tinyusb_device_base PROPERTY INTERFACE_SOURCES "${tinyusb_sources}")
target_sources(fuse_vault_security PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/usbd_budget.c")

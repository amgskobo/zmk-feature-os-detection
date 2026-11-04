cmake_minimum_required(VERSION 3.20.0)
list(APPEND DTS_ROOT "${ZMK_ROOT}/app")
list(APPEND SYSCALL_INCLUDE_DIRS "${ZMK_ROOT}/app/include")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(os_detection_settings_view)
if(NOT EXISTS "${CUSTOM_SETTINGS_MODULE}/src/custom_settings.c" OR
   NOT EXISTS "${ZMK_ROOT}/app/src/event_manager.c")
  message(FATAL_ERROR "Select resolved CUSTOM_SETTINGS_MODULE and ZMK_ROOT checkouts")
endif()
target_sources(app PRIVATE main.c ../../src/os_detection_settings.c
  "${CUSTOM_SETTINGS_MODULE}/src/custom_settings.c"
  "${CUSTOM_SETTINGS_MODULE}/src/custom_settings_notify.c"
  "${CUSTOM_SETTINGS_MODULE}/src/custom_settings_array.c"
  "${ZMK_ROOT}/app/src/event_manager.c")
target_include_directories(app BEFORE PRIVATE include ../../include
  "${CUSTOM_SETTINGS_MODULE}/include" "${ZMK_ROOT}/app/include")
target_compile_definitions(app PRIVATE CONFIG_ZMK_LOG_LEVEL=0
  CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE=64
  CONFIG_ZMK_CUSTOM_SETTINGS_LARGE_VALUE_MAX_SIZE=128
  CONFIG_ZMK_CUSTOM_SETTINGS_CUSTOM_SUBSYSTEM_ID_MAX_LEN=32
  CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN=48 CONFIG_ZMK_CUSTOM_SETTINGS_ARRAY=1
  CONFIG_ZMK_CUSTOM_SETTINGS_TEMP_SLOTS=2 CONFIG_ZMK_CUSTOM_SETTINGS_TEMP_SLOT_SIZE=32
  CONFIG_ZMK_CUSTOM_SETTINGS_ARRAY_VIEW_POOL_SIZE=16)
zephyr_linker_sources(ROM_SECTIONS "${CUSTOM_SETTINGS_MODULE}/include/linker/zmk-custom-settings-rom.ld")
zephyr_linker_sources(DATA_SECTIONS "${CUSTOM_SETTINGS_MODULE}/include/linker/zmk-custom-settings.ld")
zephyr_linker_sources(RODATA "${ZMK_ROOT}/app/include/linker/zmk-events.ld")
target_link_options(zephyr_interface INTERFACE -Wl,--wrap=settings_save_one
  -Wl,--wrap=zmk_custom_setting_find_array_element
  -Wl,--wrap=zmk_custom_setting_read_array_by_key
  -Wl,--wrap=zmk_custom_setting_write_array_by_key)

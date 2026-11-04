cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(os_detection_ble_session)
target_include_directories(app BEFORE PRIVATE include ../../include)
target_sources(app PRIVATE main.c)
target_compile_definitions(app PRIVATE CONFIG_ZMK_CUSTOM_SETTINGS=1
  CONFIG_ZMK_OS_DETECTION_BLE_SETTLE_MS=1000
  OS_BLE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/os_detection_ble.c")

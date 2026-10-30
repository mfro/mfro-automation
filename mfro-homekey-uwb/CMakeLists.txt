# mfro-homekey-uwb: Apple Home Key (Aliro, BLE + UWB) reader on a XIAO ESP32-C6 +
# DWM3000EVB, Matter over Thread. Built on the ultrawidelock ESP32 components.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of the esp-matter repo")
endif()

# The ultrawidelock checkout providing ports/esp32/components (DW3000 UWB engine,
# credential reader, NimBLE transport, crypto). Default: sibling of automation/.
if(DEFINED ENV{ULTRAWIDELOCK_PATH})
    set(ULTRAWIDELOCK_PATH $ENV{ULTRAWIDELOCK_PATH})
else()
    get_filename_component(ULTRAWIDELOCK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../ultrawidelock" ABSOLUTE)
endif()
if(NOT EXISTS "${ULTRAWIDELOCK_PATH}/ports/esp32/components")
    message(FATAL_ERROR "ultrawidelock not found at ${ULTRAWIDELOCK_PATH}; set ULTRAWIDELOCK_PATH")
endif()

set(PROJECT_VER "0.1.0")
set(PROJECT_VER_NUMBER 1)

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ULTRAWIDELOCK_PATH}/ports/esp32/components")

# Components under ports/esp32/components this image does not use. ESP-IDF only
# builds what something requires, but excluding them keeps their Kconfig menus and
# managed dependencies out of the build entirely.
set(EXCLUDE_COMPONENTS piv_ccid ultrawidelock_satlink ultrawidelock_anchor ultrawidelock_dfu
    ultrawidelock_ble_central)

project(mfro_homekey_uwb)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# RISC-V: project_include.cmake sets -Wno-format but not the flags that depend on it.
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)

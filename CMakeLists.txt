# Handoff — Shake hands. Share contacts.
#
# Body-coupled wristband firmware for the Raspberry Pi Pico 2 W (RP2350).
# See docs/body-coupled-handshake-design.md for the full design.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# Pico 2 W: RP2350 + CYW43439. The 'W' matters — the onboard LED and the SMPS
# mode pin both hang off the wireless chip, not off RP2350 GPIO.
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(handoff C CXX ASM)

pico_sdk_init()

# Collect every app's artifacts in build/ rather than build/firmware/<app>/,
# so BOOTSEL drag-and-drop and the extension's Flash command both have one
# predictable path.
#
# This needs both properties: the SDK takes the .elf/.bin/.hex location from
# RUNTIME_OUTPUT_DIRECTORY but reads the .uf2 location out of
# ARCHIVE_OUTPUT_DIRECTORY (see pico_add_uf2_output in the SDK's
# tools/CMakeLists.txt). Both are read at configure time, so they must be set
# before pico_add_extra_outputs() runs — hence wrapping the two together.
function(handoff_add_app TARGET)
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    pico_add_extra_outputs(${TARGET})
endfunction()

add_subdirectory(firmware)

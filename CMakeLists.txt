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
    # The SDK puts core 0's stack at the top of the 4 kB scratch-Y bank and
    # core 1's at the top of scratch X, the bank directly beneath it, with
    # nothing between them: a core-0 overflow lands in core 1's outermost
    # frames, which on every image here are the receiver's DSP state. That
    # is how a flash write took the body link down (see store.h).
    #
    # And 4 kB is not enough for core 0. The main loop and the whole BTstack
    # context share it, and with a phone connected the product image was
    # measured at ~3.9 kB before a single handshake had ended (15 Sep 2026:
    # every phone connection to the Release image ended in an MSPLIM fault
    # inside the BTstack interrupt; -O3 inlines on_done's 2 kB record into
    # main, so it sits under every interrupt). So core 0 gets 32 kB carved
    # from the top of main RAM (firmware/ld/*.incl override the SDK's stack
    # placement; RAM_LENGTH below is 512 kB less that region), core 1 keeps
    # its whole scratch bank, and the SDK's stack guard at the bottom of
    # each stack turns the next overflow into a hard fault on the bench
    # rather than a silently deaf band.
    target_compile_definitions(${TARGET} PRIVATE
        PICO_STACK_SIZE=0x8000
        PICO_CORE1_STACK_SIZE=0x1000
        PICO_USE_STACK_GUARDS=1
    )
    pico_add_linker_script_override_path(${TARGET} ${CMAKE_SOURCE_DIR}/firmware/ld GLOB_FILES)
    pico_set_linker_script_var(${TARGET} RAM_LENGTH 491520)
    pico_add_extra_outputs(${TARGET})
endfunction()

add_subdirectory(firmware)

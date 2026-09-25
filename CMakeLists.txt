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

# Link v2 §4 — the system clock is 144 MHz, not the SDK's 150 MHz default.
#
# Both link tones must be a whole, EVEN number of system cycles per period,
# because the fsk_out generator builds each period out of two equal halves.
# At 150 MHz a 180 kHz period is 833.33 cycles and cannot be generated at all.
# At 144 MHz it is 800, and 200 kHz is 720. See firmware/lib/hal/config.h,
# which carries the same number as HANDOFF_SYS_CLK_HZ and static-asserts it.
#
# This is a BOOT clock, not a runtime switch: clocks_init() brings the PLL up
# at 144 MHz once and USB never sees a frequency change. The SDK requires the
# PLL_SYS_* triple whenever SYS_CLK_HZ is overridden, so it is given here:
#
#   XOSC 12 MHz, refdiv 1 -> fbdiv 96 -> VCO 1152 MHz  (inside 750..1600)
#   1152 / (4 * 2) = 144 MHz exactly
#
# clk_usb keeps its own PLL (1200 MHz / 5 / 5 = 48 MHz) and clk_adc is left on
# PLL_USB by the hardware reset default, so 500 ksps is untouched by this —
# the bench verifies that rather than trusting it. clk_peri is on PLL_USB too
# (PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK defaults to 0).
#
# These must be set BEFORE pico_sdk_init(), because that is when the SDK's own
# targets are created and inherit this directory's compile definitions.
add_compile_definitions(
    SYS_CLK_HZ=144000000
    PLL_SYS_VCO_FREQ_HZ=1152000000
    PLL_SYS_POSTDIV1=4
    PLL_SYS_POSTDIV2=2
)

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
